#include "daqifi_settings.h"
#include "wdrv_winc_client_api.h"
#include "HAL/NVM/nvm.h"
#include "version.h"
#include "state/board/BoardConfig.h"
#include "state/board/NQ3BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "Util/CRC32.h"   /* #306: settings integrity checksum (severs wolfSSL) */
/* #904: taskENTER_CRITICAL around the 64-bit cal stores below.
 * #908 r9: vTaskDelay as well, so the seqlock reader yields between retries
 *          instead of spinning at priority 7 against a bank load at 2. */
#include "FreeRTOS.h"
#include "task.h"

uint8_t gTempFflashBuffer[NVM_FLASH_ROWSIZE] __attribute__((coherent, aligned(16)));

/* #14: Runtime cache of the device friendly name. Seeded at boot from
 * NVM-persisted TopLevelSettings; read by the PB/JSON info-message
 * encoders and the SYST:DEVice:NAME? query; captured into the NVM blob
 * on the next TopLevelSettings save. Empty string = unset. Written only
 * from task context (SCPI callbacks / boot); the fixed buffer means
 * readers always see a valid NUL-terminated string. */
static char gFriendlyDeviceName[FRIENDLY_DEVICE_NAME_SIZE] = {0};

/* #908: set at boot when the factory-cal NVM page is blank or fails its
 * checksum, so the runtime CalM/CalB stay at their identity defaults. Also
 * cleared immediately by a successful CONF:ADC:SAVEFcal (SCPIADC.c's
 * CalSaveCommon), so a capability query between the save and the next
 * reboot reports the slot as present rather than the stale boot-time
 * reading -- Qodo caught this PR reporting "still missing" right after a
 * save that had already succeeded. Read by the CONF:CAP:JSON? emitter
 * (identity.cal). Never persisted: both writers re-derive it from a real
 * load/save outcome, never from NVM. Written only from task context (boot,
 * or the SCPI task handling SAVEFcal), before the SCPI transport tasks
 * that read it are created on the boot path. */
static bool gFactoryCalMissing = false;

/* #908 (Qodo item 3): WHICH BANK the live coefficients were last loaded from.
 *
 * gFactoryCalMissing answers "is the factory NVM SLOT populated?". It was also
 * being used to answer "which coefficients are IN EFFECT?", and those are two
 * different questions -- the defect Qodo reported as "failed saves report
 * identity defaults": a failed SAVEFcal sets the missing flag, the emitter
 * translated that straight into source:"identity", and measurements carried on
 * using the unchanged non-identity coefficients. One value cannot answer both,
 * and nothing could detect the mismatch.
 *
 * Tracked HERE, inside the load, rather than at the four call sites (boot's
 * factory and user loads in app_freertos.c, CalLoadCommon, and
 * ADCUseCalSetClaimed): the load is the single choke point every selection
 * passes through, so recording it here cannot be forgotten by a new caller.
 *
 * Defaults to the factory bank because that is what boot attempts first. Never
 * persisted -- it is re-derived from a real load outcome on every boot, like
 * gFactoryCalMissing, and written only from task context. */
static DaqifiSettingsType gActiveCalBank = DaqifiSettings_FactAInCalParams;

/* #908 (codex-r7): generation counter over the (coefficients, bank) pair.
 *
 * EVEN = stable, ODD = a load is part-way through. r6's fix compared the bank
 * before and after the scan, which detects a load that COMPLETED but is blind
 * to one IN PROGRESS -- because the bank is published LAST, a reader that
 * pre-empts between the copy loop and the bank store reads the OLD bank twice
 * while scanning the NEW coefficients, and reports a source the device never
 * had. Publishing the bank last was itself r3's fix (so the bank can never
 * name a load that did not land), which is precisely what makes the
 * in-progress window invisible to a bank comparison.
 *
 * volatile because the reader's two loads must not be hoisted, cached or
 * reordered around the scan between them; aligned 32-bit accesses are atomic
 * on PIC32MZ, so the counter itself needs no critical section.
 *
 * ⚠ Bumped by EVERY writer of the live coefficients -- the bank load and both
 * CONF:ADC:chanCALM / chanCALB setters -- via
 * daqifi_settings_BeginCalWrite/EndCalWrite.
 *
 * An earlier revision of this comment said the setters were "deliberately not
 * covered", on the argument that either value a reader sees across ONE setter
 * is a state the device genuinely had. That is true of one setter and false of
 * two, which is what the r8 audit caught: an early-exiting identity scan reads
 * channel 0 as identity and moves on; one setter then CLEARS a later channel
 * while another SETS channel 0; the scan reads the later channel as identity
 * and concludes ALL identity -- a view that held at no instant. The argument
 * was about a single writer and the hazard needs only two. */
static volatile uint32_t gCalGeneration = 0u;

/* #625: the friendly name is emitted UNESCAPED into the JSON info message
 * ("friendlyName":"%s") and copied into the PB info message, so it must be
 * printable ASCII with no JSON-structural characters. Reject control chars
 * (< 0x20, 0x7F), non-ASCII (> 0x7E), and '"' / '\\'. Bounded scan: the
 * boot-seed path hands us a fixed NVM field that may be blank flash (0xFF,
 * no NUL) — a name with no terminator inside the field is invalid. */
bool daqifi_settings_FriendlyNameIsValid(const char* name) {
    if (name == NULL) {
        return false;
    }
    for (size_t i = 0; i < FRIENDLY_DEVICE_NAME_SIZE; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c == '\0') {
            return true;   // reached the terminator, all chars were safe
        }
        if (c < 0x20 || c > 0x7E || c == '"' || c == '\\') {
            return false;
        }
    }
    return false;          // no NUL within the field = malformed / invalid
}

void daqifi_settings_SetFriendlyName(const char* name) {
    /* Fail safe: a NULL or invalid name (blank-flash NVM, an older build,
     * or a corrupt blob reaching the boot-seed / NVM-load paths) leaves the
     * cache EMPTY rather than injecting corruption into the JSON/PB
     * encoders. The SCPI setter validates and rejects with an error first,
     * so a user command never silently lands in this clear-to-empty branch. */
    if (!daqifi_settings_FriendlyNameIsValid(name)) {
        gFriendlyDeviceName[0] = '\0';
        return;
    }
    strncpy(gFriendlyDeviceName, name, FRIENDLY_DEVICE_NAME_SIZE - 1);
    gFriendlyDeviceName[FRIENDLY_DEVICE_NAME_SIZE - 1] = '\0';
}

const char* daqifi_settings_GetFriendlyName(void) {
    return gFriendlyDeviceName;
}

void daqifi_settings_SeedFriendlyName(const char* name) {
    daqifi_settings_SetFriendlyName(name);
}

void daqifi_settings_MarkFactoryCalMissing(void) {
    gFactoryCalMissing = true;
}

void daqifi_settings_ClearFactoryCalMissing(void) {
    gFactoryCalMissing = false;
}

bool daqifi_settings_FactoryCalIsMissing(void) {
    return gFactoryCalMissing;
}

DaqifiSettingsType daqifi_settings_GetActiveCalBank(void) {
    return gActiveCalBank;
}

/* #908 (codex-r8 finding 2): the generation bracket, exposed so EVERY writer
 * of the live coefficients can mark its update -- while gCalGeneration itself
 * stays private to this file, so a caller cannot forget to pair them or set
 * the counter directly.
 *
 * ⚠ r7 argued CHANCALM/CHANCALB need not participate, because either value a
 * reader sees across ONE setter is a state the device genuinely had. That is
 * true of one setter and FALSE of two, which the audit caught. Concretely:
 * channel 0 identity, channel 5 non-identity; the scan reads channel 0 as
 * identity and moves on; one setter then CLEARS channel 5 while another SETS
 * channel 0; the scan reads channel 5 as identity and concludes ALL identity
 * -- a view that held at no instant, because channel 0 was non-identity by
 * then. An early-exiting scan plus two independent writers is enough. */
void daqifi_settings_BeginCalWrite(void) {
    gCalGeneration++;   /* -> ODD */
}

void daqifi_settings_EndCalWrite(void) {
    gCalGeneration++;   /* -> EVEN */
}

/* #908 (Qodo item 3): are the coefficients actually IN EFFECT identity?
 *
 * This is a question about the runtime array, not about NVM, and it is the one
 * the capability emitter's "source" key is named after. It cannot be inferred
 * from gActiveCalBank: a bank can load successfully and still contain identity
 * -- both bench boards on this station are exactly that, factory cal equal to
 * identity -- so "a load succeeded" and "calibration is in effect" are
 * different facts.
 *
 * CalM/CalB are DOUBLE (AInRuntimeConfig.h), so the literals are written 1.0 /
 * 0.0 rather than 1.0f / 0.0f -- the float forms promote to exactly the same
 * values, but reading a float literal against a double field invites a
 * precision question that does not exist here.
 *
 * Exact comparison is correct rather than an epsilon: the identity
 * defaults are ASSIGNED as 1.0/0.0, so a value that has never been through a
 * calibration is bit-identical to them. An epsilon would instead answer "is
 * the calibration nearly identity", which is a different and weaker claim --
 * a real coefficient legitimately close to unity would then be reported as no
 * calibration at all.
 *
 * An empty array reads as identity: no channel carries calibration. */
bool daqifi_settings_CalIsIdentity(const AInRuntimeArray* channelRuntimeConfig) {
    if (channelRuntimeConfig == NULL) {
        return true;
    }
    for (uint8_t x = 0; x < channelRuntimeConfig->Size; x++) {
        if (channelRuntimeConfig->Data[x].CalM != 1.0
                || channelRuntimeConfig->Data[x].CalB != 0.0) {
            return false;
        }
    }
    return true;
}

/* #908 (codex-r6 findings 2+3): the COMPOSITE answer, computed coherently.
 *
 * The emitter used to call daqifi_settings_CalIsIdentity() and then
 * daqifi_settings_GetActiveCalBank() as two separate reads. A higher-priority
 * CONF:ADC:LOADcal or LOADFcal landing between them -- or part-way through the
 * scan -- yields a pair the device never actually had: identity-ness measured
 * against one bank's coefficients, attributed to the other bank's name.
 * Introduced by this PR, because this PR is what made `source` depend on two
 * facts instead of one.
 *
 * No critical section, and deliberately so. The counter and the bank are both
 * aligned 32-bit objects, so no single read can tear on PIC32MZ. What is
 * missing is not atomicity but COHERENCE across the scan, and the fix detects
 * interference rather than excluding it -- a seqlock read:
 *
 *   require an EVEN generation (odd = a load is mid-flight)
 *   scan, read the bank
 *   require the generation UNCHANGED (else a load started or finished)
 *
 * ⚠ r6 compared the BANK before and after instead, which detects a load that
 * COMPLETED but is blind to one IN PROGRESS: the bank is published LAST, so a
 * reader pre-empting between the copy loop and the bank store reads the OLD
 * bank twice while scanning the NEW coefficients. Publishing the bank last is
 * r3's fix -- it stops the bank naming a load that did not land -- so the two
 * fixes are in tension and only a generation counter satisfies both.
 *
 * ⚠ CHANCALM/CHANCALB change coefficients WITHOUT changing the bank, and are
 * deliberately not detected here: either outcome they produce is a state the
 * device genuinely had, so reporting before-or-after is honest. It is only the
 * CROSS of two different banks that would be fiction.
 *
 * Bounded at three attempts rather than looping: this runs on a SCPI query
 * path, and an unbounded retry would turn a contended moment into a stall.
 * ⚠ On exhaustion it reports NONE, which is the UNDERSTATING direction -- a
 * client reading "none" treats the readings as uncalibrated and distrusts
 * absolute accuracy, which is safe. The alternative, naming a bank, is a false
 * provenance claim, and false provenance is the whole thing #908 exists to
 * prevent. Three consecutive races needs three load completions inside three
 * short scans; the fallback exists to be correct, not because it is expected.
 */
DaqifiCalSource daqifi_settings_GetCalSource(const AInRuntimeArray* channelRuntimeConfig) {
    for (uint8_t attempt = 0; attempt < 3u; attempt++) {
        /* ⛔ YIELD FIRST on every retry. This runs on a SCPI transport task at
         * priority 7; a bank load runs at priority 2. Spinning here would be a
         * priority inversion -- the writer we are waiting for cannot run,
         * every attempt observes the same odd generation, and a SUSPENDED load
         * would turn real calibration into a permanent `none`. One tick is
         * enough: the writer only needs to be scheduled, and the whole bound
         * is 2 ticks on a query path. */
        if (attempt > 0u) {
            vTaskDelay(1);
        }

        const uint32_t genBefore = gCalGeneration;
        if ((genBefore & 1u) != 0u) {
            continue;   /* a load is mid-flight; nothing read now is coherent */
        }

        const bool isIdentity = daqifi_settings_CalIsIdentity(channelRuntimeConfig);
        const DaqifiSettingsType bank = gActiveCalBank;

        if (gCalGeneration != genBefore) {
            continue;   /* a load started or finished under the scan */
        }

        if (isIdentity) {
            return DAQIFI_CAL_SOURCE_NONE;
        }
        return (bank == DaqifiSettings_UserAInCalParams)
                ? DAQIFI_CAL_SOURCE_USER : DAQIFI_CAL_SOURCE_FACTORY;
    }
    return DAQIFI_CAL_SOURCE_NONE;
}




bool daqifi_settings_LoadFromNvm(DaqifiSettingsType type, DaqifiSettings* settings) {

    
    DaqifiSettings tmpSettings;
    uint32_t address = 0;
    uint16_t dataSize = 0;
    memset(&tmpSettings,0,sizeof(DaqifiSettings));

    switch (type) {
        case DaqifiSettings_TopLevelSettings:
            address = TOP_LEVEL_SETTINGS_ADDR;
            dataSize = sizeof (TopLevelSettings);
            break;
        case DaqifiSettings_FactAInCalParams:
            address = FAINCAL_SETTINGS_ADDR;
            dataSize = sizeof(AInCalArray);
            break;
        case DaqifiSettings_UserAInCalParams:
            address = UAINCAL_SETTINGS_ADDR;
            dataSize = sizeof(AInCalArray);
            break;
        case DaqifiSettings_Wifi:
            address = WIFI_SETTINGS_ADDR;
            dataSize = sizeof (wifi_manager_settings_t);
            break;
        default:
            return false;
    }
    memcpy(&tmpSettings, (uint32_t *) PA_TO_KVA1(address), sizeof (DaqifiSettings));

    /* #306: validate integrity with CRC32 (Util/CRC32), severing the settings
     * load path from wolfSSL. Dual-validate for the migration window: if the
     * CRC32 doesn't match, fall back to the legacy 16-byte MD5 checksum so a
     * device upgrading from pre-#306 firmware still reads its stored settings
     * (the next save rewrites them with CRC32). The CRC32 lives in the first
     * 4 bytes of the layout-stable checksum field; the checksum covers only the
     * `settings` payload (dataSize bytes), exactly as the MD5 did. */
    uint32_t crc = CRC32_Compute(&(tmpSettings.settings), dataSize);
    uint32_t storedCrc;
    memcpy(&storedCrc, tmpSettings.checksum, sizeof(storedCrc));
    /* A valid CRC32 record is [crc(4) | zero(12)] (see the save path). Require
     * the zero padding as well, so a legacy MD5 (or corrupt) 16-byte checksum
     * can't alias a CRC32 match on just the first 4 bytes and wrongly skip the
     * MD5 fallback — the full checksum field must match the CRC32 record shape. */
    bool padZero = true;
    for (size_t i = sizeof(storedCrc); i < DAQIFI_SETTINGS_CHECKSUM_SIZE; i++) {
        if (tmpSettings.checksum[i] != 0u) {
            padZero = false;
            break;
        }
    }
    bool valid = padZero && (storedCrc == crc);

    if (!valid) {
        uint8_t hash[CRYPT_MD5_DIGEST_SIZE];
        CRYPT_MD5_CTX md5Ctx;
        CRYPT_MD5_Initialize(&md5Ctx);
        CRYPT_MD5_DataAdd(&md5Ctx, (const uint8_t*) &(tmpSettings.settings), dataSize);
        CRYPT_MD5_Finalize(&md5Ctx, hash);
        valid = (memcmp(hash, tmpSettings.checksum, CRYPT_MD5_DIGEST_SIZE) == 0);
    }

    if (!valid) {
        return false;
    }

    memcpy(settings, &tmpSettings, sizeof (DaqifiSettings));
    return true;
}

bool daqifi_settings_LoadFactoryDeafult(DaqifiSettingsType type, DaqifiSettings* settings) {
    settings->type = type;

    switch (settings->type) {
        case DaqifiSettings_TopLevelSettings:
        {
            TopLevelSettings* pTopLevelSettings = &(settings->settings.topLevelSettings);
            const tBoardConfig* pBoardConfig = (const tBoardConfig*)NqBoardConfig_Get();
            pTopLevelSettings->calVals = 0;
            pTopLevelSettings->voltagePrecision = pBoardConfig->DefaultVoltagePrecision;
            pTopLevelSettings->autoPowerOnUsb = false;  // #454: opt-in
            pTopLevelSettings->friendlyDeviceName[0] = '\0';  // #14: unset by default
            strcpy(pTopLevelSettings->boardHardwareRev, HARDWARE_REVISION);
            strcpy(pTopLevelSettings->boardFirmwareRev, FIRMWARE_REVISION);
            pTopLevelSettings->boardVariant = pBoardConfig->BoardVariant;
            break;
        }
        case DaqifiSettings_FactAInCalParams:
            return false;
            break;
        case DaqifiSettings_UserAInCalParams:
            return false;
            break;
        case DaqifiSettings_Wifi:
        {
            wifi_manager_settings_t* wifi = &(settings->settings.wifi);

            // Set isEnabled flag correctly 
            wifi->isEnabled = true;

            // Copy SSID - keep original behavior but fix the null termination bug
            strncpy(wifi->ssid, DEFAULT_WIFI_AP_SSID, strlen(DEFAULT_WIFI_AP_SSID) + 1);
            wifi->ssid[WDRV_WINC_MAX_SSID_LEN] = '\0';
            
            // Set security mode
            wifi->securityMode = DEFAULT_WIFI_AP_SECURITY_MODE;
            
            // Copy Hostname - keep original behavior
            strncpy(wifi->hostName, DEFAULT_NETWORK_HOST_NAME, strlen(DEFAULT_NETWORK_HOST_NAME) + 1);
            
            switch (wifi->securityMode) {
                case WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE:
                    strncpy((char*) wifi->passKey, DEFAULT_WIFI_WPA_PSK_PASSKEY, strlen(DEFAULT_WIFI_WPA_PSK_PASSKEY) + 1);
                    // Fix: Remove the incorrect line that was writing to ssid array
                    // wifi->ssid[WDRV_WINC_PSK_LEN] = '\0';  // This was the bug!
                    break;
                case WIFI_MANAGER_SECURITY_MODE_OPEN:
                default:
                    memset(wifi->passKey, 0, WDRV_WINC_PSK_LEN);
                    break;
            }


            //mac address will be populated after reading from ATWINC
            wifi->networkMode = DEFAULT_WIFI_NETWORK_MODE;
            wifi->powerSaveDisabled = false;  /* #29: power-save on by default */
            memset(&wifi->macAddr, 0, sizeof (WDRV_WINC_MAC_ADDR));
            wifi->ipAddr.Val = inet_addr(DEFAULT_NETWORK_IP_ADDRESS);
            wifi->ipMask.Val = inet_addr(DEFAULT_NETWORK_IP_MASK);
            wifi->gateway.Val = inet_addr(DEFAULT_NETWORK_GATEWAY_IP_ADDRESS);

            wifi->tcpPort = DEFAULT_TCP_PORT;
            wifi->ssidHidden = false;  // #45: AP SSID visible by default
            break;
        }
        default:
            return false;
            break;
    }

    settings->type = type;

    return true;
}

/* #909: see the contract on the declaration in daqifi_settings.h.
 *
 * Bounded string ops, unlike the two strcpy() stamps below: those copy a
 * compile-time literal into a field the compiler can see is big enough,
 * whereas this operates on a 16-byte array that came out of FLASH and may not
 * be NUL-terminated at all. memset-then-strncpy leaves the field fully
 * defined and terminated whatever the stored bytes were. */
void daqifi_settings_RefreshTopLevelRevisions(
        TopLevelSettings* pTopLevelSettings) {
    if (pTopLevelSettings == NULL) {
        return;
    }

    /* Unconditional: this touches RAM only, so there is nothing to save by
     * comparing first. An earlier revision returned "did it change" so the
     * caller could persist on that basis; the persist is gone (see the header),
     * and with it the only reason to branch. */
    memset(pTopLevelSettings->boardFirmwareRev, 0,
            sizeof (pTopLevelSettings->boardFirmwareRev));
    strncpy(pTopLevelSettings->boardFirmwareRev,
            FIRMWARE_REVISION,
            sizeof (pTopLevelSettings->boardFirmwareRev) - 1);

    memset(pTopLevelSettings->boardHardwareRev, 0,
            sizeof (pTopLevelSettings->boardHardwareRev));
    strncpy(pTopLevelSettings->boardHardwareRev,
            HARDWARE_REVISION,
            sizeof (pTopLevelSettings->boardHardwareRev) - 1);
}

bool daqifi_settings_SaveToNvm(DaqifiSettings* settings) {
    uint32_t address = 0;
    uint32_t dataSize = 0;

    switch (settings->type) {
        case DaqifiSettings_TopLevelSettings:
        {
            const tBoardConfig* pBoardConfig = (const tBoardConfig*)NqBoardConfig_Get();
            strcpy(settings->settings.topLevelSettings.boardHardwareRev, HARDWARE_REVISION);
            strcpy(settings->settings.topLevelSettings.boardFirmwareRev, FIRMWARE_REVISION);
            settings->settings.topLevelSettings.boardVariant = pBoardConfig->BoardVariant;
            // Capture current voltage precision from runtime config
            StreamingRuntimeConfig *pStreamCfg = BoardRunTimeConfig_Get(
                    BOARDRUNTIME_STREAMING_CONFIGURATION);
            if (pStreamCfg != NULL) {
                settings->settings.topLevelSettings.voltagePrecision =
                        pStreamCfg->VoltagePrecision;
            }
            // #454 autoPowerOnUsb is NOT captured here — the caller is
            // responsible for setting it explicitly before SaveToNvm.
            // Auto-capture would clobber the persisted value on every
            // unrelated save (e.g. CONF:VOLT:SAVE) if PowerData has been
            // toggled at runtime without an AUTOOn:SAVE.
            // #14 friendly name IS captured from the runtime cache here
            // (same auto-capture pattern as voltagePrecision), so any
            // TopLevelSettings save persists the current name.
            strncpy(settings->settings.topLevelSettings.friendlyDeviceName,
                    daqifi_settings_GetFriendlyName(),
                    FRIENDLY_DEVICE_NAME_SIZE - 1);
            settings->settings.topLevelSettings
                    .friendlyDeviceName[FRIENDLY_DEVICE_NAME_SIZE - 1] = '\0';
            address = TOP_LEVEL_SETTINGS_ADDR;
            dataSize = sizeof (TopLevelSettings);
            break;
        }
        case DaqifiSettings_FactAInCalParams:
            address = FAINCAL_SETTINGS_ADDR;
            dataSize = sizeof(AInCalArray);
            break;
        case DaqifiSettings_UserAInCalParams:
            address = UAINCAL_SETTINGS_ADDR;
            dataSize = sizeof(AInCalArray);
            break;
        case DaqifiSettings_Wifi:
            address = WIFI_SETTINGS_ADDR;            
            dataSize = sizeof (wifi_manager_settings_t);
            break;
        default:
            return false;
    }

    if (!daqifi_settings_ClearNvm(settings->type)) {
        return false;
    }

    /* #306: CRC32 integrity (Util/CRC32) — no wolfSSL on the save path. Store
     * the 4-byte CRC32 in the first bytes of the layout-stable checksum field
     * and zero the rest, so the field is deterministic and can't be mistaken
     * for a 16-byte MD5 by the load-path fallback. */
    uint32_t crc = CRC32_Compute(&(settings->settings), dataSize);
    memset(settings->checksum, 0, sizeof(settings->checksum));
    memcpy(settings->checksum, &crc, sizeof(crc));

    if (sizeof (DaqifiSettings)>sizeof (gTempFflashBuffer)) {
        return false;
    }
    memcpy(gTempFflashBuffer, settings, sizeof (DaqifiSettings));
    
    return nvm_WriteRowtoAddr(address, gTempFflashBuffer);
}

bool daqifi_settings_ClearNvm(DaqifiSettingsType type) {
    uint32_t address = 0;
    
    switch (type) {
        case DaqifiSettings_TopLevelSettings:
            address = TOP_LEVEL_SETTINGS_ADDR;       
            break;
        case DaqifiSettings_FactAInCalParams:
            address = FAINCAL_SETTINGS_ADDR;           
            break;
        case DaqifiSettings_UserAInCalParams:
            address = UAINCAL_SETTINGS_ADDR;           
            break;
        case DaqifiSettings_Wifi:
            address = WIFI_SETTINGS_ADDR;
            break;
        default:
            return false;
    }
    if (sizeof (DaqifiSettings) > NVM_FLASH_PAGESIZE) return false;

    return nvm_ErasePage(address);
}

bool daqifi_settings_LoadADCCalSettings(DaqifiSettingsType type, AInRuntimeArray* channelRuntimeConfig)
{
    bool status = true;
    uint8_t x = 0;
    AInCalArray* calArray;
    DaqifiSettings tmpSettings;
    memset(&tmpSettings,0,sizeof(DaqifiSettings));
    status = daqifi_settings_LoadFromNvm(type, &tmpSettings);
    if(!status) return status;
            
    switch(type)
    {
        case DaqifiSettings_FactAInCalParams:
        {
            calArray = &(tmpSettings.settings.factAInCalParams);
            break;
        }
        case DaqifiSettings_UserAInCalParams:
        {
            calArray = &(tmpSettings.settings.userAInCalParams);
            break;
        }
        default:
            status = false;
            break;
    }
    
    if(!status) return status;    
    
    /* ⚠ THE GENERATION BRACKET IS OUTSIDE, THE CRITICAL SECTION INSIDE.
     *
     * Two mechanisms, orthogonal, and the nesting is load-bearing in both
     * directions. The generation must span the WHOLE load and the bank
     * publish, or a reader can sample an even generation between channels and
     * see a half-loaded array still paired with the OLD bank -- a view that
     * held at no instant, which is what the counter exists to prevent. The
     * critical section must be the innermost thing around each 64-bit pair,
     * and must stay short: one section around the whole loop would hold
     * interrupts off for 48 channels to buy set-atomicity nothing needs.
     */
    /* -> ODD: from here until the bank is published, the coefficients and
     * gActiveCalBank disagree. A reader that samples an odd generation, or a
     * changed one, discards what it saw. */
    daqifi_settings_BeginCalWrite();

    /* #904: CalM and CalB are doubles, so each assignment is two 32-bit
     * stores on PIC32MZ and a writer preempted between them leaves a torn
     * coefficient that a concurrent reader reads back faithfully -- the
     * reader's own critical section cannot undo it. This is the LOADcal /
     * LOADFcal / USECal half of that fix; the SCPI chanCALM / chanCALB
     * setters carry the same section (SCPIADC.c), and the getters' sections
     * close the opposite direction.
     *
     * Per channel rather than around the whole loop: one channel's pair is
     * the unit every reader consumes (the getters read one index, and
     * MC12b_ConvertToVoltage reads one channel's CalM and CalB per
     * conversion), so it is what has to be atomic. A section spanning all 48
     * channels would additionally make the load atomic as a SET, which
     * nothing here needs, at the cost of a much longer interrupt-off window.
     *
     * Task context only: the SCPI callbacks, and the boot-time call from
     * app_SystemInit, which runs inside the priority-1 APP_FREERTOS_Tasks
     * task (tasks.c creates it with xTaskCreate) rather than before the
     * scheduler starts -- taskEXIT_CRITICAL would not re-enable interrupts
     * if it ran pre-scheduler. */
    for(x=0;x<channelRuntimeConfig->Size;x++)
    {
        taskENTER_CRITICAL();
        channelRuntimeConfig->Data[x].CalM = calArray->Data[x].CalM;
        channelRuntimeConfig->Data[x].CalB = calArray->Data[x].CalB;
        taskEXIT_CRITICAL();
    }

    /* #908 (Qodo item 3): record the bank only AFTER the copy loop, so the
     * tracked bank can never name a load that did not reach the runtime array.
     * Every early return above leaves it at its previous value, which is
     * correct: a failed load changes no coefficients, so it changes nothing
     * about which bank is in effect.
     *
     * (A note here used to say the copy loop had NO critical section, under
     * ruling 8 = A which gave the write half to #1048. #1048 has merged, the
     * section is three lines above, and the note is deleted rather than
     * amended around -- a revert must include the comments.) */
    gActiveCalBank = type;
    daqifi_settings_EndCalWrite();   /* -> EVEN: coefficients and bank agree */

    return status;
}
bool daqifi_settings_SaveADCCalSettings(DaqifiSettingsType type, AInRuntimeArray* channelRuntimeConfig)
{
    bool status = true;
    uint8_t x = 0;
    AInCalArray* calArray; 
    DaqifiSettings tmpSettings;
    memset(&tmpSettings,0,sizeof(DaqifiSettings));
    tmpSettings.type = type;
    
    switch(type)
    {
        case DaqifiSettings_FactAInCalParams:
        {
            calArray = &(tmpSettings.settings.factAInCalParams);
            break;
        }
        case DaqifiSettings_UserAInCalParams:
        {
            calArray = &(tmpSettings.settings.userAInCalParams);
            break;
        }
        default:
            status = false;
            break;
    }
    
    if(!status) return status;    
    
    /* #904 write side, the twin of the section in LoadADCCalSettings above:
     * this loop READS the live runtime pair, and a 64-bit read is two 32-bit
     * loads on PIC32MZ (docs/MCU_REFERENCE.md), so a writer landing between
     * them yields a torn coefficient that is then persisted by the
     * SaveToNvm() below -- a bad NVM image rather than a garbled reply.
     *
     * The streaming claim CalSaveCommon holds (SCPIADC.c) does NOT cover
     * this, and that file says so in both directions: it records that
     * "because CalM/CalB are 64-bit, a single channel's value can itself be
     * torn", and that "Boot's first-run factory save (app_freertos.c) calls
     * daqifi_settings_SaveADCCalSettings directly, never through these
     * callbacks, so it cannot meet the claim." A claim excludes other
     * claim-TAKERS; it does not make a 64-bit access atomic, and one caller
     * takes no claim at all. The claim and this section answer different
     * questions about the same two lines.
     *
     * Per channel, not around the whole loop, for the reason the load side
     * gives: one channel's pair is the unit every reader consumes, and a
     * section spanning all 48 would additionally make the copy atomic as a
     * SET -- which nothing here needs -- for a much longer interrupts-off
     * window.
     *
     * Task context only, same as the load side: the SCPI callbacks, and the
     * boot-time call from app_SystemInit, which runs inside the priority-1
     * APP_FREERTOS_Tasks task (app_freertos.c states this at its own
     * "we're inside priority-1 APP_FREERTOS_Tasks" note) rather than before
     * the scheduler starts -- taskEXIT_CRITICAL would not re-enable
     * interrupts if it ran pre-scheduler. */
    for(x=0;x<channelRuntimeConfig->Size;x++)
    {
        taskENTER_CRITICAL();
        calArray->Data[x].CalM = channelRuntimeConfig->Data[x].CalM;
        calArray->Data[x].CalB = channelRuntimeConfig->Data[x].CalB;
        taskEXIT_CRITICAL();
    }
    status = daqifi_settings_SaveToNvm(&tmpSettings);
    return status;
}
