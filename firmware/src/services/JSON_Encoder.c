#define LOG_LVL LOG_LEVEL_DEBUG
#define LOG_MODULE LOG_MODULE_ENCODER
/*! @file JSON_Encoder.c
 *
 * This file implements the functions to manage the JSON encoder
 */

#include "../services/DaqifiPB/DaqifiOutMessage.pb.h"
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "Util/StringFormatters.h"
#include "Util/Logger.h"
#include "encoder.h"
#include "JSON_Encoder.h"
#include "../HAL/ADC.h"
#include "../HAL/TimerApi/TimerApi.h"
#include "streaming.h"
#include "services/daqifi_settings.h"
#include "JSON_StringEscape.h"

#ifndef max
#define max(x,y) x >= y ? x : y
#endif // min

//! Size of temporal buffer used for JSON encoding purposes
#define TMP_MAX_LEN                                 64
//! Temporal buffer used for JSON encoding purposes
static char tmp[ TMP_MAX_LEN ];

/* #164: every closer that MUST still fit after the bytes it closes have been
 * written. Each reserve is derived from sizeof() the literal that is actually
 * emitted, so a reservation and its literal can never drift apart. These
 * replace the old ">= 65" magic pre-check, which reserved a byte count with no
 * relationship to anything it was protecting. */
#define JSON_OBJ_CLOSE      "\n}\n"
#define JSON_OBJ_CLOSE_LEN  (sizeof(JSON_OBJ_CLOSE) - 1u)   /* 3 */
#define JSON_DI_CLOSE       "],\n"
#define JSON_DI_CLOSE_LEN   (sizeof(JSON_DI_CLOSE) - 1u)    /* 3 */
#define JSON_AI_CLOSE       "\n],\n"
#define JSON_AI_CLOSE_LEN   (sizeof(JSON_AI_CLOSE) - 1u)    /* 4 */

// Track whether JSON header has been sent (reset when streaming stops)
static bool jsonHeaderSent = false;

/* #164 (audit): one-call DIO deferral -- see the note beside `deferDio` in
 * Json_Encode. Set when an ADC sample failed to fit a max-room call ONLY
 * because a committed "di":[...] had already taken room ahead of it, which is
 * the one shape that leaves the permanence test at the end of the ADC loop
 * unable to fire. It makes the next max-room call skip the DIO block so that
 * test gets the whole-buffer measurement it needs, and is spent by that call
 * whether or not it changed the answer, so it can never latch DIO off.
 * Written and read only on streaming_Task inside Json_Encode, and cleared
 * below on the SCPI task at session start -- the same single-writer plus
 * quiescent-reset discipline jsonHeaderSent already relies on there
 * (streaming.c, beside csv_ResetEncoder()). */
static bool jsonDioDeferred = false;

/**
 * @brief Reset JSON encoder state (call when streaming stops)
 */
void json_ResetEncoder(void) {
    jsonHeaderSent = false;
    jsonDioDeferred = false;
}

/**
 * @brief Generate JSON metadata header with device info (sent once per session)
 *
 * Outputs a JSON object containing device metadata similar to CSV header:
 * {"meta":{"dev":"Nyquist NQ1","sn":"0123456789ABCDEF","tick_hz":1000000}}
 *
 * @param out      Output buffer
 * @param buffSize Size of output buffer
 * @return Bytes written (0 on failure)
 */
static size_t generateJsonHeader(char *out, size_t buffSize) {
    if (!out || buffSize == 0) {
        return 0;
    }

    const tBoardConfig* boardConfig = (const tBoardConfig*)BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    if (!boardConfig) {
        LOG_E("[JSON] Board config is NULL, cannot generate header");
        return 0;
    }

    uint8_t variant = 0;
    uint64_t serialNum = 0;
    void* pVar = BoardConfig_Get(BOARDCONFIG_VARIANT, 0);
    void* pSer = BoardConfig_Get(BOARDCONFIG_SERIAL_NUMBER, 0);
    if (pVar) variant = *(uint8_t*)pVar;
    if (pSer) serialNum = *(uint64_t*)pSer;

    // Get timestamp tick rate (0 = timer not initialized, indicates config problem)
    uint32_t tickRate = TimerApi_FrequencyGet(boardConfig->StreamingConfig.TSTimerIndex);

    // Determine required size dynamically (avoids hardcoded buffer size)
    int need = snprintf(NULL, 0,
        "{\"meta\":{\"dev\":\"%s %u\",\"sn\":\"%016llX\",\"tick_hz\":%u}}\n",
        DAQIFI_PRODUCT_NAME,
        variant,
        (unsigned long long)serialNum,
        tickRate);
    if (need < 0) {
        return 0;
    }
    size_t required = (size_t)need + 1; // +1 for null terminator
    if (required > buffSize) {
        return 0;
    }

    // Generate compact JSON metadata object
    int written = snprintf(out, buffSize,
        "{\"meta\":{\"dev\":\"%s %u\",\"sn\":\"%016llX\",\"tick_hz\":%u}}\n",
        DAQIFI_PRODUCT_NAME,
        variant,
        (unsigned long long)serialNum,
        tickRate);

    if (written < 0 || (size_t)written >= buffSize) {
        return 0;
    }

    return (size_t)written;
}

bool json_IsHeaderSent(void) {
    return jsonHeaderSent;
}

size_t json_GenerateHeaderToBuffer(char* buffer, size_t size) {
    if (!buffer || size == 0) return 0;
    return generateJsonHeader(buffer, size);
}

size_t Json_Encode(tBoardData* state,
        NanopbFlagsArray* fields,
        uint8_t* pBuffer, size_t buffSize) {
    int tmpLen = 0;
    char* charBuffer = (char*) pBuffer;
    size_t startIndex = 0;
    size_t initialOffsetIndex = 0;
    size_t objStart = 0;
    size_t i = 0;
    bool encodeDIO = false;
    bool encodeADC = false;
    /* #164: set only when an ARRAY of samples has been committed into THIS
     * object and not subsequently rolled back -- a "di":[...] that closed, or
     * an "ai":[...] whose sample was popped. Deliberately NOT set by the
     * scalar fields above: when the digital_data tag is absent the ADC block
     * rewinds over every one of them (see the guard after that block). */
    bool objHasPayload = false;

    if (pBuffer == NULL) {
        return 0; // Return 0 if buffer is NULL
    }

    if (buffSize < 10) {
        return 0;  // Minimum buffer size check
    }

    // Generate metadata header on first call (once per streaming session)
    if (!jsonHeaderSent) {
        size_t headerLen = generateJsonHeader(charBuffer, buffSize);
        if (headerLen == 0) {
            // Not enough space for header
            if (buffSize > 0) {
                charBuffer[0] = '\0';
            }
            return 0;
        }
        startIndex = headerLen;
        initialOffsetIndex = startIndex;
        jsonHeaderSent = true;

        // If no room left to start an object, send header now and return
        if (buffSize - startIndex <= 3) {  // Need at least "{\n" (3 bytes)
            if (buffSize > 0) {
                size_t term = startIndex < buffSize ? startIndex : (buffSize - 1);
                charBuffer[term] = '\0';
            }
            return startIndex;  // Return header bytes (valid data)
        }
    }

    // Start JSON sample object (write at current offset)
    /* #164: rollback point for the object as a whole. Any failure that would
     * leave this object open must restore startIndex to here -- never emit a
     * bare "{\n" and call it a return value. */
    objStart = startIndex;
    int objWritten = snprintf(charBuffer + startIndex, buffSize - startIndex, "{\n");
    if (objWritten < 0 || objWritten >= (int)(buffSize - startIndex)) {
        // Could not start a new JSON object; return what we have (e.g., header)
        if (buffSize > 0) {
            size_t term = startIndex < buffSize ? startIndex : (buffSize - 1);
            charBuffer[term] = '\0';
        }
        return startIndex;  // Return already-written bytes (e.g., header)
    }
    startIndex += objWritten;
    initialOffsetIndex = startIndex;

    for (i = 0; i < fields->Size; ++i) {
        switch (fields->Data[i]) {
            case DaqifiOutMessage_msg_time_stamp_tag:
            {
                int written = snprintf(charBuffer + startIndex,
                        buffSize - startIndex,
                        "\"ts\":%u,\n",
                        state->StreamTrigStamp);
                if (written < 0 || written >= (int)(buffSize - startIndex)) {
                    /* #164: this used to `return startIndex`, which emitted a
                     * bare "{\n" -- an object opened and never closed. Roll the
                     * object back and return only bytes that are complete on
                     * their own (the metadata header, or nothing at all). */
                    startIndex = objStart;
                    if (buffSize > 0) {
                        size_t term = startIndex < buffSize ? startIndex : (buffSize - 1);
                        charBuffer[term] = '\0';
                    }
                    return startIndex;
                }
                startIndex += written;
                break;
            }
            case DaqifiOutMessage_analog_in_data_tag:
                encodeADC = true;
                break;
            case DaqifiOutMessage_digital_data_tag:
                encodeDIO = true;
                break;
            case DaqifiOutMessage_device_status_tag:
                //TODO: message.device_status;
                break;
            case DaqifiOutMessage_batt_status_tag:
                //TODO: message.bat_level;
                break;
            case DaqifiOutMessage_pwr_status_tag:
                //TODO:  message.pwr_status;
                break;
            case DaqifiOutMessage_temp_status_tag:
                //TODO:  message.temp_status;
                break;
            case DaqifiOutMessage_analog_out_data_tag:
                //TODO:  message.analog_out_data[8];
                break;
            case DaqifiOutMessage_ip_addr_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                inet_ntop(AF_INET, &wifiSettings->ipAddr.Val, tmp, TMP_MAX_LEN);
                tmpLen = strlen(tmp);
                if (tmpLen > 0) {
                    /* Fixed-format dotted quad from inet_ntop() -- no character
                     * outside [0-9.] can occur, so no escaping is needed. */
                    int written = snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "\"ip\":\"%s\",\n",
                            tmp);
                    if (written < 0 || written >= (int)(buffSize - startIndex)) {
                        // Optional field - skip on buffer full, continue processing
                        break;
                    }
                    startIndex += written;
                }
                break;
            }
            case DaqifiOutMessage_host_name_tag:
            {
                //                WifiSettings* wifiSettings =                                
                //                        &state->wifiSettings;
                //                tmpLen = min(                                               
                //                        strlen(wifiSettings->hostName),                     
                //                        WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN);
                //                
                //                if (tmpLen > 0)
                //                {
                //                    startIndex += snprintf(                                 
                //                        charBuffer + startIndex,                            
                //                        JSON_ENCODER_BUFFER_SIZE - startIndex,              
                //                        " \"host\"=\"%s\",\n\r",                            
                //                        wifiSettings->hostName);
                //                }

                break;
            }
            case DaqifiOutMessage_mac_addr_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                tmpLen = MacAddr_ToString(wifiSettings->macAddr.addr, tmp, TMP_MAX_LEN);
                if (tmpLen > 0) {
                    /* Fixed-format hex-and-colons from MacAddr_ToString() -- no
                     * character needing an escape can occur. */
                    int written = snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "\"mac\":\"%s\",\n",
                            tmp);
                    if (written < 0 || written >= (int)(buffSize - startIndex)) {
                        // Optional field - skip on buffer full, continue processing
                        break;
                    }
                    startIndex += written;
                }
                break;
            }
            case DaqifiOutMessage_ssid_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                /* #164 (Qodo catch): strlen() must not run on ssid before it
                 * is bounded -- ssid is a fixed-size field written by
                 * SCPI_SafeParamString(), which is NUL-terminating in the
                 * normal write path, but nothing here may assume that holds
                 * for every possible source (a boot-time / NVM-loaded value
                 * that never went through that setter). Calling strlen()
                 * FIRST and clamping the result SECOND still reads past the
                 * field if no NUL exists anywhere in it -- the exact
                 * over-read this fix exists to close. Scan bounded by
                 * WDRV_WINC_MAX_SSID_LEN from the start, matching the bound
                 * escape_json_string() itself already enforces on inLen. */
                tmpLen = 0;
                while (tmpLen < (int)WDRV_WINC_MAX_SSID_LEN
                        && wifiSettings->ssid[tmpLen] != '\0') {
                    tmpLen++;
                }
                if (tmpLen > 0) {
                    /* #164: the SSID is free-form. SCPI_LANSsidSet() ->
                     * SCPI_SafeParamString() does a bare memcpy with NO
                     * character validation, so a '"' or '\' in the SSID landed
                     * verbatim in the output and produced invalid JSON.
                     *
                     * Escape into the existing `tmp` scratch buffer (already
                     * used by the ip/mac cases above) rather than a new
                     * stack-local: this file's `Json_Encode` runs only on
                     * streaming_Task, whose measured peak (692 words) already
                     * sits right at the documented 2x-of-1392 margin, so ANY
                     * new stack frame narrows it (Qodo catch). TMP_MAX_LEN
                     * (64) is smaller than the theoretical worst case
                     * JSON_ESC_MAX_LEN (193, every one of 32 SSID bytes
                     * needing a full \u00XX) -- that only matters if this
                     * field is ever wired into the streaming path (it is not
                     * today, see the module-level note above) AND carries a
                     * pathological SSID; the fallback there is the same
                     * "does not fit escaped -- omit" path every optional
                     * field in this encoder already takes. */
                    size_t escLen = escape_json_string(wifiSettings->ssid,
                            (size_t)tmpLen, tmp, TMP_MAX_LEN);
                    if (escLen == 0) {
                        // Does not fit escaped - omit the optional field
                        break;
                    }
                    int written = snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "\"ssid\":\"%s\",\n",
                            tmp);
                    if (written < 0 || written >= (int)(buffSize - startIndex)) {
                        // Optional field - skip on buffer full, continue processing
                        break;
                    }
                    startIndex += written;
                }
                break;
            }
            case DaqifiOutMessage_digital_port_dir_tag:
                //TODO:  message.digital_port_dir;
                break;
            case DaqifiOutMessage_analog_in_port_rse_tag:
                //TODO:  message.analog_in_port_rse;
                break;
            case DaqifiOutMessage_analog_in_port_enabled_tag:
                //TODO:  message.analog_in_port_enabled;
                break;
            case DaqifiOutMessage_analog_in_port_range_tag:
                //TODO:  message.analog_in_port_range;
                break;
            case DaqifiOutMessage_analog_in_res_tag:
                //TODO:  message.analog_in_res;
                break;
            case DaqifiOutMessage_analog_out_res_tag:
                //TODO:  message.analog_out_res;
                break;
            case DaqifiOutMessage_device_pn_tag:
            {
                //TODO:  message.device_pn[32];
                break;
            }
            case DaqifiOutMessage_device_port_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                int written = snprintf(charBuffer + startIndex,
                        buffSize - startIndex,
                        "\"port\":%u,\n",
                        wifiSettings->tcpPort);
                if (written < 0 || written >= (int)(buffSize - startIndex)) {
                    // Optional field - skip on buffer full, continue processing
                    break;
                }
                startIndex += written;
                break;
            }
            case DaqifiOutMessage_wifi_security_mode_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                int written = snprintf(charBuffer + startIndex,
                        buffSize - startIndex,
                        "\"sec\":%u,\n",
                        wifiSettings->securityMode);
                if (written < 0 || written >= (int)(buffSize - startIndex)) {
                    // Optional field - skip on buffer full, continue processing
                    break;
                }
                startIndex += written;
                break;
            }
            case DaqifiOutMessage_friendly_device_name_tag:
            {
                // #14: emit the user-defined friendly name when set.
                const char* friendlyName = daqifi_settings_GetFriendlyName();
                if (friendlyName[0] == '\0') {
                    break;  // unset — omit the field
                }
                /* #164: defence in depth, in two parts.
                 *
                 * (a) daqifi_settings_FriendlyNameIsValid() (#625) already
                 * rejects '"', '\' and every byte outside 0x20..0x7E, and
                 * SetFriendlyName() clears the cache when that check fails --
                 * so nothing reaching here needs ESCAPING today. The encoder
                 * must not depend on a validator in another module staying
                 * that strict, and routing both free-form fields through one
                 * helper is what keeps them from diverging.
                 *
                 * (b) bound the length SCAN itself, mirroring the ssid_tag
                 * fix above (Qodo catch on an earlier version of this
                 * comment: it cited "CLAUDE.md #409" for a BSS-init claim
                 * that section does not make -- verified against the actual
                 * file, not memory, before rewriting this). The scan is
                 * bounded on its own merits, independent of any reset-path
                 * claim: gFriendlyDeviceName is declared as a fixed
                 * FRIENDLY_DEVICE_NAME_SIZE array, and a read of it must
                 * respect that declared bound rather than trust every
                 * possible producer of its contents to have NUL-terminated
                 * it -- the same discipline escape_json_string() itself
                 * already applies via its own inLen parameter.
                 *
                 * `tmp` (64 bytes, shared with the ip/mac/ssid cases -- see
                 * the ssid_tag comment above) is far more than the max
                 * FRIENDLY_DEVICE_NAME_SIZE-1 (31) unescaped chars this field
                 * can ever hold. */
                size_t friendlyNameLen = 0;
                while (friendlyNameLen < (size_t)(FRIENDLY_DEVICE_NAME_SIZE - 1)
                        && friendlyName[friendlyNameLen] != '\0') {
                    friendlyNameLen++;
                }
                size_t escLen = escape_json_string(friendlyName,
                        friendlyNameLen, tmp, TMP_MAX_LEN);
                if (escLen == 0) {
                    // Does not fit escaped - omit the optional field
                    break;
                }
                int written = snprintf(charBuffer + startIndex,
                        buffSize - startIndex,
                        "\"friendlyName\":\"%s\",\n",
                        tmp);
                if (written < 0 || written >= (int)(buffSize - startIndex)) {
                    // Optional field - skip on buffer full, continue processing
                    break;
                }
                startIndex += written;
                break;
            }
            default:
                // Skip unknown fields
                break;
        }
    }

    /* #164 (audit): half the proof that a "did not fit" is PERMANENT and not
     * merely "full right now". streaming.c hands us `bufferSize - packetSize`
     * and resets packetSize to 0 before every packet-build loop, so its
     * batchIdx == 0 call -- which runs on EVERY encoder wake while the AIN
     * queue is non-empty -- passes the whole encoder buffer, and no call can
     * ever pass more. Equality therefore means "this call already offered the
     * most room that will ever exist"; anything less is a partially-filled
     * buffer a later call will beat, and must keep today's retry-forever
     * behaviour. Read through the accessor rather than assumed, so a
     * non-streaming caller with a buffer of its own simply never satisfies it
     * and stays on the old, non-consuming path. The other half is at the ADC
     * loop's end; this is declared above the DIO block because the deferral
     * immediately below needs it too. */
    const bool fullCapacityCall =
            (buffSize == (size_t) Streaming_GetEncoderBufferSize());

    /* #164 (audit): the permanence test needs the largest room that will ever
     * exist, which means BOTH a full-capacity call AND nothing committed
     * ahead of the ADC block in this object. A committed "di":[...] breaks
     * the second half, and DIO_StreamingTrigger() (DIO.c) pushes one DIO
     * sample per streaming tick whenever DIOGlobalEnable is set, so in a
     * DIO-co-streaming session that can hold on every wake -- leaving a
     * sample that fits NO buffer stalled at the head of the AIN queue
     * forever, which is exactly the defect the test exists to break.
     *
     * Nothing inside a single call can tell that apart from an honestly-full
     * buffer, and no bound on the DI array's share exists to test against:
     * its worst case is MAX_DIO_SAMPLE_COUNT (256, DIOConfig.h) elements of
     * ~50 B, past even the 65536 B maximum encoder buffer, and 21 of them
     * already exceed the 1024 B ENCODER_BUFFER_MIN.
     *
     * So rather than guess, ARRANGE for the measurement: skip the DIO block
     * for exactly one max-room call. Nothing is lost -- DI elements are
     * popped only inside that block, so a skipped call leaves all of them
     * queued for the next one, and an object carrying "ai" but no "di" while
     * digital_data was requested is the same shape this encoder already
     * emits whenever the DI array rolls back (see that path below). The skip
     * is gated on encodeADC so a DIO-only message can never be turned into a
     * payload-free object, and on the same fullCapacityCall/objStart == 0
     * pair the test itself requires, so the latch is spent only on a call
     * that can actually settle the question. */
    const bool deferDio = jsonDioDeferred && encodeADC && fullCapacityCall
            && objStart == 0;
    if (encodeADC && fullCapacityCall && objStart == 0) {
        jsonDioDeferred = false;
    }

    // Encode DIO if needed
    if (encodeDIO && !deferDio) {
        size_t diStart = startIndex;

        int written = snprintf(charBuffer + startIndex,
                buffSize - startIndex,
                "\"di\":[");
        if (written < 0 || written >= (int)(buffSize - startIndex)) {
            /* #164: was `return startIndex`, i.e. a bare "{\n". Roll the object
             * back instead -- if six bytes will not fit, nothing else will. */
            startIndex = objStart;
            if (buffSize > 0) {
                size_t term = startIndex < buffSize ? startIndex : (buffSize - 1);
                charBuffer[term] = '\0';
            }
            return startIndex;
        }
        startIndex += written;

        size_t diElementsStart = startIndex;

        /* #164: the ">= 65" pre-check is gone. A DIO element is popped as soon
         * as it is written and cannot be put back, so instead of a magic
         * margin each element reserves exactly the two closers that must still
         * fit after it: this array's "],\n" and the enclosing object's "\n}\n".
         * That is what makes the two failure branches below unreachable rather
         * than merely unlikely. */
        while (!DIOSampleList_IsEmpty(&state->DIOSamples)) {
            DIOSample data;
            // Peek first to avoid data loss if write fails
            if (!DIOSampleList_PeekFront(&state->DIOSamples, &data)) break;

            size_t elemRoom = buffSize - startIndex;
            if (elemRoom <= (JSON_DI_CLOSE_LEN + JSON_OBJ_CLOSE_LEN)) {
                break;  // no room for an element plus the closers it owes
            }
            elemRoom -= (JSON_DI_CLOSE_LEN + JSON_OBJ_CLOSE_LEN);

            int elemWritten = snprintf(charBuffer + startIndex,
                    elemRoom,
                    "{\"ts\":%u, \"mask\":%u, \"val\":%u},",
                    state->StreamTrigStamp - data.Timestamp,
                    data.Mask,
                    data.Values);
            if (elemWritten < 0 || elemWritten >= (int)elemRoom) {
                break;  // Keep sample for next attempt
            }

            // Write succeeded - commit by removing sample from queue
            startIndex += elemWritten;
            DIOSampleList_PopFront(&state->DIOSamples, &data);
        }

        if (startIndex == diElementsStart) {
            // No elements were written; roll back emission of "di":[
            startIndex = diStart;
        } else {
            // Remove trailing comma and close array
            if (startIndex > 0 && charBuffer[startIndex - 1] == ',') {
                startIndex -= 1;
            }
            /* Reserve the object closer here too: the elements above are
             * already popped, so a `return 0` at the bottom of this function
             * would destroy them. The element reservation guarantees this
             * branch cannot be taken; it rolls back rather than emitting an
             * unclosed array if that guarantee is ever broken. */
            size_t closeRoom = buffSize - startIndex;
            if (closeRoom <= JSON_OBJ_CLOSE_LEN) {
                startIndex = diStart;
            } else {
                closeRoom -= JSON_OBJ_CLOSE_LEN;
                int closeWritten = snprintf(charBuffer + startIndex,
                        closeRoom,
                        JSON_DI_CLOSE);
                if (closeWritten < 0 || closeWritten >= (int)closeRoom) {
                    startIndex = diStart;
                } else {
                    startIndex += closeWritten;
                    /* Committed and closed. Nothing after this point rolls it
                     * back: the ADC block rewinds only as far as
                     * initialOffsetIndex, which is set to this post-DIO
                     * position immediately below. */
                    objHasPayload = true;
                }
            }
        }

        initialOffsetIndex = startIndex; // so that analog data can be appended
    }

    // Encode ADC if needed
    if (encodeADC) {
        startIndex = initialOffsetIndex; // Remove the initial timestamp added

        uint32_t qSize = AInSampleList_Size();
        AInPublicSampleList_t *pPublicSampleList;
        // Cache mapping pointer and fields in locals (#269)
        const AInChannelMapping* mapping = Streaming_GetChannelMapping();
        const uint8_t mapCount = mapping->count;
        const uint8_t* mapChannelIds = mapping->channelIds;
        const uint8_t* mapConfigIdx = mapping->configIndices;
        // Hoist runtime config lookup out of the per-sample loop (#269)
        StreamingRuntimeConfig *pStreamCfg = BoardRunTimeConfig_Get(
                BOARDRUNTIME_STREAMING_CONFIGURATION);
        uint8_t precision = (pStreamCfg != NULL) ? pStreamCfg->VoltagePrecision : 4;
        bool rawMode = (pStreamCfg != NULL) ? pStreamCfg->RawOutputMode : false;   /* #158/#270 */
        /* #164: the ">= 65" pre-check is gone; the loop now ends when a sample
         * does not fit, which the per-sample rollback below makes safe. */
        /* #164 (audit): `fullCapacityCall`, declared above the DIO block, is
         * half the proof that a "did not fit" is PERMANENT rather than merely
         * "full right now". The other half is at this loop's end. */
        bool sampleDidNotFit = false;
        while (qSize > 0) {
            /* #164: PEEK -- never pop -- until this sample's whole block is on
             * the wire, exactly as csv_encoder.c's tryWriteRow() has always
             * done. The old code did PopFront() up front and FreeToPool() at
             * the bottom, so a buffer-full part way through the channel loop
             * destroyed a sample that had only been partly encoded. */
            if (!AInSampleList_PeekFront(&pPublicSampleList)) {
                break;
            }
            if (pPublicSampleList == NULL)
                break;

            /* Rollback point for this sample's atomic unit, which is the whole
             * of  "ts":<t>,\n"ai":[\n <channels> \n],\n  -- not just one
             * snprintf. The old code advanced startIndex past "ts":<t>,\n and
             * only then tried "ai":[\n; on failure it left the timestamp
             * committed, and the close-out below then stripped the ",\n" and
             * appended "\n],\n", emitting a ']' that closes nothing. */
            size_t sampleStart = startIndex;
            bool sampleOk = true;
            bool timestampAdded = false;
            // Clamp to the sample's own channelCount in case the mapping and
            // the sample fall out of sync. Defensive — they should always match.
            uint8_t chCount = mapCount;
            if (pPublicSampleList->channelCount < chCount) {
                chCount = (uint8_t)pPublicSampleList->channelCount;
            }
            for (uint8_t j = 0; j < chCount; j++) {
                if (!(pPublicSampleList->validMask & (1U << j)))
                    continue;

                uint8_t channelId = mapChannelIds[j];
                uint32_t rawValue = pPublicSampleList->Values[j];

                if (!timestampAdded) {
                    int written = snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "\"ts\":%u,\n",
                            pPublicSampleList->Timestamp);
                    if (written < 0 || written >= (int)(buffSize - startIndex)) {
                        sampleOk = false;
                        break;
                    }
                    startIndex += written;

                    written = snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "\"ai\":[\n");
                    if (written < 0 || written >= (int)(buffSize - startIndex)) {
                        sampleOk = false;
                        break;
                    }
                    startIndex += written;
                    timestampAdded = true;
                }

                // #158/#270: raw mode emits the ADC code directly (no cal /
                // no voltage conversion / no double math).
                int written;
                if (rawMode) {
                    // #620: AD7609 (18-bit bipolar) negative codes are
                    // sign-extended two's-complement int32 (e.g. -4096 =
                    // 0xFFFFF000); emit SIGNED (%d) to match the PB sint32
                    // raw path (unsigned misprinted negatives as ~4.29e9).
                    written = snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "{\"ch\":%u, \"val\":%d},\n",
                            channelId,
                            (int32_t)rawValue);
                } else
                // Convert raw ADC value to voltage by board config index
                // directly (#268/#269). Skips O(N) channel-ID search.
                if (precision == 0) {
                    // Integer millivolts (backwards compatible)
                    double voltage_mv = ADC_ConvertToVoltageByIndex(
                        mapConfigIdx[j], rawValue) * 1000.0;
                    int32_t mv;
                    if (voltage_mv > (double)INT32_MAX) mv = INT32_MAX;
                    else if (voltage_mv < (double)INT32_MIN) mv = INT32_MIN;
                    else mv = (int32_t)(voltage_mv >= 0.0 ? voltage_mv + 0.5 : voltage_mv - 0.5);
                    written = snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "{\"ch\":%u, \"val\":%d},\n",
                            channelId,
                            (int)mv);
                } else {
                    // Volts with N decimal places
                    double voltage = ADC_ConvertToVoltageByIndex(
                        mapConfigIdx[j], rawValue);
                    written = snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "{\"ch\":%u, \"val\":%.*f},\n",
                            channelId,
                            (int)precision, voltage);
                }
                if (written < 0 || written >= (int)(buffSize - startIndex)) {
                    sampleOk = false;
                    break;
                }
                startIndex += written;
            }

            if (!sampleOk) {
                /* Buffer filled part way through this sample. Discard the
                 * fragment and LEAVE the sample queued -- the next encoder
                 * call re-encodes it whole. Nothing has been popped. */
                startIndex = sampleStart;
                sampleDidNotFit = true;   /* #164 audit -- see loop end */
                break;
            }

            if (!timestampAdded) {
                /* validMask selected no channel: the sample is real but
                 * carries nothing to emit. Consume it and move on to the
                 * NEXT queued sample rather than ending the batch here --
                 * this branch writes nothing to charBuffer, so there is
                 * nothing to close and no reason to stop draining the
                 * queue. (Qodo catch: an earlier version of this fix used
                 * `break`, which -- unlike the old `startIndex ==
                 * initialOffsetIndex` first-iteration check it replaced --
                 * could stall an entire encoder call on a single
                 * all-invalid tick, one sample per call, while the queue
                 * behind it kept growing.) */
                if (AInSampleList_PopFront(&pPublicSampleList)) {
                    AInSampleList_FreeToPool(pPublicSampleList);
                }
                qSize--;
                continue;
            }

            // Remove trailing comma and close adc array
            size_t closeIndex = startIndex;
            if (closeIndex >= 2 && charBuffer[closeIndex - 2] == ',') {
                closeIndex -= 2; // Remove trailing comma
            }
            /* Reserve the object closer: a sample may only commit if the
             * object can still be closed after it. Without this a committed
             * sample could leave the final "\n}\n" unable to fit, and that
             * path returns 0 -- discarding every sample already popped by this
             * call, not just the one that did not fit. */
            size_t closeRoom = buffSize - closeIndex;
            if (closeRoom <= JSON_OBJ_CLOSE_LEN) {
                startIndex = sampleStart;
                sampleDidNotFit = true;   /* #164 audit -- see loop end */
                break;
            }
            closeRoom -= JSON_OBJ_CLOSE_LEN;
            int closeWritten = snprintf(charBuffer + closeIndex,
                    closeRoom,
                    JSON_AI_CLOSE);
            if (closeWritten < 0 || closeWritten >= (int)closeRoom) {
                startIndex = sampleStart;   // whole block rolls back
                sampleDidNotFit = true;     /* #164 audit -- see loop end */
                break;
            }
            /* Committed: only now is it safe to consume the queue entry. Do
             * NOT advance startIndex past the write yet -- PopFront is
             * documented fallible (queue teardown / a torn-down receive
             * mid-encode), and if it returns false the entry we just wrote
             * is STILL queued. Returning those bytes anyway would let the
             * same sample be encoded and transmitted again on the next
             * call (Qodo catch). Only commit startIndex, free the pool
             * entry and decrement qSize once the pop actually succeeds. */
            if (!AInSampleList_PopFront(&pPublicSampleList)) {
                startIndex = sampleStart;
                break;
            }
            startIndex = closeIndex + closeWritten;
            objHasPayload = true;
            AInSampleList_FreeToPool(pPublicSampleList);
            qSize--;
        }

        /* #164 (audit, CONFIRMED HIGH): break the head-of-line block that a
         * sample which can NEVER fit would otherwise create.
         *
         * The rollback sites above leave the sample queued. That is right for
         * "the buffer is full right now" and is an infinite stall for "this
         * sample does not fit even an EMPTY buffer": the queue is FIFO and
         * drained only from the front, so such a sample is re-peeked, fails
         * and is rolled back on every call, forever, taking every sample
         * behind it with it -- the stream simply stops carrying ADC data with
         * no error raised. main's loop popped up front, so it LOST the sample
         * but the queue advanced; this PR's peek-before-pop plus the object
         * rollback removed the only thing that was advancing it. Reachable on
         * legal SCPI: CONFigure:ADC:chanCALB accepts any finite double, so a
         * value like 1e300 makes "%.*f" emit ~324 bytes for ONE channel and a
         * few channels then exceed a 1024-byte encoder buffer (the accepted
         * ENCODER_BUFFER_MIN, i.e. SYST:MEM:ENC:BUFfer 1024). It fails toward
         * silence, the direction CLAUDE.md's SCPI visibility principle calls
         * out as the worst one.
         *
         * Each condition is load-bearing:
         *
         *   sampleDidNotFit  -- set ONLY by the three room failures, never by
         *      the PopFront arm (a queue teardown, where consuming the sample
         *      is the double-transmit bug that arm exists to prevent).
         *   fullCapacityCall -- no future call can offer more room; see its
         *      definition above. Excludes streaming.c's batchIdx > 0 calls,
         *      which ARE partially-filled buffers and must keep retrying.
         *   objStart == 0    -- no metadata header sits ahead of this object.
         *      True on every call but the session's first; excluding that one
         *      costs a tick and keeps the header's bytes out of the bound
         *      below (it also has no accounting: that call returns headerLen,
         *      not 0, so streaming.c would book nothing).
         *   !objHasPayload   -- nothing else in this object committed. This
         *      SUBSUMES "this was the first sample the ADC section tried":
         *      inside the loop startIndex advances only at the commit that
         *      sets objHasPayload (the all-invalid-validMask path consumes
         *      its sample without writing a byte), so !objHasPayload means
         *      sampleStart was still initialOffsetIndex. It also means no
         *      "di":[...] committed ahead of the ADC block and ate room --
         *      without it a DIO-co-streaming session would drop samples a
         *      DIO-free call could have carried, and DIO_StreamingTrigger()
         *      pushes one sample per streaming tick, so that is the norm, not
         *      a corner case.
         *
         * The bound this buys: the room the sample got was
         * buffSize - initialOffsetIndex, and on this arm initialOffsetIndex is
         * either 2 (no digital_data tag -- the ADC block rewinds over the
         * message-level "ts" too; the deferral above forces this case for one
         * call when a committed DI array was what stood in the way) or
         * diStart, that is 2 + that "ts" field and so <= 19, when the tag was
         * present but the DI array rolled back without committing. The ceiling
         * any call can ever offer is buffSize - 2. So the test is EXACT
         * whenever the DI array did not commit, and in the single remaining
         * shape -- tag requested, array rolled back -- conservative by at most
         * 17 bytes: only a sample needing within 17 bytes of the ENTIRE
         * encoder buffer is affected, and the cost there is one dropped
         * sample, not a stall. Closing even that means changing what
         * initialOffsetIndex is when the DI array rolls back, which moves the
         * emitted field order and belongs to #959, not here. The case that IS
         * a stall -- a DI array that committed -- is NOT left as a residual:
         * it routes to the deferral arm below, which re-runs this test one
         * call later at buffSize - 2.
         *
         * Action: consume the sample so the queue head advances, and say so
         * once per session. Nothing here books the loss, deliberately:
         * objHasPayload is false, so the guard below rolls the object back to
         * objStart and returns 0, and streaming.c's existing `encoded == 0`
         * arm books exactly one encoder failure and one dropped sample. That
         * count is now TRUE -- one sample really was lost -- where before this
         * fix the same arm re-counted a still-queued sample on every retry. A
         * dedicated Streaming_Report* entry point (the Streaming_ReportSdDiscard
         * shape) was considered and rejected for exactly that reason: it would
         * double-count against the `encoded == 0` arm this path unavoidably
         * takes. PopFront's return is honoured rather than assumed -- it
         * rewrites pPublicSampleList with the real head, so nothing stale is
         * ever freed.
         *
         * LOG_E_SESSION, not LOG_E: one line per streaming session (the bit is
         * cleared by Streaming_ClearStats() at start), on a path that runs at
         * most once per encoder call, on streaming_Task (1392 words, 692 peak)
         * -- the same task that already carries streaming.c's LOG_E_SESSION
         * vsnprintf frames. */
        if (sampleDidNotFit && fullCapacityCall && objStart == 0) {
            if (!objHasPayload) {
                if (AInSampleList_PopFront(&pPublicSampleList)) {
                    AInSampleList_FreeToPool(pPublicSampleList);
                }
                LOG_E_SESSION(LOG_SESSION_JSON_SAMPLE_TOO_LARGE,
                        "JSON: sample (%u ch) does not fit a %u B encoder "
                        "buffer at precision %u - dropped",
                        (unsigned) mapCount, (unsigned) buffSize,
                        (unsigned) precision);
            } else if (startIndex == initialOffsetIndex) {
                /* #164 (audit): the DIO-blocked shape, and the reason the
                 * `!objHasPayload` arm above stays exact instead of becoming
                 * a residual. This call offered the most room that will ever
                 * exist; the ADC block committed nothing into it (startIndex
                 * never left initialOffsetIndex -- inside the loop it advances
                 * only at the commit that also sets objHasPayload, and the
                 * all-invalid-validMask path writes no bytes); yet
                 * objHasPayload is set, so the payload can only be a
                 * "di":[...] that took room ahead of us. Whether the sample is
                 * oversized or the DI array merely happened to be large this
                 * tick is not decidable here, so decide nothing: ask the next
                 * max-room call to skip the DIO block and re-run the test
                 * against the whole buffer. Bounded -- that call spends the
                 * latch and either fits the sample or takes the drop arm
                 * above -- so this cannot itself become a new unbounded
                 * retry. */
                jsonDioDeferred = true;
            }
        }
    }

    /* #164: an object that committed no sample payload must not ship at all.
     *
     * Every rollback above restores startIndex to the start of the unit that
     * failed -- the DIO element run, or ONE ADC sample -- which is correct for
     * that unit and says nothing about the enclosing object. When the failed
     * unit was the first thing this object would have carried, the close-out
     * below strips the message-level timestamp's trailing ",\n", appends
     * "\n}\n" and returns a NON-ZERO byte count for `{\n"ts":1000\n}\n` --
     * or, when the digital_data tag was not requested and the ADC block
     * therefore rewound over that timestamp too (initialOffsetIndex is still
     * just past "{\n"), for the empty `{\n\n}\n`. Both are well-formed JSON
     * records carrying no measurement, reported to the caller as bytes
     * successfully encoded.
     *
     * Checked ONCE here rather than at each `startIndex = sampleStart` site:
     * none of those can tell on its own whether the object still holds DIO
     * elements committed before the ADC block ran, and an object with
     * "di":[...] and no "ai" is a legitimate partial record that must ship.
     * objHasPayload records exactly that distinction.
     *
     * Scoped to messages that ASKED for sample data: a caller requesting only
     * scalar fields is entitled to an object built from them. streaming.c is
     * the sole caller today and always sets at least one of the two tags (it
     * breaks out of its batch loop when both queues are empty), so the scope
     * is about not making the rule wider than the defect.
     *
     * Rolls back to objStart -- not to sampleStart -- so the object never
     * exists, and returns the same two values the msg_time_stamp failure path
     * above can return: the metadata header this call just wrote, or nothing.
     * No sample is lost by returning here. A sample that did not fit is still
     * queued (nothing was popped), and a sample whose validMask selected no
     * channel was consumed deliberately, having nothing to emit either way.
     * Returning 0 makes streaming.c book one encoder failure and one dropped
     * sample, which is the shape that path already has for a tick that
     * produced nothing -- see its own #707/#745 note, "the encoder emits
     * nothing, and it was booked as a lost sample and an encoder failure". */
    if ((encodeADC || encodeDIO) && !objHasPayload) {
        startIndex = objStart;
        if (buffSize > 0) {
            size_t term = startIndex < buffSize ? startIndex : (buffSize - 1);
            charBuffer[term] = '\0';
        }
        return startIndex;
    }

    // Close the JSON object
    if (startIndex >= 2 && charBuffer[startIndex - 2] == ',') {
        startIndex -= 2; // Remove trailing comma
    }
    int written = snprintf(charBuffer + startIndex,
            buffSize - startIndex,
            JSON_OBJ_CLOSE);
    if (written < 0 || written >= (int)(buffSize - startIndex)) {
        // Truncated or error; incomplete JSON is invalid, signal failure
        if (buffSize > 0) {
            size_t term = (startIndex < buffSize) ? startIndex : (buffSize - 1);
            charBuffer[term] = '\0';
        }
        return 0;  // Invalid/incomplete JSON - return failure
    }
    startIndex += written;

    // Ensure safe null-termination without exceeding buffer
    if (buffSize > 0) {
        charBuffer[startIndex < buffSize ? startIndex : (buffSize - 1)] = '\0';
    }
    return startIndex; // Return the number of bytes written
}
