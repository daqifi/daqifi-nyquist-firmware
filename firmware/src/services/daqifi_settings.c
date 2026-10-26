#include "daqifi_settings.h"
#include "wdrv_winc_client_api.h"
#include "HAL/NVM/nvm.h"
#include "version.h"
#include "state/board/BoardConfig.h"
#include "state/board/NQ3BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "Util/CRC32.h"   /* #306: settings integrity checksum (severs wolfSSL) */
#include "task.h"         /* #904 write half: taskENTER_CRITICAL for CalM/CalB */

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

/* #908 (codex-r5): STAGE -> PERSIST -> PUBLISH, which is why there is no
 * rollback here any more.
 *
 * Rounds r1-r4 each patched a rollback: add one, snapshot live values instead
 * of NVM, also restore the hidden bank, then protect its writes. None of them
 * asked whether there should BE a rollback. There should not: the only reason
 * one was needed is that the new coefficients went LIVE before the selection
 * was persisted, so a failed persist had something to undo.
 *
 * daqifi_settings_LoadADCCalSettings already had the seam -- it reads NVM into
 * a LOCAL and touches the live array only in a final copy loop. Splitting it
 * there gives:
 *
 *   Stage    reads NVM into this module's staging slot. CAN FAIL. Touches
 *            nothing live, so a failure needs no undo.
 *   Persist  the caller's own SaveToNvm. CAN FAIL -- and if it does, the
 *            caller simply returns: staging is discarded by never being
 *            published.
 *   Publish  copies staging into the live array under per-pair critical
 *            sections and records the bank. CANNOT FAIL.
 *
 * That deletes the snapshot, the restore, the snapshot slot, and r3's
 * hidden-state problem -- gActiveCalBank is now written at PUBLISH time, so it
 * can never name a bank that is not live. It also removes an arm that could
 * not be verified on hardware (nothing makes SaveToNvm fail), because the arm
 * no longer exists.
 *
 * ⚠ One residual, stated: between persist and publish, NVM names the new
 * selection while the live array still holds the old coefficients. It is
 * brief, it cannot fail, and it is self-consistent -- a reboot in that window
 * loads exactly what was persisted. Strictly better than publishing a
 * selection that may not persist.
 *
 * The staging slot REUSES the 768 B of .bss the snapshot occupied
 * (MAX_AIN_CHANNEL x CalM+CalB, both double) rather than adding a caller-side
 * buffer: LoadADCCalSettings already spends that much stack on its local
 * DaqifiSettings, so staging in the caller's frame would have grown the SCPI
 * task's peak instead. ONE slot, for the same reason the snapshot had one --
 * every SCPI writer of this state takes the same
 * Streaming_BeginConfigChange() claim, so these cannot nest. That claim
 * discipline is the precondition, and this comment is what a change to it
 * should trip over. */
static struct {
    double CalM;
    double CalB;
} gCalStaging[MAX_AIN_CHANNEL];
static uint8_t gCalStagingCount = 0;
static DaqifiSettingsType gCalStagingBank = DaqifiSettings_FactAInCalParams;
static bool gCalStagingValid = false;

/* The staging slot is sized by MAX_AIN_CHANNEL and filled from an AInCalArray,
 * which is sized by MAX_AIN_RUNTIME_CHANNEL. Equal today (48 each) and defined
 * in different headers, so a future change to one is exactly the kind that
 * lands silently. Build error instead. */
_Static_assert(MAX_AIN_RUNTIME_CHANNEL == MAX_AIN_CHANNEL,
        "#908: cal staging assumes AInCalArray and the AIn runtime array have "
        "the same channel capacity");

bool daqifi_settings_StageADCCalSettings(DaqifiSettingsType type) {
    DaqifiSettings tmpSettings;
    const AInCalArray* calArray;

    gCalStagingValid = false;
    gCalStagingCount = 0;

    memset(&tmpSettings, 0, sizeof (DaqifiSettings));
    if (!daqifi_settings_LoadFromNvm(type, &tmpSettings)) {
        return false;
    }

    switch (type) {
        case DaqifiSettings_FactAInCalParams:
            calArray = &(tmpSettings.settings.factAInCalParams);
            break;
        case DaqifiSettings_UserAInCalParams:
            calArray = &(tmpSettings.settings.userAInCalParams);
            break;
        default:
            return false;
    }

    /* Bounded by the SOURCE array's capacity, not the staging slot's. These
     * are two separate constants (AInCalArray is MAX_AIN_RUNTIME_CHANNEL,
     * AInConfig.h defines MAX_AIN_CHANNEL) that happen to be equal today, and
     * reading the destination's bound out of the source is how that stops
     * being true silently. The _Static_assert above fails the build if they
     * ever diverge; this bound keeps the copy correct even if someone removes
     * it. */
    for (uint8_t x = 0; x < (uint8_t) MAX_AIN_RUNTIME_CHANNEL; x++) {
        gCalStaging[x].CalM = calArray->Data[x].CalM;
        gCalStaging[x].CalB = calArray->Data[x].CalB;
    }
    gCalStagingCount = (uint8_t) MAX_AIN_RUNTIME_CHANNEL;
    gCalStagingBank = type;
    gCalStagingValid = true;
    return true;
}

bool daqifi_settings_PublishStagedCal(AInRuntimeArray* channelRuntimeConfig) {
    if (!gCalStagingValid || channelRuntimeConfig == NULL) {
        return false;
    }
    for (uint8_t x = 0; x < channelRuntimeConfig->Size
            && x < gCalStagingCount; x++) {
        /* #904's write half: the conversion readers take this exact section
         * (#1054), so an unprotected writer would hand them a torn pair. */
        taskENTER_CRITICAL();
        channelRuntimeConfig->Data[x].CalM = gCalStaging[x].CalM;
        channelRuntimeConfig->Data[x].CalB = gCalStaging[x].CalB;
        taskEXIT_CRITICAL();
    }
    /* Recorded at PUBLISH, so it cannot name a bank that is not live. */
    gActiveCalBank = gCalStagingBank;
    return true;
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

/* #908 (codex-r5): now just Stage + Publish, so the two callers that want the
 * combined behaviour keep their signature.
 *
 * Boot (app_freertos.c) and CalLoadCommon (CONF:ADC:LOADcal / LOADFcal) have
 * nothing to persist, so for them "load" IS stage-then-publish and a failure
 * genuinely means "nothing happened" -- the stage touches nothing live.
 * ADCUseCalSetClaimed is the one caller that must persist BETWEEN the two, so
 * it calls them separately. */
bool daqifi_settings_LoadADCCalSettings(DaqifiSettingsType type, AInRuntimeArray* channelRuntimeConfig)
{
    if (!daqifi_settings_StageADCCalSettings(type)) {
        return false;
    }
    return daqifi_settings_PublishStagedCal(channelRuntimeConfig);
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
    
    for(x=0;x<channelRuntimeConfig->Size;x++)
    {
        calArray->Data[x].CalM = channelRuntimeConfig->Data[x].CalM;
        calArray->Data[x].CalB = channelRuntimeConfig->Data[x].CalB;
    }
    status = daqifi_settings_SaveToNvm(&tmpSettings);
    return status;
}
