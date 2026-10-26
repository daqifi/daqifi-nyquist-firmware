/* ==========================================================================
 * test_1152_usecal_type_trust.c -- PR firmware#1152 (the #1148 follow-up)
 *
 * WHAT IS UNDER TEST
 *
 * ADCUseCalSetClaimed() (services/SCPI/SCPIADC.c), the body of
 * CONF:ADC:USECal, and the DaqifiSettings record it carries between
 * daqifi_settings_LoadFromNvm() and daqifi_settings_SaveToNvm()
 * (services/daqifi_settings.c).
 *
 * #1148 reordered that body to validate -> apply -> persist: the new
 * calibration coefficients are loaded into the runtime array BEFORE the
 * selector is written to NVM, so a reload that fails can no longer leave a
 * stale selector persisted. The three steps are
 *
 *   1. daqifi_settings_LoadFromNvm(DaqifiSettings_TopLevelSettings, &tmp)
 *   2. daqifi_settings_LoadADCCalSettings(calType, pRuntimeAInChannels)
 *        -- INSTALLS the new coefficients into the live runtime array
 *   3. tmp.settings.topLevelSettings.calVals = param1;
 *      daqifi_settings_SaveToNvm(&tmp)
 *
 * and the ordering's whole promise is that step 3 is the only step left that
 * can fail, and that when it does it fails for a reason nothing the caller
 * sends can provoke (a flash fault in nvm_ErasePage / nvm_WriteRowtoAddr).
 *
 * THE DEFECT #1152 FIXES -- THE INTEGRITY CHECKSUM DOES NOT COVER `type`
 *
 * DaqifiSettings is { checksum[16], type, settings }. Both validators in
 * daqifi_settings.c cover the PAYLOAD only:
 *
 *     uint32_t crc = CRC32_Compute(&(tmpSettings.settings), dataSize);   (load)
 *     uint32_t crc = CRC32_Compute(&(settings->settings), dataSize);     (save)
 *
 * `type` sits outside that span, and once the payload checksum validates
 * LoadFromNvm copies the WHOLE stored record out:
 *
 *     memcpy(settings, &tmpSettings, sizeof (DaqifiSettings));
 *
 * So a TOP_LEVEL record can carry a checksum-passing payload next to a `type`
 * field holding whatever bytes are physically at that flash address. Step 1
 * hands that value to step 3 unchanged, and daqifi_settings_SaveToNvm()
 * dispatches on it:
 *
 *     switch (settings->type) { ... default: return false; }
 *
 * Two things follow, and the second is worse than the first:
 *
 *   OUT-OF-ENUM type. The `default:` arm returns false BEFORE ClearNvm and
 *   therefore before any flash I/O at all. So the command answers -200 -- but
 *   step 2 has already run, so the NEW coefficients are live while the
 *   persisted selector, and with it USECal?, still names the OLD set. The
 *   caller is told the command failed and acquisition is silently converting
 *   against the other bank. On the board that motivated #1148 that is a
 *   reported voltage at twice the true value, with no ongoing error state.
 *   It is also not the flash-fault residual #1148 documents and accepts: this
 *   failure needs no faulty hardware and repeats on EVERY USECal call against
 *   that record.
 *
 *   IN-ENUM BUT WRONG type -- 1, 2 or 3 rather than 0. The switch then takes a
 *   REAL arm: it resolves a different address and a different dataSize, erases
 *   THAT page, and writes the TopLevelSettings record over it with a checksum
 *   computed across the other bank's span. The command returns OK. Nothing is
 *   reported. What is destroyed is the WiFi settings page (type 1) or a
 *   calibration bank (types 2 and 3) -- and type 2 is the FACTORY bank, i.e.
 *   `USECal 0` can overwrite the very coefficients it was asked to select,
 *   while page 0 keeps the old selector so USECal? never shows it either.
 *
 * THE FIX is one line, placed immediately after the step-1 load succeeds:
 *
 *     tmpTopLevelSettings.type = DaqifiSettings_TopLevelSettings;
 *
 * -- trust the type we ASKED for over the type NVM claims to hold. It is the
 * pattern SCPI_SaveAutoPowerOnUsb, SCPI_SaveDataPrecision and
 * SCPI_SaveDeviceName (SCPIInterface.c) already follow at the three other
 * sites that reuse a LoadFromNvm'd struct for a save; this call site was the
 * one that omitted it.
 *
 * HOW IT IS TESTED
 *
 * Neither SCPIADC.c nor daqifi_settings.c is host-compilable -- both pull in
 * Harmony's configuration.h / definitions.h, the WINC WiFi driver headers and
 * the whole board-config graph. So this follows test_1112, test_985, test_953
 * and test_943: the control-flow SHAPE is re-implemented here against an
 * in-memory NVM, and the PRE-FIX and POST-FIX shapes are run against the SAME
 * fixture and compared. One binary carries both, which is how "fails at the
 * PR's base commit and passes after it" is demonstrated without building two
 * revisions -- the same technique test_1112 uses for its reader/writer pair.
 *
 * WHAT IS NOT A MODEL: the checksum. firmware/src/Util/CRC32.c includes only
 * its own header (stdint.h + stddef.h), so this target LINKS THE REAL ONE.
 * Every checksum in this file is computed by the function the firmware runs,
 * over a payload-only span, which is what makes "the record validates and the
 * type is still wrong" a demonstration rather than an assumption.
 *
 * The Makefile guards pin the two claims a model cannot check for itself:
 * that the real SCPIADC.c still normalizes the type, between the load and the
 * save; and that daqifi_settings.c's two checksums still cover `.settings`
 * rather than the whole struct -- because if that ever changed, this file's
 * entire premise would be gone and the build should say so rather than pass a
 * stale model.
 *
 * CASES
 *
 *   1  the premise                    a record with an out-of-enum type still
 *                                     passes the REAL CRC32; flipping the type
 *                                     byte does not move the checksum, flipping
 *                                     a payload byte does
 *   2  the defect, out-of-enum        pre-fix: -200, no flash I/O at all, and
 *                                     the new bank ALREADY installed while the
 *                                     persisted selector is unchanged
 *   3  the fix, out-of-enum           post-fix: the save takes the TopLevel arm
 *                                     with type == TopLevelSettings, succeeds,
 *                                     persists the selector, and repairs the
 *                                     stored type byte on the way out
 *   4  the defect, in-enum but wrong  pre-fix: returns OK and erases + rewrites
 *                                     the WiFi page / the FACTORY cal bank;
 *                                     post-fix leaves both untouched
 *   5  every stored byte              all 256 single-byte values plus three
 *                                     wide ones: post-fix persists correctly
 *                                     for every one, pre-fix only for 0
 *   6  REGRESSION, healthy record     the two shapes are byte-for-byte
 *                                     identical when the stored type is right
 *   7  REGRESSION, #1148's gate       a bank that fails validation still
 *                                     refuses before it installs or persists,
 *                                     in both shapes
 *   8  the accepted residual          a flash fault still leaves coefficients
 *                                     live and the selector unpersisted, in
 *                                     both shapes -- #1152 does not claim it
 *   9  the paths that touch no NVM    raw mode (2) and out-of-range, unchanged
 *  10  the headline                   over the whole fixture matrix the two
 *                                     shapes differ in EXACTLY the cells where
 *                                     the stored type is wrong and the run
 *                                     reached the save
 *
 * FIDELITY -- what these models are and are not
 *
 * 1. `type` is modelled as uint32_t, not as the enum. The real field IS
 *    DaqifiSettingsType, but its bytes come out of flash, so the firmware can
 *    and does read a value outside the enumeration; storing such a value into
 *    a C enum object in a test would be the test's own undefined behaviour
 *    rather than the firmware's. uint32_t models the STORAGE, which is the
 *    thing the defect is about. The four enumerator values 0..3 and their
 *    order are pinned by a Makefile guard against daqifi_settings.h.
 * 2. The MD5 fallback in LoadFromNvm is not modelled. No case here constructs
 *    a legacy MD5 record; what matters to this file is only that BOTH
 *    validators span `.settings` and neither spans `type`, and that is what
 *    the Makefile guard checks -- including the MD5 line.
 * 3. SaveToNvm's TopLevelSettings arm also re-stamps the revision strings, the
 *    voltage precision and the friendly name before it writes. None of that
 *    touches the type dispatch, so it is absent here; the payload this file
 *    writes is otherwise a faithful TopLevelSettings image.
 * 4. The four NVM pages are modelled as an array indexed by the type value,
 *    which is exactly the real mapping: the enum's order (TopLevel, Wifi,
 *    FactAInCal, UserAInCal) and the address order (TOP_LEVEL, WIFI, FAINCAL,
 *    UAINCAL, daqifi_settings.h) agree, so index == type value. The absolute
 *    addresses are not modelled and nothing here depends on them.
 * 5. The record is zeroed before a fixture fills it, so the union's tail
 *    beyond the TopLevel payload is deterministic zeros rather than whatever
 *    flash held. That tail matters in exactly one place -- the wrong-arm save
 *    of case 4 checksums a span that runs into it -- and there the point is
 *    which page is written, not what the bytes are.
 * 6. No concurrency. The real body runs under Streaming_BeginConfigChange()'s
 *    claim (#847), which excludes the other SCPI transport and a session start
 *    across the whole callback, so there is no interleaving to model.
 * ========================================================================== */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "CRC32.h"              /* the REAL firmware checksum -- linked, not copied */
#include "test_framework.h"

/* ==========================================================================
 * The stored record -- DaqifiSettings, modelled
 * ========================================================================== */

/* DAQIFI_SETTINGS_CHECKSUM_SIZE (== CRYPT_MD5_DIGEST_SIZE). A valid CRC32
 * record is [crc(4) | zero(12)]; the load path requires the zero padding too,
 * so a legacy 16-byte MD5 cannot alias a CRC32 match on its first 4 bytes. */
#define MODEL_CHECKSUM_SIZE 16u

#define MODEL_CHANNELS 4u

/* The four DaqifiSettingsType enumerators, in declaration order
 * (daqifi_settings.h). Pinned by a Makefile guard. */
#define MODEL_TYPE_TOP_LEVEL  0u
#define MODEL_TYPE_WIFI       1u
#define MODEL_TYPE_FACT_CAL   2u
#define MODEL_TYPE_USER_CAL   3u
#define MODEL_TYPE_COUNT      4u

/* TopLevelSettings, field for field. */
typedef struct {
    uint8_t boardVariant;
    char    boardHardwareRev[16];
    char    boardFirmwareRev[16];
    uint8_t calVals;            /* a bool in the firmware; the byte it occupies here */
    uint8_t voltagePrecision;
    uint8_t autoPowerOnUsb;
    char    friendlyDeviceName[32];
} ModelTopLevelSettings;

/* AInCalArray, cut down to MODEL_CHANNELS entries. */
typedef struct {
    uint32_t Size;
    double   CalM[MODEL_CHANNELS];
    double   CalB[MODEL_CHANNELS];
} ModelCalArray;

/* The WiFi arm is modelled only by its SIZE. Nothing here reads it as a
 * structure; what matters is that its dataSize differs from the TopLevel one,
 * so a save down the wrong arm checksums a different span. */
#define MODEL_WIFI_SIZE   160u
#define MODEL_UNION_SIZE  256u

typedef union {
    ModelTopLevelSettings topLevelSettings;
    ModelCalArray         calParams;
    uint8_t               wifi[MODEL_WIFI_SIZE];
    uint8_t               raw[MODEL_UNION_SIZE];
} ModelSettingsImpl;

typedef struct {
    uint8_t           checksum[MODEL_CHECKSUM_SIZE];
    /* FIDELITY 1: the storage, not the enum -- the firmware reads this field
     * out of flash and it is not covered by the checksum above. */
    uint32_t          type;
    ModelSettingsImpl settings;
} ModelSettings;

_Static_assert(sizeof(ModelTopLevelSettings) <= MODEL_UNION_SIZE,
               "TopLevel payload must fit the modelled union");
_Static_assert(sizeof(ModelCalArray) <= MODEL_UNION_SIZE,
               "cal payload must fit the modelled union");
_Static_assert(MODEL_WIFI_SIZE > sizeof(ModelTopLevelSettings),
               "the WiFi span must differ from the TopLevel one, or case 4 "
               "would not exercise a different checksum span");

/* daqifi_settings.c's `dataSize` switch. */
static size_t model_data_size(uint32_t type)
{
    switch (type) {
    case MODEL_TYPE_TOP_LEVEL: return sizeof(ModelTopLevelSettings);
    case MODEL_TYPE_WIFI:      return MODEL_WIFI_SIZE;
    case MODEL_TYPE_FACT_CAL:  return sizeof(ModelCalArray);
    case MODEL_TYPE_USER_CAL:  return sizeof(ModelCalArray);
    default:                   return 0u;
    }
}

/* ==========================================================================
 * The NVM -- four pages, one per settings type (FIDELITY 4)
 * ========================================================================== */

typedef struct {
    uint8_t  page[MODEL_TYPE_COUNT][sizeof(ModelSettings)];
    unsigned erases[MODEL_TYPE_COUNT];   /* nvm_ErasePage calls, per page */
    unsigned writes[MODEL_TYPE_COUNT];   /* nvm_WriteRowtoAddr calls, per page */
    bool     eraseFails;                 /* injected flash fault */
    bool     writeFails;                 /* injected flash fault */

    /* ⛔ FIDELITY 5 and 6, added after the r2 audit found the model could not
     * represent either of its reproducers.
     *
     * eraseRefused: the page is WRITE-PROTECTED. nvm_ErasePage reads back one
     *   word, finds it is not 0xFFFFFFFF, and returns TRUE ANYWAY (nvm.c:45-54
     *   -- the failure branch's only action is a commented-out log). So the old
     *   record survives intact and the caller is told the erase succeeded. This
     *   is why `eraseFails` below models a path PRODUCTION CANNOT REACH: the
     *   HAL has no way to report an erase failure at all.
     *
     * writeLandsButReadbackFails: the flash write SUCCEEDS and
     *   nvm_WriteRowtoAddr still returns false, because its success test is a
     *   readback comparison (nvm.c:132) against the SHARED staging buffer
     *   gTempFflashBuffer (daqifi_settings.c:305). Any concurrent settings
     *   writer on another transport -- SCPI_LANSettingsSave takes no config
     *   claim -- can overwrite that buffer between the memcpy and the compare.
     *   NO FLASH FAULT IS INVOLVED; this is ordinary concurrent USB/WiFi
     *   configuration. */
    bool     eraseRefused;
    bool     writeLandsButReadbackFails;
    bool     commitForeignSelector;      /* a competing writer's record lands */
    /* FIDELITY 7: a concurrent LEGITIMATE writer changes an UNRELATED field of
     * the same record -- SYST:DEVICE:NAME:SAVE's friendlyDeviceName, or the
     * voltagePrecision that SaveToNvm auto-captures on every save -- while the
     * selector we intended is committed correctly. This is what the ACCEPTING
     * arm needs, and its absence is why r5's one-armed `refuses` test passed
     * over an over-broad predicate. */
    bool     commitUnrelatedFieldChange;
} ModelNvm;

static unsigned nvm_total(const unsigned *counts)
{
    unsigned i, n = 0;
    for (i = 0; i < MODEL_TYPE_COUNT; i++) {
        n += counts[i];
    }
    return n;
}

static void nvm_read_page(const ModelNvm *nvm, uint32_t type, ModelSettings *out)
{
    memcpy(out, nvm->page[type], sizeof(*out));
}

static void nvm_store_page(ModelNvm *nvm, uint32_t type, const ModelSettings *rec)
{
    memcpy(nvm->page[type], rec, sizeof(*rec));
}

/* An erased page. 0xFF because that is what erased NOR flash reads as, and
 * because nvm_ErasePage's own success test is `readByte == 0xFFFFFFFF`. A
 * blanked page cannot validate: CRC32 over all-0xFF will not match the
 * all-0xFF checksum field, and the pad-must-be-zero test fails too. */
static void nvm_blank_page(ModelNvm *nvm, uint32_t type)
{
    memset(nvm->page[type], 0xFF, sizeof(ModelSettings));
}

/* ==========================================================================
 * daqifi_settings_LoadFromNvm(), modelled
 *
 *   switch (type) -> address + dataSize, default: return false
 *   memcpy the WHOLE stored record into a local
 *   CRC32_Compute over the PAYLOAD only, compared against checksum[0..3]
 *     with checksum[4..15] required zero
 *   on valid: memcpy the WHOLE local into the caller's struct  <-- `type` rides
 * ========================================================================== */
static bool model_LoadFromNvm(const ModelNvm *nvm, uint32_t type,
                              ModelSettings *out)
{
    ModelSettings tmp;
    size_t   dataSize = model_data_size(type);
    uint32_t crc, storedCrc;
    bool     padZero = true;
    size_t   i;

    if (type >= MODEL_TYPE_COUNT) {
        return false;                       /* the switch's `default:` arm */
    }

    memset(&tmp, 0, sizeof(tmp));
    nvm_read_page(nvm, type, &tmp);

    /* THE REAL FIRMWARE CRC32, over `.settings` and nothing else. */
    crc = CRC32_Compute(&tmp.settings, dataSize);
    memcpy(&storedCrc, tmp.checksum, sizeof(storedCrc));
    for (i = sizeof(storedCrc); i < MODEL_CHECKSUM_SIZE; i++) {
        if (tmp.checksum[i] != 0u) {
            padZero = false;
            break;
        }
    }
    if (!padZero || storedCrc != crc) {
        return false;                       /* (the MD5 fallback: FIDELITY 2) */
    }

    /* The whole record, `type` included. This is the carrier. */
    memcpy(out, &tmp, sizeof(tmp));
    return true;
}

/* ==========================================================================
 * daqifi_settings_SaveToNvm(), modelled
 * ========================================================================== */

typedef enum {
    SAVE_OK = 0,
    SAVE_REJECTED_UNKNOWN_TYPE,   /* `default: return false` -- before any I/O */
    SAVE_ERASE_FAILED,
    SAVE_WRITE_FAILED
} ModelSaveOutcome;

static ModelSaveOutcome model_SaveToNvm(ModelNvm *nvm, ModelSettings *rec)
{
    size_t   dataSize = model_data_size(rec->type);
    uint32_t type = rec->type;
    uint32_t crc;

    /* The dispatch. Everything below it is flash I/O, so this arm is reached
     * with the medium untouched -- which is why the out-of-enum failure is
     * NOT the flash-fault residual #1148 documents. */
    if (type >= MODEL_TYPE_COUNT) {
        return SAVE_REJECTED_UNKNOWN_TYPE;
    }

    /* daqifi_settings_ClearNvm(settings->type) -> nvm_ErasePage(address) */
    nvm->erases[type]++;
    if (nvm->eraseFails) {
        /* ⚠ MODELS A PATH PRODUCTION CANNOT REACH. nvm_ErasePage returns true
         * on both branches (nvm.c:45-54), so the settings layer is never told
         * an erase failed. Kept because the pre-r2 tests assert through it and
         * removing it would silently drop their coverage of the earlier
         * shapes -- but a real erase failure presents as `eraseRefused`
         * below, NOT as this. */
        return SAVE_ERASE_FAILED;
    }

    /* ERASE INVALIDATION -- the fidelity the r2 audit found missing. A real
     * erase that is not refused destroys the committed record BEFORE the
     * replacement is written, so a write that then fails leaves NO valid
     * record at this address. The model used to leave the old record in place
     * here, which made every save-failure case look recoverable. */
    if (!nvm->eraseRefused) {
        nvm_blank_page(nvm, type);
    }

    /* [crc(4) | zero(12)], computed over the arm's OWN dataSize. */
    crc = CRC32_Compute(&rec->settings, dataSize);
    memset(rec->checksum, 0, sizeof(rec->checksum));
    memcpy(rec->checksum, &crc, sizeof(crc));

    nvm->writes[type]++;
    if (nvm->writeFails) {
        return SAVE_WRITE_FAILED;
    }
    if (nvm->writeLandsButReadbackFails) {
        /* The write LANDED. Only nvm_WriteRowtoAddr's readback comparison
         * failed, because the shared staging buffer was overwritten under it.
         * So the medium now holds the NEW record and the caller is told the
         * save FAILED -- the worst of both, and the r2 reproducer. */
        if (nvm->commitUnrelatedFieldChange) {
            /* A LEGITIMATE concurrent writer: our selector lands correctly, but
             * its save stamped a different voltagePrecision (SaveToNvm
             * auto-captures that field on every TopLevelSettings save). The
             * record is VALID and its calVals is exactly what we intended. */
            ModelSettings other = *rec;
            uint32_t c;
            other.settings.topLevelSettings.voltagePrecision = 0x5A;
            c = CRC32_Compute(&other.settings, model_data_size(other.type));
            memset(other.checksum, 0, sizeof(other.checksum));
            memcpy(other.checksum, &c, sizeof(c));
            nvm_store_page(nvm, type, &other);
        } else if (nvm->commitForeignSelector) {
            /* A competing writer's record lands instead of ours. Still VALID,
             * so only a payload comparison can tell them apart. */
            ModelSettings other = *rec;
            uint32_t c;
            other.settings.topLevelSettings.calVals = 0u;
            c = CRC32_Compute(&other.settings, model_data_size(other.type));
            memset(other.checksum, 0, sizeof(other.checksum));
            memcpy(other.checksum, &c, sizeof(c));
            nvm_store_page(nvm, type, &other);
        } else {
            nvm_store_page(nvm, type, rec);
        }
        return SAVE_WRITE_FAILED;
    }
    nvm_store_page(nvm, type, rec);
    return SAVE_OK;
}

/* ==========================================================================
 * The live runtime array, and daqifi_settings_LoadADCCalSettings()
 *
 * The install is an observable side effect: `installs` is what lets a case
 * assert the exact silent-wrong-data shape -- the command failed AND the new
 * coefficients are already live -- rather than merely "it returned an error".
 * ========================================================================== */

#define MODEL_NO_BANK 0xFFFFFFFFu

typedef struct {
    unsigned installs;
    uint32_t installedBank;
    double   CalM[MODEL_CHANNELS];
    double   CalB[MODEL_CHANNELS];
} ModelRuntimeArray;

static void runtime_init(ModelRuntimeArray *rt, double m, double b)
{
    unsigned i;
    memset(rt, 0, sizeof(*rt));
    rt->installedBank = MODEL_NO_BANK;
    for (i = 0; i < MODEL_CHANNELS; i++) {
        rt->CalM[i] = m;
        rt->CalB[i] = b;
    }
}

static bool model_LoadADCCalSettings(const ModelNvm *nvm, uint32_t calType,
                                     ModelRuntimeArray *rt)
{
    ModelSettings tmp;
    unsigned i;

    /* #1148's gate: LoadFromNvm validates and returns false WELL BEFORE the
     * per-channel copy loop, so a failed reload provably leaves rt untouched. */
    memset(&tmp, 0, sizeof(tmp));
    if (!model_LoadFromNvm(nvm, calType, &tmp)) {
        return false;
    }
    if (calType != MODEL_TYPE_FACT_CAL && calType != MODEL_TYPE_USER_CAL) {
        return false;
    }

    for (i = 0; i < MODEL_CHANNELS; i++) {
        rt->CalM[i] = tmp.settings.calParams.CalM[i];
        rt->CalB[i] = tmp.settings.calParams.CalB[i];
    }
    rt->installs++;
    rt->installedBank = calType;
    return true;
}

/* daqifi_settings_ValidateADCCalBank(), modelled.
 *
 * Deliberately the SAME two checks as model_LoadADCCalSettings above, in the
 * same order, and NOTHING ELSE -- no ModelRuntimeArray parameter at all, so it
 * is not merely that this function does not touch `installs`: it CANNOT. A
 * version that took rt and simply chose not to write it would leave the
 * r2 flash-fault assertions below resting on discipline rather than on the
 * signature. */
/* daqifi_settings_TopLevelMatchesCommitted(), modelled.
 *
 * Reads the medium independently -- never the staging buffer -- and requires a
 * valid record, the expected stored type (uncovered by the checksum, so checked
 * explicitly), and payload equality. Its record is local and expires on return. */
static bool model_TopLevelMatchesCommitted(const ModelNvm *nvm,
                                           const ModelSettings *expected)
{
    ModelSettings committed;

    if (expected == NULL) {
        return false;
    }
    memset(&committed, 0, sizeof(committed));
    if (!model_LoadFromNvm(nvm, MODEL_TYPE_TOP_LEVEL, &committed)) {
        return false;
    }
    if (committed.type != MODEL_TYPE_TOP_LEVEL) {
        return false;
    }
    /* The SELECTOR FIELD alone -- see the production comment. A whole-payload
     * comparison here was the r5 defect. */
    return (committed.settings.topLevelSettings.calVals ==
            expected->settings.topLevelSettings.calVals);
}

static bool model_ValidateADCCalBank(const ModelNvm *nvm, uint32_t calType)
{
    ModelSettings tmp;

    memset(&tmp, 0, sizeof(tmp));
    if (!model_LoadFromNvm(nvm, calType, &tmp)) {
        return false;
    }
    return (calType == MODEL_TYPE_FACT_CAL || calType == MODEL_TYPE_USER_CAL);
}

/* ==========================================================================
 * ADCUseCalSetClaimed(), in its three shapes
 * ========================================================================== */

typedef enum { RES_OK = 0, RES_ERR } ModelScpiResult;

typedef enum {
    USECAL_PREFIX = 0,   /* dc967c9e1: the loaded type is reused for the save */
    USECAL_POSTFIX,      /* #1152 r1: the type is normalized right after the load,
                          * but the bank is still INSTALLED before the save. The
                          * r1 adversarial audit found that a save failure then
                          * leaves the coefficients live and the selector stale,
                          * with no indication -- see USECAL_R2. Kept as the
                          * historical shape, not as the current firmware. */
    USECAL_R2            /* #1152 r2 (HISTORICAL -- r5 below is what production
                          * runs): validate -> save -> install.
                          *
                          * ⛔ DO NOT RESTORE THE CLAIM THIS COMMENT USED TO
                          * MAKE -- that "no failure can leave the runtime array
                          * and NVM disagreeing". The r2 audit disproved it. The
                          * reorder changes WHICH SIDE goes stale; it does not
                          * remove the divergence:
                          *
                          *   r1 (install first): save fails -> new coefficients
                          *     live, OLD selector stored.
                          *   r2 (save first): the flash write LANDS but its
                          *     readback comparison fails -> NEW selector
                          *     stored, OLD coefficients live, and USECal?
                          *     reads the selector straight from NVM.
                          *
                          * r2's failure needs no hardware fault at all: the
                          * readback compares against the SHARED staging buffer
                          * gTempFflashBuffer, which any concurrent settings
                          * writer on another transport can overwrite. See
                          * r2_erase_invalidation_* below, which model it.
                          *
                          * What r2 does buy: the common case -- a bank that
                          * fails validation -- touches nothing at all. */,
    USECAL_R5,           /* #1152 r5 (HISTORICAL -- r8 below is what production
                          * runs): r2, plus -- when SaveToNvm REPORTS failure --
                          * an independent verification of the committed record
                          * before giving up. Closes the r2 reproducer where the
                          * write LANDED and only the readback comparison failed.
                          *
                          * ⚠ NOT transaction safety, and this comment must
                          * not grow into claiming it: another writer can
                          * overwrite after the check, a save prepared
                          * elsewhere can land later, the install still
                          * re-reads the bank, and a HUNG save never reaches
                          * the check. One interleaving, closed.
                          *
                          * Kept as the historical shape round 8 changed TWO
                          * things about: see USECAL_R8. */
    USECAL_R8            /* #1152 round 8 (CURRENT), operator ruling 9=A, on
                          * top of r5's save-verification:
                          *
                          *   1. RawOutputMode (modelled here as rawOutputMode,
                          *      threaded through via rawModeIn/run.rawOutputMode
                          *      since the model has no persistent
                          *      StreamingRuntimeConfig to mutate across calls)
                          *      is cleared IMMEDIATELY after the param1==2
                          *      branch, independent of whether anything below
                          *      succeeds. Every earlier shape clears it only
                          *      on the final success return, which left a
                          *      board with no populated cal bank unable to
                          *      ever leave raw mode (USECal 2 -> USECal 0/1
                          *      against a blank bank -> -200, still raw).
                          *   2. On the TopLevelMatchesCommitted MISMATCH
                          *      branch (genuine divergence, not r5's
                          *      recovered-concurrent-write case), round 7's
                          *      audit found returning WITHOUT installing left
                          *      the PREVIOUS bank live while the stored
                          *      selector was already destroyed by the erase --
                          *      strictly worse than installing, since the
                          *      bank was already PROVEN loadable by
                          *      ValidateADCCalBank. r8 installs the intended
                          *      bank on this branch before still returning
                          *      RES_ERR. */
} UseCalShape;

typedef struct {
    ModelScpiResult  result;
    bool             loadOk;          /* step 1 succeeded */
    bool             installAttempted; /* step 2 was reached */
    bool             saveAttempted;   /* step 3 was reached */
    uint32_t         typeHandedToSave;/* what SaveToNvm's switch actually saw */
    ModelSaveOutcome saveOutcome;
    bool             recoveredAfterReportedFailure; /* r5 verified and continued */
    bool             rawOutputMode;
} UseCalRun;

static UseCalRun model_usecal_set(UseCalShape shape, int param1,
                                  ModelNvm *nvm, ModelRuntimeArray *rt,
                                  bool rawModeIn)
{
    UseCalRun run;
    ModelSettings tmp;
    uint32_t calType;

    memset(&run, 0, sizeof(run));
    run.typeHandedToSave = MODEL_NO_BANK;
    run.saveOutcome = SAVE_OK;

    /* #620: reject an out-of-range value before ANY state mutation --
     * raw mode included, in every shape, so this reflects whatever it was
     * BEFORE this call rather than a hardcoded false. */
    if (param1 < 0 || param1 > 2) {
        run.rawOutputMode = rawModeIn;
        run.result = RES_ERR;
        return run;
    }

    /* #158/#270: value 2 is runtime-only raw output; it never reaches NVM. */
    if (param1 == 2) {
        run.rawOutputMode = true;
        run.result = RES_OK;
        return run;
    }

    /* Round 8, ruling 9=A, modelled: the real firmware's RawOutputMode is
     * persistent FreeRTOS-task state (StreamingRuntimeConfig, held across
     * calls via BoardRunTimeConfig_Get()), which this model does not carry
     * internally -- callers thread it themselves via `rawModeIn` (the state
     * BEFORE this call) and read it back via `run.rawOutputMode` (the state
     * AFTER). r8 clears it HERE, immediately and unconditionally, before any
     * step below that can fail. Every earlier shape does not touch it here
     * at all -- it stays whatever `rawModeIn` was until (if) the shared
     * success return at the bottom flips it, so a failure on those shapes
     * leaves it exactly as it was handed in. That is the behaviour the 9=A
     * hardware repro exposed: a board with no populated bank could call
     * USECal 0/1 forever and never leave raw mode, because every one of
     * those calls fails before reaching the bottom. */
    if (shape == USECAL_R8) {
        run.rawOutputMode = false;
    } else {
        run.rawOutputMode = rawModeIn;
    }

    /* --- step 1 --------------------------------------------------------- */
    memset(&tmp, 0, sizeof(tmp));
    if (!model_LoadFromNvm(nvm, MODEL_TYPE_TOP_LEVEL, &tmp)) {
        run.result = RES_ERR;
        return run;
    }
    run.loadOk = true;

    /* --- #1152's normalization: present in every shape except the
     * pre-fix one it exists to replace -- production normalizes
     * unconditionally for every in-range, non-raw param1. */
    if (shape != USECAL_PREFIX) {
        tmp.type = MODEL_TYPE_TOP_LEVEL;
    }

    switch (param1) {
    case 0:  calType = MODEL_TYPE_FACT_CAL; break;
    case 1:  calType = MODEL_TYPE_USER_CAL; break;
    default: run.result = RES_ERR; return run;   /* unreachable, kept as in situ */
    }

    if (shape == USECAL_R2 || shape == USECAL_R5 || shape == USECAL_R8) {
        /* --- r2 step 2: VALIDATE without installing ---------------------- */
        /* daqifi_settings_ValidateADCCalBank: the same LoadFromNvm check
         * LoadADCCalSettings performs internally, without the copy loop that
         * cannot fail. `installs` must NOT move here -- that is the whole
         * property, and the flash-fault cases below assert it. */
        if (!model_ValidateADCCalBank(nvm, calType)) {
            run.result = RES_ERR;
            return run;
        }

        /* --- r2 step 3: persist the selector ---------------------------- */
        tmp.settings.topLevelSettings.calVals = (uint8_t)param1;
        run.saveAttempted = true;
        run.typeHandedToSave = tmp.type;
        run.saveOutcome = model_SaveToNvm(nvm, &tmp);
        if (run.saveOutcome != SAVE_OK) {
            /* r5 added: a REPORTED failure is not proof the write failed.
             * Read the medium independently and continue only on an exact
             * match. r2 has no such check and gives up unconditionally --
             * `recovers` is false for it, which short-circuits the `||`
             * below so model_TopLevelMatchesCommitted is never even called,
             * matching production having no such call in r2. */
            bool recovers = (shape == USECAL_R5 || shape == USECAL_R8);
            if (!recovers || !model_TopLevelMatchesCommitted(nvm, &tmp)) {
                /* round 8 (Change 2): on a GENUINE mismatch -- as opposed to
                 * r5/r8's recovered-concurrent-write case just above --
                 * round 7's audit found returning here WITHOUT installing
                 * leaves the PREVIOUS bank live while the stored selector is
                 * already destroyed by the erase that already ran either
                 * way. model_ValidateADCCalBank has already PROVEN calType
                 * loads, so r8 installs it here before still failing. r2 and
                 * r5 do NOT -- that is the pre-round-8 shape the
                 * `..._refuses_a_valid_record_that_is_not_the_intended_one`
                 * and `r2_*` tests below pin with `rt.installs == 0` on this
                 * exact branch, and must keep passing unchanged. */
                if (shape == USECAL_R8) {
                    run.installAttempted = true;
                    (void)model_LoadADCCalSettings(nvm, calType, rt);
                }
                run.result = RES_ERR;
                return run;
            }
            run.recoveredAfterReportedFailure = true;
        }

        /* --- r2 step 4: install LAST ------------------------------------ */
        run.installAttempted = true;
        if (!model_LoadADCCalSettings(nvm, calType, rt)) {
            run.result = RES_ERR;
            return run;
        }
    } else {
        /* --- r1/pre-fix step 2: validate the bank by INSTALLING it ------- */
        run.installAttempted = true;
        if (!model_LoadADCCalSettings(nvm, calType, rt)) {
            run.result = RES_ERR;
            return run;
        }

        /* --- r1/pre-fix step 3: only now persist the selector ------------ */
        tmp.settings.topLevelSettings.calVals = (uint8_t)param1;
        run.saveAttempted = true;
        run.typeHandedToSave = tmp.type;
        run.saveOutcome = model_SaveToNvm(nvm, &tmp);
        if (run.saveOutcome != SAVE_OK) {
            run.result = RES_ERR;
            return run;
        }
    }

    /* Historical shapes clear here, on success, for the first time in this
     * call. r8 already cleared it at the top (unconditionally) -- this is a
     * no-op for r8, kept unconditional rather than `if (shape != USECAL_R8)`
     * because both branches land on the same value and a conditional here
     * would claim a distinction that does not exist. */
    run.rawOutputMode = false;
    run.result = RES_OK;
    return run;
}

/* ==========================================================================
 * Fixtures
 * ========================================================================== */

/* Write a TOP_LEVEL page whose PAYLOAD is valid and correctly checksummed with
 * the real CRC32, and whose stored `type` field holds `storedType` verbatim --
 * the case the checksum does not protect. */
static void fixture_top_level(ModelNvm *nvm, uint32_t storedType, uint8_t calVals)
{
    ModelSettings rec;
    uint32_t crc;

    memset(&rec, 0, sizeof(rec));
    rec.settings.topLevelSettings.boardVariant = 1u;
    strcpy(rec.settings.topLevelSettings.boardHardwareRev, "1.0.0");
    strcpy(rec.settings.topLevelSettings.boardFirmwareRev, "3.8.0");
    rec.settings.topLevelSettings.calVals = calVals;
    rec.settings.topLevelSettings.voltagePrecision = 4u;
    rec.settings.topLevelSettings.autoPowerOnUsb = 0u;
    strcpy(rec.settings.topLevelSettings.friendlyDeviceName, "bench-nq1");

    crc = CRC32_Compute(&rec.settings, sizeof(ModelTopLevelSettings));
    memcpy(rec.checksum, &crc, sizeof(crc));   /* [4..15] left zero by the memset */

    rec.type = storedType;
    nvm_store_page(nvm, MODEL_TYPE_TOP_LEVEL, &rec);
}

/* Write a calibration bank. `valid` false corrupts the checksum, which is what
 * an unwritten User bank looks like to LoadFromNvm. */
static void fixture_cal_bank(ModelNvm *nvm, uint32_t type, bool valid,
                             double m, double b)
{
    ModelSettings rec;
    uint32_t crc;
    unsigned i;

    memset(&rec, 0, sizeof(rec));
    rec.settings.calParams.Size = MODEL_CHANNELS;
    for (i = 0; i < MODEL_CHANNELS; i++) {
        rec.settings.calParams.CalM[i] = m;
        rec.settings.calParams.CalB[i] = b;
    }
    crc = CRC32_Compute(&rec.settings, sizeof(ModelCalArray));
    if (!valid) {
        crc ^= 0xA5A5A5A5u;
    }
    memcpy(rec.checksum, &crc, sizeof(crc));
    rec.type = type;
    nvm_store_page(nvm, type, &rec);
}

#define FACT_M 1.0
#define FACT_B 0.0
#define USER_M 2.0
#define USER_B 0.5

/* A board as it ships: a good TopLevel page selecting FACTORY, both cal banks
 * written, no injected faults. `storedType` is the one variable. */
static void fixture_board(ModelNvm *nvm, ModelRuntimeArray *rt,
                          uint32_t storedType, bool userBankValid)
{
    memset(nvm, 0, sizeof(*nvm));
    fixture_top_level(nvm, storedType, 0u);
    fixture_cal_bank(nvm, MODEL_TYPE_FACT_CAL, true, FACT_M, FACT_B);
    fixture_cal_bank(nvm, MODEL_TYPE_USER_CAL, userBankValid, USER_M, USER_B);
    runtime_init(rt, FACT_M, FACT_B);
}

/* What USECal? would answer: the persisted selector, read back the way
 * SCPI_ADCUseCalGet reads it. Returns -1 if the page no longer validates. */
/* The committed voltagePrecision, so the ACCEPTING arm can prove the unrelated
 * field really did differ. Without this the arm could pass vacuously. */
static int persisted_precision(const ModelNvm *nvm)
{
    ModelSettings rec;
    memset(&rec, 0, sizeof(rec));
    if (!model_LoadFromNvm(nvm, MODEL_TYPE_TOP_LEVEL, &rec)) {
        return -1;
    }
    return (int)rec.settings.topLevelSettings.voltagePrecision;
}

static int persisted_selector(const ModelNvm *nvm)
{
    ModelSettings rec;
    memset(&rec, 0, sizeof(rec));
    if (!model_LoadFromNvm(nvm, MODEL_TYPE_TOP_LEVEL, &rec)) {
        return -1;
    }
    return (int)rec.settings.topLevelSettings.calVals;
}

/* ==========================================================================
 * CASE 1 -- THE PREMISE. The record validates and the type is still wrong.
 *
 * Computed with the REAL CRC32_Compute, so this is not an assumption about
 * what the firmware's checksum covers -- it is that function's answer.
 * ========================================================================== */
TEST(the_payload_checksum_does_not_cover_the_type_field)
{
    ModelNvm nvm;
    ModelSettings loaded, probe;
    uint32_t crcBefore, crcAfterTypeFlip, crcAfterPayloadFlip;

    memset(&nvm, 0, sizeof(nvm));
    fixture_top_level(&nvm, 99u, 0u);       /* out of enum, payload untouched */

    memset(&loaded, 0, sizeof(loaded));
    ASSERT_TRUE(model_LoadFromNvm(&nvm, MODEL_TYPE_TOP_LEVEL, &loaded));
    /* The load SUCCEEDED -- and handed the caller the corrupted type. */
    ASSERT_EQ(loaded.type, 99u);
    ASSERT_EQ(loaded.settings.topLevelSettings.voltagePrecision, 4);
    ASSERT_EQ(loaded.settings.topLevelSettings.boardVariant, 1);

    /* The checksum's span, demonstrated directly: moving `type` does not move
     * the CRC; moving one payload byte does. */
    nvm_read_page(&nvm, MODEL_TYPE_TOP_LEVEL, &probe);
    crcBefore = CRC32_Compute(&probe.settings, sizeof(ModelTopLevelSettings));

    probe.type = 0xDEADBEEFu;
    crcAfterTypeFlip = CRC32_Compute(&probe.settings, sizeof(ModelTopLevelSettings));
    ASSERT_EQ(crcAfterTypeFlip, crcBefore);

    probe.settings.topLevelSettings.voltagePrecision ^= 0x01u;
    crcAfterPayloadFlip = CRC32_Compute(&probe.settings,
                                        sizeof(ModelTopLevelSettings));
    ASSERT_TRUE(crcAfterPayloadFlip != crcBefore);

    /* And a corrupted PAYLOAD is caught, so the load path is not simply
     * accepting everything handed to it. offsetof, not 16+4: the union is
     * 8-aligned (it holds doubles), so there is an alignment hole after
     * `type` -- unchecked by the checksum like `type` itself, and a byte
     * flipped there would have proved nothing. */
    nvm.page[MODEL_TYPE_TOP_LEVEL][offsetof(ModelSettings, settings)] ^= 0xFFu;
    ASSERT_FALSE(model_LoadFromNvm(&nvm, MODEL_TYPE_TOP_LEVEL, &loaded));
}

/* ==========================================================================
 * CASE 2 -- THE DEFECT, out-of-enum stored type.
 *
 * The exact silent-wrong-data shape, asserted as a shape and not merely as an
 * error return: -200 back to the caller, no flash touched, the NEW bank
 * already live in the runtime array, and USECal? still reporting the OLD
 * selector.
 * ========================================================================== */
TEST(pre_fix_installs_the_new_bank_then_cannot_persist_the_selector)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;

    fixture_board(&nvm, &rt, 99u, true);
    ASSERT_EQ(persisted_selector(&nvm), 0);      /* factory, before the command */

    run = model_usecal_set(USECAL_PREFIX, 1, &nvm, &rt, false);   /* CONF:ADC:USECal 1 */

    /* The caller is told it failed. */
    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_TRUE(run.loadOk);
    ASSERT_TRUE(run.saveAttempted);
    ASSERT_EQ(run.typeHandedToSave, 99u);
    ASSERT_EQ(run.saveOutcome, SAVE_REJECTED_UNKNOWN_TYPE);

    /* It is NOT the documented flash-I/O residual: nothing was erased and
     * nothing was written, on any page. The save died in the dispatch. */
    ASSERT_EQ(nvm_total(nvm.erases), 0);
    ASSERT_EQ(nvm_total(nvm.writes), 0);

    /* And yet the USER coefficients are already live. */
    ASSERT_EQ(rt.installs, 1);
    ASSERT_EQ(rt.installedBank, MODEL_TYPE_USER_CAL);
    ASSERT_TRUE(rt.CalM[0] == USER_M);
    ASSERT_TRUE(rt.CalB[0] == USER_B);

    /* While USECal? goes on answering 0. That disagreement IS the defect:
     * every conversion uses bank 1 and every reader is told bank 0. */
    ASSERT_EQ(persisted_selector(&nvm), 0);
}

/* ==========================================================================
 * CASE 3 -- THE FIX, same record.
 * ========================================================================== */
TEST(post_fix_normalizes_the_type_and_the_same_record_persists)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;
    ModelSettings after;

    fixture_board(&nvm, &rt, 99u, true);
    ASSERT_EQ(persisted_selector(&nvm), 0);

    run = model_usecal_set(USECAL_POSTFIX, 1, &nvm, &rt, false);

    ASSERT_EQ(run.result, RES_OK);
    /* The switch saw the type we ASKED for, not the one NVM held. */
    ASSERT_EQ(run.typeHandedToSave, MODEL_TYPE_TOP_LEVEL);
    ASSERT_EQ(run.saveOutcome, SAVE_OK);

    /* One erase and one write, both on the TopLevel page, none anywhere else. */
    ASSERT_EQ(nvm.erases[MODEL_TYPE_TOP_LEVEL], 1);
    ASSERT_EQ(nvm.writes[MODEL_TYPE_TOP_LEVEL], 1);
    ASSERT_EQ(nvm_total(nvm.erases), 1);
    ASSERT_EQ(nvm_total(nvm.writes), 1);

    /* The coefficients are live AND the selector agrees with them. */
    ASSERT_EQ(rt.installs, 1);
    ASSERT_EQ(rt.installedBank, MODEL_TYPE_USER_CAL);
    ASSERT_EQ(persisted_selector(&nvm), 1);

    /* The stored type byte is repaired on the way out, so the next boot's
     * load -- and every later save -- starts from a clean record. */
    nvm_read_page(&nvm, MODEL_TYPE_TOP_LEVEL, &after);
    ASSERT_EQ(after.type, MODEL_TYPE_TOP_LEVEL);
}

/* ==========================================================================
 * CASE 4 -- THE DEFECT, in-enum but WRONG stored type. Worse than case 2:
 * the command returns OK, nothing is reported anywhere, and a DIFFERENT NVM
 * bank is erased and overwritten with the TopLevelSettings image.
 * ========================================================================== */
TEST(pre_fix_in_enum_wrong_type_overwrites_the_wifi_page_and_reports_success)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;

    fixture_board(&nvm, &rt, MODEL_TYPE_WIFI, true);

    run = model_usecal_set(USECAL_PREFIX, 1, &nvm, &rt, false);

    /* No error. Nothing to find in SYST:LOG?. */
    ASSERT_EQ(run.result, RES_OK);
    ASSERT_EQ(run.typeHandedToSave, MODEL_TYPE_WIFI);
    ASSERT_EQ(run.saveOutcome, SAVE_OK);

    /* The WiFi page was erased and rewritten; the TopLevel page never was. */
    ASSERT_EQ(nvm.erases[MODEL_TYPE_WIFI], 1);
    ASSERT_EQ(nvm.writes[MODEL_TYPE_WIFI], 1);
    ASSERT_EQ(nvm.erases[MODEL_TYPE_TOP_LEVEL], 0);
    ASSERT_EQ(nvm.writes[MODEL_TYPE_TOP_LEVEL], 0);

    /* So the selector never persisted either -- USECal? still answers 0 while
     * the USER coefficients are live, case 2's disagreement reached by a path
     * that reports success. */
    ASSERT_EQ(persisted_selector(&nvm), 0);
    ASSERT_EQ(rt.installedBank, MODEL_TYPE_USER_CAL);

    /* Post-fix, from the same fixture: the WiFi page is not touched at all. */
    fixture_board(&nvm, &rt, MODEL_TYPE_WIFI, true);
    run = model_usecal_set(USECAL_POSTFIX, 1, &nvm, &rt, false);
    ASSERT_EQ(run.result, RES_OK);
    ASSERT_EQ(nvm.erases[MODEL_TYPE_WIFI], 0);
    ASSERT_EQ(nvm.writes[MODEL_TYPE_WIFI], 0);
    ASSERT_EQ(nvm.writes[MODEL_TYPE_TOP_LEVEL], 1);
    ASSERT_EQ(persisted_selector(&nvm), 1);
}

/* The same shape aimed at the FACTORY calibration bank, which is the sharpest
 * version of it: `USECal 0` selects the factory coefficients, installs them,
 * and then erases the page they live on -- reporting success. */
TEST(pre_fix_stored_type_two_overwrites_the_factory_cal_bank_it_just_selected)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;
    ModelSettings bank;

    fixture_board(&nvm, &rt, MODEL_TYPE_FACT_CAL, true);
    runtime_init(&rt, USER_M, USER_B);          /* currently on the user bank */

    /* The factory bank is readable before the command. */
    ASSERT_TRUE(model_LoadFromNvm(&nvm, MODEL_TYPE_FACT_CAL, &bank));

    run = model_usecal_set(USECAL_PREFIX, 0, &nvm, &rt, false);   /* CONF:ADC:USECal 0 */

    ASSERT_EQ(run.result, RES_OK);              /* reports success */
    ASSERT_EQ(run.typeHandedToSave, MODEL_TYPE_FACT_CAL);
    ASSERT_EQ(rt.installedBank, MODEL_TYPE_FACT_CAL);
    ASSERT_TRUE(rt.CalM[0] == FACT_M);

    /* ... having erased and rewritten the factory bank with a TopLevel image.
     * The page still checksums -- SaveToNvm recomputed it for the FACT span --
     * so nothing downstream reports a problem either; the coefficients it now
     * yields are simply TopLevelSettings bytes reinterpreted as doubles. */
    ASSERT_EQ(nvm.erases[MODEL_TYPE_FACT_CAL], 1);
    ASSERT_EQ(nvm.writes[MODEL_TYPE_FACT_CAL], 1);
    ASSERT_TRUE(model_LoadFromNvm(&nvm, MODEL_TYPE_FACT_CAL, &bank));
    ASSERT_TRUE(bank.settings.calParams.CalM[0] != FACT_M);
    ASSERT_EQ(persisted_selector(&nvm), 0);     /* and the selector never moved */

    /* Post-fix: the factory bank is intact and still yields FACT_M. */
    fixture_board(&nvm, &rt, MODEL_TYPE_FACT_CAL, true);
    runtime_init(&rt, USER_M, USER_B);
    run = model_usecal_set(USECAL_POSTFIX, 0, &nvm, &rt, false);
    ASSERT_EQ(run.result, RES_OK);
    ASSERT_EQ(nvm.erases[MODEL_TYPE_FACT_CAL], 0);
    ASSERT_TRUE(model_LoadFromNvm(&nvm, MODEL_TYPE_FACT_CAL, &bank));
    ASSERT_TRUE(bank.settings.calParams.CalM[0] == FACT_M);
}

/* ==========================================================================
 * CASE 5 -- every stored byte value, plus three wide ones. The post-fix shape
 * is correct for all of them; the pre-fix shape is correct for exactly one.
 * ========================================================================== */
TEST(post_fix_persists_correctly_for_every_stored_type_value)
{
    static const uint32_t wide[] = { 256u, 100000u, 0xFFFFFFFFu };
    unsigned v;
    unsigned preFixCorrect = 0, preFixWrongPage = 0, preFixRejected = 0;
    unsigned postFixCorrect = 0;
    unsigned total = 0;
    size_t   w;

    for (v = 0; v < 256u + (sizeof(wide) / sizeof(wide[0])); v++) {
        uint32_t storedType = (v < 256u) ? v : wide[v - 256u];
        ModelNvm nvm;
        ModelRuntimeArray rt;
        UseCalRun run;

        total++;

        /* ---- pre-fix ---- */
        fixture_board(&nvm, &rt, storedType, true);
        run = model_usecal_set(USECAL_PREFIX, 1, &nvm, &rt, false);
        /* The install always happens -- it precedes the save in both shapes. */
        ASSERT_EQ(rt.installs, 1);
        if (storedType == MODEL_TYPE_TOP_LEVEL) {
            if (run.result == RES_OK && persisted_selector(&nvm) == 1) {
                preFixCorrect++;
            }
        } else if (storedType < MODEL_TYPE_COUNT) {
            /* a real arm, the wrong page, and no error */
            if (run.result == RES_OK
                && nvm.writes[storedType] == 1u
                && nvm.writes[MODEL_TYPE_TOP_LEVEL] == 0u
                && persisted_selector(&nvm) == 0) {
                preFixWrongPage++;
            }
        } else {
            /* the `default:` arm, before any I/O, with the bank already live */
            if (run.result == RES_ERR
                && run.saveOutcome == SAVE_REJECTED_UNKNOWN_TYPE
                && nvm_total(nvm.erases) == 0u
                && nvm_total(nvm.writes) == 0u
                && persisted_selector(&nvm) == 0
                && rt.installedBank == MODEL_TYPE_USER_CAL) {
                preFixRejected++;
            }
        }

        /* ---- post-fix ---- */
        fixture_board(&nvm, &rt, storedType, true);
        run = model_usecal_set(USECAL_POSTFIX, 1, &nvm, &rt, false);
        if (run.result == RES_OK
            && run.typeHandedToSave == MODEL_TYPE_TOP_LEVEL
            && run.saveOutcome == SAVE_OK
            && nvm.writes[MODEL_TYPE_TOP_LEVEL] == 1u
            && nvm_total(nvm.writes) == 1u
            && nvm_total(nvm.erases) == 1u
            && persisted_selector(&nvm) == 1
            && rt.installedBank == MODEL_TYPE_USER_CAL) {
            postFixCorrect++;
        }
    }

    w = sizeof(wide) / sizeof(wide[0]);
    ASSERT_EQ(total, 256u + w);
    /* Exactly one stored value made the pre-fix shape behave. */
    ASSERT_EQ(preFixCorrect, 1);
    /* Three in-enum values took a real arm on the wrong page, silently. */
    ASSERT_EQ(preFixWrongPage, MODEL_TYPE_COUNT - 1u);
    /* Everything else was the -200-with-the-bank-already-live shape. */
    ASSERT_EQ(preFixRejected, 256u + w - MODEL_TYPE_COUNT);
    /* And every single one of them is correct after the fix. */
    ASSERT_EQ(postFixCorrect, total);
}

/* ==========================================================================
 * CASE 6 -- REGRESSION. On a healthy record the fix must change nothing: the
 * normalization is then an assignment of the value already there. This is
 * what stops it from being a behaviour change on every fielded board, and it
 * is what a mutation that normalizes to the WRONG constant fails.
 * ========================================================================== */
TEST(a_healthy_record_is_byte_for_byte_unchanged_by_the_fix)
{
    int param1;

    for (param1 = 0; param1 <= 1; param1++) {
        ModelNvm nvmPre, nvmPost;
        ModelRuntimeArray rtPre, rtPost;
        UseCalRun runPre, runPost;

        fixture_board(&nvmPre,  &rtPre,  MODEL_TYPE_TOP_LEVEL, true);
        fixture_board(&nvmPost, &rtPost, MODEL_TYPE_TOP_LEVEL, true);

        runPre  = model_usecal_set(USECAL_PREFIX,  param1, &nvmPre,  &rtPre, false);
        runPost = model_usecal_set(USECAL_POSTFIX, param1, &nvmPost, &rtPost, false);

        ASSERT_EQ(runPre.result, RES_OK);
        ASSERT_EQ(runPost.result, runPre.result);
        ASSERT_EQ(runPost.saveOutcome, runPre.saveOutcome);
        ASSERT_EQ(runPost.typeHandedToSave, runPre.typeHandedToSave);
        ASSERT_EQ(runPre.typeHandedToSave, MODEL_TYPE_TOP_LEVEL);

        /* The NVM, every page, byte for byte -- including the counters. */
        ASSERT_BYTES(nvmPost.page, nvmPre.page, sizeof(nvmPre.page));
        ASSERT_BYTES(nvmPost.erases, nvmPre.erases, sizeof(nvmPre.erases));
        ASSERT_BYTES(nvmPost.writes, nvmPre.writes, sizeof(nvmPre.writes));

        /* And the live array. */
        ASSERT_EQ(rtPost.installs, rtPre.installs);
        ASSERT_EQ(rtPost.installedBank, rtPre.installedBank);
        ASSERT_BYTES(rtPost.CalM, rtPre.CalM, sizeof(rtPre.CalM));
        ASSERT_BYTES(rtPost.CalB, rtPre.CalB, sizeof(rtPre.CalB));

        ASSERT_EQ(persisted_selector(&nvmPost), param1);
    }
}

/* ==========================================================================
 * CASE 7 -- REGRESSION on #1148 itself. The reorder's own gate must still
 * hold: a bank that fails validation refuses BEFORE anything is installed and
 * BEFORE anything is persisted. #1152 sits between the load and that gate, so
 * this is the property most at risk from it.
 * ========================================================================== */
TEST(a_bank_that_fails_validation_still_refuses_before_install_or_persist)
{
    UseCalShape shapes[2] = { USECAL_PREFIX, USECAL_POSTFIX };
    unsigned s;

    for (s = 0; s < 2u; s++) {
        ModelNvm nvm;
        ModelRuntimeArray rt;
        UseCalRun run;

        /* A board whose USER bank was never written -- #1148's own reproducer,
         * which any such board hits on every `USECal 1`. */
        fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, false);

        run = model_usecal_set(shapes[s], 1, &nvm, &rt, false);

        ASSERT_EQ(run.result, RES_ERR);
        ASSERT_TRUE(run.loadOk);
        ASSERT_TRUE(run.installAttempted);
        /* Nothing installed ... */
        ASSERT_EQ(rt.installs, 0);
        ASSERT_EQ(rt.installedBank, MODEL_NO_BANK);
        ASSERT_TRUE(rt.CalM[0] == FACT_M);
        /* ... and the save never reached. */
        ASSERT_FALSE(run.saveAttempted);
        ASSERT_EQ(nvm_total(nvm.erases), 0);
        ASSERT_EQ(nvm_total(nvm.writes), 0);
        /* So USECal? keeps answering the set the next boot will really load. */
        ASSERT_EQ(persisted_selector(&nvm), 0);
    }
}

/* ==========================================================================
 * CASE 8 -- THE ACCEPTED RESIDUAL, stated as a test so that it is on the
 * record as unchanged rather than silently assumed. A genuine flash fault in
 * the erase or the write still leaves the coefficients live and the selector
 * unpersisted, identically in both shapes. #1152 removes a NON-hardware way
 * into this state; it does not claim the hardware one.
 * ========================================================================== */
/* r2: the same two faults, and the coefficients must NOT be live.
 *
 * This is the property the r1 adversarial audit asked for, asserted as a shape
 * rather than as an error return: both shapes above answer RES_ERR too, so the
 * return value does not distinguish them. `installs` does.
 *
 * ⛔ Note what the test below this one asserts, and why it is NOT a regression:
 * in the two historical shapes a write fault leaves installs == 1. That was the
 * defect. Keeping both assertions side by side is the point -- the pair is what
 * shows the fix changed the state and not just the wording. */
TEST(r2_a_flash_fault_leaves_the_coefficients_untouched)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;

    /* write fault: the erase lands, the row write fails its readback */
    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    nvm.writeFails = true;
    run = model_usecal_set(USECAL_R2, 1, &nvm, &rt, false);
    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_EQ(run.saveOutcome, SAVE_WRITE_FAILED);
    ASSERT_EQ(rt.installs, 0);              /* NOT live -- the whole fix */
    ASSERT_EQ(rt.installedBank, MODEL_NO_BANK);
    ASSERT_EQ(run.installAttempted, false); /* install never even reached */
    ASSERT_EQ(nvm.writes[MODEL_TYPE_TOP_LEVEL], 1);  /* the attempt happened */

    /* erase fault: fails earlier, same requirement */
    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    nvm.eraseFails = true;
    run = model_usecal_set(USECAL_R2, 1, &nvm, &rt, false);
    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_EQ(run.saveOutcome, SAVE_ERASE_FAILED);
    ASSERT_EQ(rt.installs, 0);
    ASSERT_EQ(rt.installedBank, MODEL_NO_BANK);
    ASSERT_EQ(nvm.writes[MODEL_TYPE_TOP_LEVEL], 0);
    ASSERT_EQ(persisted_selector(&nvm), 0);

    /* And the selector did not move either, so NVM and runtime still agree --
     * which is the property neither historical shape can state. */
    ASSERT_EQ(persisted_selector(&nvm), 0);
    ASSERT_EQ(rt.installedBank, MODEL_NO_BANK);
}

/* ==========================================================================
 * THE r2 AUDIT'S TWO REPRODUCERS. Both were invisible to this model before
 * erase invalidation and the readback-interleaving case were added, which is
 * why the suite went green over the r2 reorder.
 *
 * ⛔ THESE ASSERT THE CURRENT, DEFECTIVE BEHAVIOUR, deliberately. They are the
 * open hazard pinned so it cannot go quiet again -- not a specification of
 * what the firmware should do. When a real fix lands (failure-safe replacement
 * in the settings layer, an NVM HAL that can report an erase failure, and a
 * claim discipline covering every settings writer) these assertions must FLIP,
 * and this comment is how the next reader knows that is the intended outcome
 * rather than a regression.
 * ========================================================================== */
/* ⭐⭐ THE ACCEPTING ARM -- the half r5 was missing.
 *
 * r5 shipped `r5_refuses_a_valid_record_that_is_not_the_intended_one`, which was
 * mutation-proven and still missed an over-broad predicate, because ONE ARM
 * PROVES SENSITIVITY OR SPECIFICITY, NEVER BOTH: a `refuses` test passes MORE
 * convincingly the broader the comparison gets. This is its twin. It varies only
 * what the predicate must IGNORE, and it FAILS against r5's whole-payload
 * comparison and PASSES against the narrowed one. */
TEST(r6_accepts_the_intended_selector_despite_a_concurrent_unrelated_write)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;

    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    nvm.writeLandsButReadbackFails = true;   /* the readback is corrupted */
    nvm.commitUnrelatedFieldChange = true;   /* ...by a legitimate other writer */
    run = model_usecal_set(USECAL_R5, 1, &nvm, &rt, false);

    /* NOT VACUOUS: the unrelated field really did change under us. Assert that
     * FIRST -- if it had not, everything below would pass for the wrong reason. */
    ASSERT_EQ(persisted_precision(&nvm), 0x5A);

    /* And the selector we intended IS committed, so recovery must ACCEPT. */
    ASSERT_EQ(persisted_selector(&nvm), 1);
    ASSERT_EQ(run.result, RES_OK);
    ASSERT_EQ(run.recoveredAfterReportedFailure, true);

    /* ...and therefore install, so the two sides agree. */
    ASSERT_EQ(rt.installs, 1);
    ASSERT_EQ(rt.installedBank, MODEL_TYPE_USER_CAL);
    ASSERT_EQ(rt.CalM[0], USER_M);
}

TEST(r5_the_concurrent_write_divergence_is_closed)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;

    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    nvm.writeLandsButReadbackFails = true;   /* no flash fault; concurrency */
    run = model_usecal_set(USECAL_R5, 1, &nvm, &rt, false);

    /* The command SUCCEEDS, because the intended record is in fact committed. */
    ASSERT_EQ(run.result, RES_OK);
    ASSERT_EQ(run.saveOutcome, SAVE_WRITE_FAILED);   /* it still REPORTED failure */
    ASSERT_EQ(run.recoveredAfterReportedFailure, true);

    /* ⭐ AND THE TWO SIDES AGREE -- this is the r2 divergence, closed. */
    ASSERT_EQ(persisted_selector(&nvm), 1);
    ASSERT_EQ(rt.installs, 1);
    ASSERT_EQ(rt.installedBank, MODEL_TYPE_USER_CAL);
    ASSERT_EQ(rt.CalM[0], USER_M);
}

TEST(r5_does_not_rescue_a_genuine_write_failure)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;

    /* The write really did not land, so the erase has destroyed the record and
     * there is nothing to verify against. The recovery must NOT fire. */
    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    nvm.writeFails = true;
    run = model_usecal_set(USECAL_R5, 1, &nvm, &rt, false);

    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_EQ(run.recoveredAfterReportedFailure, false);
    ASSERT_EQ(rt.installs, 0);
    ASSERT_EQ(persisted_selector(&nvm), -1);

    /* And the write-protected case: the OLD record survived, so it does not
     * match the intended one either. Recovery must not fire on a stale match. */
    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    nvm.eraseRefused = true;
    nvm.writeFails   = true;
    run = model_usecal_set(USECAL_R5, 1, &nvm, &rt, false);

    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_EQ(run.recoveredAfterReportedFailure, false);
    ASSERT_EQ(rt.installs, 0);
    ASSERT_EQ(persisted_selector(&nvm), 0);   /* still the OLD selector */
}

/* Proves the payload equality actually DISCRIMINATES, rather than passing
 * because the record merely validates. A concurrent writer commits a VALID
 * top-level record carrying a DIFFERENT selector; recovery must refuse it. */
TEST(r5_refuses_a_valid_record_that_is_not_the_intended_one)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;

    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    nvm.writeLandsButReadbackFails = true;
    nvm.commitForeignSelector      = true;   /* someone else's value lands */
    run = model_usecal_set(USECAL_R5, 1, &nvm, &rt, false);

    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_EQ(run.recoveredAfterReportedFailure, false);
    ASSERT_EQ(rt.installs, 0);
    ASSERT_EQ(persisted_selector(&nvm), 0);   /* the foreign value, not 1 */
}

TEST(r2_erase_invalidation_destroys_the_stored_selector)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;

    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    nvm.writeFails = true;            /* real erase, then the row write fails */
    run = model_usecal_set(USECAL_R2, 1, &nvm, &rt, false);

    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_EQ(run.saveOutcome, SAVE_WRITE_FAILED);
    ASSERT_EQ(nvm.erases[MODEL_TYPE_TOP_LEVEL], 1);
    ASSERT_EQ(nvm.writes[MODEL_TYPE_TOP_LEVEL], 1);

    /* The runtime array is untouched -- that part of r2 does hold. */
    ASSERT_EQ(rt.installs, 0);
    ASSERT_EQ(rt.installedBank, MODEL_NO_BANK);

    /* ⛔ But the COMMITTED RECORD IS GONE. The erase landed before the write
     * was attempted, so there is no valid TOP_LEVEL record any more: USECal?
     * answers -200, and the next boot takes the factory-default branch and
     * persists calVals = 0 -- which need not match what was live. The old
     * comment claiming "selection unchanged" on this path was false. */
    ASSERT_EQ(persisted_selector(&nvm), -1);
}

TEST(r2_a_concurrent_settings_write_diverges_with_no_flash_fault)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;

    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    /* No flash fault. A concurrent writer on another transport overwrites the
     * shared staging buffer, so the readback comparison fails over a write
     * that LANDED. */
    nvm.writeLandsButReadbackFails = true;
    run = model_usecal_set(USECAL_R2, 1, &nvm, &rt, false);

    ASSERT_EQ(run.result, RES_ERR);          /* the caller is told it failed */
    ASSERT_EQ(run.saveOutcome, SAVE_WRITE_FAILED);

    /* ⛔ THE DIVERGENCE, AND NOTHING WAS INJECTED BUT CONCURRENCY.
     * NVM holds the NEW selector; the live coefficients are the OLD bank. */
    ASSERT_EQ(persisted_selector(&nvm), 1);              /* USECal? says USER */
    ASSERT_EQ(rt.installs, 0);
    ASSERT_EQ(rt.CalM[0], FACT_M);                       /* acquisition: FACTORY */

    /* Stated as the comparison that matters: the selector and the live bank
     * disagree, which is exactly what the r1 fix claimed to make unreachable. */
    ASSERT_EQ(persisted_selector(&nvm), 1);
    ASSERT_EQ(rt.installedBank, MODEL_NO_BANK);
}

TEST(r2_a_write_protected_page_is_the_one_consistent_failure)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;

    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    nvm.eraseRefused = true;   /* nvm_ErasePage reports success regardless */
    nvm.writeFails   = true;
    run = model_usecal_set(USECAL_R2, 1, &nvm, &rt, false);

    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_EQ(run.saveOutcome, SAVE_WRITE_FAILED);

    /* The old record survived the refused erase, and r2 never installed, so
     * BOTH sides are the previous state: the only save failure under r2 that
     * leaves the device self-consistent. It is also the case the r1 audit used
     * to disprove r1 -- under r1 this same scenario diverged, because r1 had
     * already installed. */
    ASSERT_EQ(persisted_selector(&nvm), 0);
    ASSERT_EQ(rt.installs, 0);
    ASSERT_EQ(rt.CalM[0], FACT_M);
}

TEST(a_flash_fault_leaves_the_coefficients_live_in_the_two_historical_shapes)
{
    UseCalShape shapes[2] = { USECAL_PREFIX, USECAL_POSTFIX };
    unsigned s;

    for (s = 0; s < 2u; s++) {
        ModelNvm nvm;
        ModelRuntimeArray rt;
        UseCalRun run;

        /* write fault */
        fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
        nvm.writeFails = true;
        run = model_usecal_set(shapes[s], 1, &nvm, &rt, false);
        ASSERT_EQ(run.result, RES_ERR);
        ASSERT_EQ(run.saveOutcome, SAVE_WRITE_FAILED);
        ASSERT_EQ(rt.installs, 1);                    /* coefficients ARE live */
        ASSERT_EQ(rt.installedBank, MODEL_TYPE_USER_CAL);
        ASSERT_EQ(nvm.writes[MODEL_TYPE_TOP_LEVEL], 1);  /* the attempt happened */
        /* The erase DID land, so the selector is what the next boot will use;
         * that the page here still reads the old value is a property of this
         * model's page store, not a claim about the medium. */
        ASSERT_EQ(nvm.erases[MODEL_TYPE_TOP_LEVEL], 1);

        /* erase fault -- fails earlier, same outward shape */
        fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
        nvm.eraseFails = true;
        run = model_usecal_set(shapes[s], 1, &nvm, &rt, false);
        ASSERT_EQ(run.result, RES_ERR);
        ASSERT_EQ(run.saveOutcome, SAVE_ERASE_FAILED);
        ASSERT_EQ(rt.installs, 1);
        ASSERT_EQ(nvm.writes[MODEL_TYPE_TOP_LEVEL], 0);
        ASSERT_EQ(persisted_selector(&nvm), 0);
    }
}

/* ==========================================================================
 * CASE 9 -- the two early returns. Raw mode and an out-of-range value never
 * reach the load, so the stored type is irrelevant to them in both shapes.
 * ========================================================================== */
TEST(raw_mode_and_out_of_range_reach_no_nvm_in_either_shape)
{
    UseCalShape shapes[2] = { USECAL_PREFIX, USECAL_POSTFIX };
    unsigned s;

    for (s = 0; s < 2u; s++) {
        ModelNvm nvm;
        ModelRuntimeArray rt;
        UseCalRun run;

        /* value 2 -- runtime-only raw output */
        fixture_board(&nvm, &rt, 99u, true);
        run = model_usecal_set(shapes[s], 2, &nvm, &rt, false);
        ASSERT_EQ(run.result, RES_OK);
        ASSERT_TRUE(run.rawOutputMode);
        ASSERT_FALSE(run.loadOk);
        ASSERT_FALSE(run.installAttempted);
        ASSERT_FALSE(run.saveAttempted);
        ASSERT_EQ(rt.installs, 0);
        ASSERT_EQ(nvm_total(nvm.erases), 0);
        ASSERT_EQ(nvm_total(nvm.writes), 0);

        /* #620 -- out of range, before any state mutation */
        fixture_board(&nvm, &rt, 99u, true);
        run = model_usecal_set(shapes[s], 7, &nvm, &rt, false);
        ASSERT_EQ(run.result, RES_ERR);
        ASSERT_FALSE(run.rawOutputMode);
        ASSERT_FALSE(run.loadOk);
        ASSERT_EQ(rt.installs, 0);
        ASSERT_EQ(nvm_total(nvm.erases), 0);
        ASSERT_EQ(nvm_total(nvm.writes), 0);
        ASSERT_EQ(persisted_selector(&nvm), 0);
    }
}

/* ==========================================================================
 * CASE 11 -- ROUND 8 (operator ruling 9=A, and round 7's audit of the
 * TopLevelMatchesCommitted mismatch branch).
 *
 * USECAL_R5 remains in the enum as the shape these tests are written
 * AGAINST: every assertion below that is new in this round FAILS if the
 * call site is edited to pass USECAL_R5 instead of USECAL_R8, which is this
 * round's mutation proof (recorded, with which assertion failed, in the PR
 * description -- swapping the shape is cheaper and more direct than editing
 * the model's if/else chain, and exercises the exact same code paths this
 * round added).
 * ========================================================================== */

/* CHANGE 1 + its hardware repro, both directions. USECal 2 (raw on) followed
 * by USECal 0/1 against a BLANK bank must still leave raw mode OFF even
 * though the command reports -200 -- and, in the same assertions, the
 * INDEPENDENCE property in full: nothing is persisted, the live coefficients
 * are untouched, and the command still fails. Raw mode is the one thing that
 * unconditionally changes.
 *
 * Under USECAL_R5 every ASSERT_FALSE(run.rawOutputMode) below fails (r5
 * leaves it exactly as rawModeIn found it, i.e. still TRUE) -- that is the
 * mutation proof for this test. */
TEST(round8_ruling_9a_leaves_raw_mode_despite_a_blank_bank)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;
    bool rawMode;

    /* --- direction: USECal 0 against a blank FACTORY bank --------------- */
    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, /*userBankValid=*/false);
    fixture_cal_bank(&nvm, MODEL_TYPE_FACT_CAL, /*valid=*/false, FACT_M, FACT_B);

    run = model_usecal_set(USECAL_R8, 2, &nvm, &rt, false);    /* USECal 2 */
    ASSERT_EQ(run.result, RES_OK);
    ASSERT_TRUE(run.rawOutputMode);
    rawMode = run.rawOutputMode;

    run = model_usecal_set(USECAL_R8, 0, &nvm, &rt, rawMode);  /* USECal 0 */
    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_FALSE(run.rawOutputMode);           /* <-- the 9=A repro, fixed */
    ASSERT_EQ(rt.installs, 0);                 /* nothing installed */
    ASSERT_EQ(rt.CalM[0], FACT_M);              /* live coefficients untouched */
    ASSERT_EQ(persisted_selector(&nvm), 0);     /* nothing persisted */
    ASSERT_EQ(nvm_total(nvm.erases), 0);
    ASSERT_EQ(nvm_total(nvm.writes), 0);

    /* --- direction: USECal 1 against a blank USER bank ------------------ */
    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, /*userBankValid=*/false);

    run = model_usecal_set(USECAL_R8, 2, &nvm, &rt, false);    /* USECal 2 */
    ASSERT_EQ(run.result, RES_OK);
    ASSERT_TRUE(run.rawOutputMode);
    rawMode = run.rawOutputMode;

    run = model_usecal_set(USECAL_R8, 1, &nvm, &rt, rawMode);  /* USECal 1 */
    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_FALSE(run.rawOutputMode);           /* <-- the 9=A repro, fixed */
    ASSERT_EQ(rt.installs, 0);
    ASSERT_EQ(rt.CalM[0], FACT_M);
    ASSERT_EQ(persisted_selector(&nvm), 0);
    ASSERT_EQ(nvm_total(nvm.erases), 0);
    ASSERT_EQ(nvm_total(nvm.writes), 0);
}

/* THE OTHER ARM of CHANGE 1's predicate pair: an out-of-range value must NOT
 * touch raw mode either way -- #620's "no mutation before the range check"
 * guarantee is unconditional, not narrowed by round 8. If Change 1's clear
 * were ever hoisted ABOVE this guard, or made unconditional for ANY param1
 * instead of only 0/1, THIS is the test that catches it;
 * round8_ruling_9a_leaves_raw_mode_despite_a_blank_bank would NOT, since it
 * never sends an out-of-range value -- so neither mutation is caught by both
 * tests. */
TEST(round8_out_of_range_does_not_touch_raw_mode)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;

    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    run = model_usecal_set(USECAL_R8, 7, &nvm, &rt, /*rawModeIn=*/true);
    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_TRUE(run.rawOutputMode);             /* unchanged -- still ON */

    run = model_usecal_set(USECAL_R8, 7, &nvm, &rt, /*rawModeIn=*/false);
    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_FALSE(run.rawOutputMode);            /* unchanged -- still OFF */
}

/* CHANGE 2, arm 1 of the pair: the command still returns an ERROR on the
 * TopLevelMatchesCommitted mismatch branch. Round 8 does not turn a genuine
 * persist failure into success -- only the *installed bank* changes, not
 * the result. Both directions.
 *
 * A mutation that accidentally treats a successful install as command
 * success (e.g. dropping the `return SCPI_RES_ERR` after installing) is
 * caught HERE and not by round8_mismatch_branch_installs_the_intended_bank,
 * which does not look at run.result at all. */
TEST(round8_mismatch_branch_still_returns_error)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;

    /* direction: USER -> FACTORY (param1 0) */
    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    runtime_init(&rt, USER_M, USER_B);    /* simulate: user cal was live */
    nvm.writeFails = true;                /* genuine failure: erase ran, the
                                            * write did not land, so the
                                            * committed page is unreadable
                                            * and TopLevelMatchesCommitted
                                            * cannot possibly match */
    run = model_usecal_set(USECAL_R8, 0, &nvm, &rt, false);
    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_EQ(run.recoveredAfterReportedFailure, false);

    /* direction: FACTORY -> USER (param1 1) */
    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    nvm.writeFails = true;
    run = model_usecal_set(USECAL_R8, 1, &nvm, &rt, false);
    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_EQ(run.recoveredAfterReportedFailure, false);
}

/* CHANGE 2, arm 2 of the pair: BUT the intended bank is now installed before
 * failing, so the live coefficients match the selection the caller asked
 * for instead of whatever was live before. Asserted on the actual
 * coefficient VALUES, not merely rt.installs == 1 -- a count alone would
 * still pass if the WRONG bank got installed. The USER -> FACTORY direction
 * is the one that produced the roughly-2x voltage before round 8: it left
 * rt.CalM[0] == USER_M (2.0) while the stored selector was destroyed, so
 * acquisition silently kept reporting at twice the true value.
 *
 * A mutation that forgets to add the install call on this branch (reverting
 * to the pre-round-8 shape) is caught HERE and not by
 * round8_mismatch_branch_still_returns_error, which does not look at
 * rt.installs or rt.CalM at all. */
TEST(round8_mismatch_branch_installs_the_intended_bank)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;

    /* direction: USER -> FACTORY -- the 2x-voltage direction. Deliberately
     * does NOT assert run.result -- that is
     * round8_mismatch_branch_still_returns_error's job, so a mutation that
     * collapses the two (e.g. treating a successful install as command
     * success) is caught there and not vacuously passed here too. */
    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    runtime_init(&rt, USER_M, USER_B);
    nvm.writeFails = true;
    run = model_usecal_set(USECAL_R8, 0, &nvm, &rt, false);
    ASSERT_EQ(run.installAttempted, true);
    ASSERT_EQ(rt.installs, 1);
    ASSERT_EQ(rt.installedBank, MODEL_TYPE_FACT_CAL);
    ASSERT_EQ(rt.CalM[0], FACT_M);        /* NOT USER_M (2.0) any more */

    /* direction: FACTORY -> USER */
    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    nvm.writeFails = true;
    run = model_usecal_set(USECAL_R8, 1, &nvm, &rt, false);
    ASSERT_EQ(run.installAttempted, true);
    ASSERT_EQ(rt.installs, 1);
    ASSERT_EQ(rt.installedBank, MODEL_TYPE_USER_CAL);
    ASSERT_EQ(rt.CalM[0], USER_M);        /* NOT FACT_M any more */
}

/* Supplements the pair above with the OTHER cause of a mismatch: a write
 * that LANDS but whose readback comparison is corrupted by a concurrent
 * writer landing someone else's value (r2/r5's original reproducer), rather
 * than a flat write failure. Single direction (param1 1): the fixture's
 * `commitForeignSelector` hardcodes the foreign record's calVals to 0, which
 * for param1 0 would coincide with our OWN intended value and read back as a
 * MATCH rather than a mismatch -- not a bug in the fix, just a fixture that
 * cannot produce a foreign-vs-intended mismatch when the intended value IS
 * the fixture's hardcoded foreign one. */
TEST(round8_mismatch_from_a_landed_write_also_installs)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;

    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, true);
    nvm.writeLandsButReadbackFails = true;
    nvm.commitForeignSelector = true;
    run = model_usecal_set(USECAL_R8, 1, &nvm, &rt, false);
    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_EQ(rt.installs, 1);
    ASSERT_EQ(rt.installedBank, MODEL_TYPE_USER_CAL);
    ASSERT_EQ(rt.CalM[0], USER_M);
    ASSERT_EQ(persisted_selector(&nvm), 0);   /* the foreign value, not 1 */
}

/* Persists the CURRENT runtime array into the USER bank -- CONF:ADC:SAVEcal,
 * modelled. Not part of ADCUseCalSetClaimed; needed only for the fw#1077 r6
 * sequence below, which chains it with a direct CHANCALM-style edit. */
static void model_savecal_from_runtime(ModelNvm *nvm,
                                       const ModelRuntimeArray *rt)
{
    ModelSettings rec;
    uint32_t crc;
    unsigned i;

    memset(&rec, 0, sizeof(rec));
    rec.settings.calParams.Size = MODEL_CHANNELS;
    for (i = 0; i < MODEL_CHANNELS; i++) {
        rec.settings.calParams.CalM[i] = rt->CalM[i];
        rec.settings.calParams.CalB[i] = rt->CalB[i];
    }
    crc = CRC32_Compute(&rec.settings, sizeof(ModelCalArray));
    memcpy(rec.checksum, &crc, sizeof(crc));
    rec.type = MODEL_TYPE_USER_CAL;
    nvm_store_page(nvm, MODEL_TYPE_USER_CAL, &rec);
}

/* THE fw#1077 ROUND-6 SEQUENCE -- becomes reachable once #1077 removes
 * boot's own self-stamp of the TopLevel record. Modelled as: boot has
 * already stamped a default TopLevel record (calVals=0, factory selected)
 * over otherwise-blank NVM (neither cal bank written yet); CHANCALM 0,2
 * edits the runtime array IN PLACE (CONF:ADC:chanCALM -- it is not routed
 * through ADCUseCalSetClaimed at all, so it is modelled here as a direct
 * field write, matching the production comment this file already cites
 * elsewhere: "CONF:ADC:chanCALM / chanCALB edit it in place"); SAVEcal
 * persists the current runtime array into the USER bank.
 *
 * Single direction only: the sequence as given names a specific order of
 * SCPI commands ending in USECal 1 then USECal 0, not a general "either
 * direction" property, so there is no natural param1-swapped twin to state
 * here -- reported as such rather than silently narrowed. */
TEST(round8_fw1077_r6_sequence_does_not_strand_a_mismatched_selector)
{
    ModelNvm nvm;
    ModelRuntimeArray rt;
    UseCalRun run;

    /* "blank NVM": neither cal bank written yet. */
    fixture_board(&nvm, &rt, MODEL_TYPE_TOP_LEVEL, /*userBankValid=*/false);
    fixture_cal_bank(&nvm, MODEL_TYPE_FACT_CAL, /*valid=*/false, FACT_M, FACT_B);

    /* CHANCALM 0,2 */
    rt.CalM[0] = 2.0;

    /* SAVEcal */
    model_savecal_from_runtime(&nvm, &rt);

    /* USECal 1 -- the just-saved USER bank now validates. */
    run = model_usecal_set(USECAL_R8, 1, &nvm, &rt, false);
    ASSERT_EQ(run.result, RES_OK);
    ASSERT_EQ(persisted_selector(&nvm), 1);
    ASSERT_EQ(rt.CalM[0], 2);

    /* USECal 0 -- FACTORY is blank, so this fails VALIDATION, which (#1148's
     * guarantee, unchanged by round 8) touches NOTHING: no erase, no write,
     * no install. */
    run = model_usecal_set(USECAL_R8, 0, &nvm, &rt, false);
    ASSERT_EQ(run.result, RES_ERR);
    ASSERT_EQ(rt.installs, 1);   /* only the earlier USECal 1's install */

    /* THE ASSERTION THIS SEQUENCE EXISTS FOR: USECal? and the live
     * coefficients must still AGREE. The bad outcome is "ends with the user
     * slope live while USECal? reads 0" -- selector 0 would claim FACTORY
     * while the user slope (CalM[0]==2) is still what acquisition uses. */
    ASSERT_EQ(persisted_selector(&nvm), 1);   /* NOT 0 */
    ASSERT_EQ(rt.CalM[0], 2);                 /* the user slope, consistently */
}

/* ==========================================================================
 * CASE 10 -- THE HEADLINE, in test_1112's shape: sweep the whole fixture
 * matrix and show that the two shapes differ in EXACTLY the cells where the
 * stored type is wrong AND the run got as far as the save.
 *
 * This is what makes the fix a fix rather than a narrowing: every cell whose
 * stored type is right, and every cell #1148's gate refuses before the save,
 * is required to be identical. A change that altered any of those would fail
 * here even if every assertion above still passed.
 * ========================================================================== */
TEST(the_two_shapes_differ_in_exactly_the_wrong_stored_type_cells)
{
    static const uint32_t storedTypes[] = {
        MODEL_TYPE_TOP_LEVEL, MODEL_TYPE_WIFI, MODEL_TYPE_FACT_CAL,
        MODEL_TYPE_USER_CAL, 99u, 0xFFFFFFFFu
    };
    size_t   t;
    int      param1;
    unsigned bankValid, eraseFault, writeFault;
    unsigned cells = 0, differing = 0, expectedDiffering = 0;

    for (t = 0; t < sizeof(storedTypes) / sizeof(storedTypes[0]); t++) {
        for (param1 = 0; param1 <= 1; param1++) {
            for (bankValid = 0; bankValid < 2u; bankValid++) {
                for (eraseFault = 0; eraseFault < 2u; eraseFault++) {
                    for (writeFault = 0; writeFault < 2u; writeFault++) {
                        ModelNvm nvmPre, nvmPost;
                        ModelRuntimeArray rtPre, rtPost;
                        UseCalRun runPre, runPost;
                        bool same, reachesSave, wrongType;

                        fixture_board(&nvmPre,  &rtPre,
                                      storedTypes[t], bankValid != 0u);
                        fixture_board(&nvmPost, &rtPost,
                                      storedTypes[t], bankValid != 0u);
                        nvmPre.eraseFails  = nvmPost.eraseFails  = (eraseFault != 0u);
                        nvmPre.writeFails  = nvmPost.writeFails  = (writeFault != 0u);

                        runPre  = model_usecal_set(USECAL_PREFIX,  param1,
                                                   &nvmPre,  &rtPre, false);
                        runPost = model_usecal_set(USECAL_POSTFIX, param1,
                                                   &nvmPost, &rtPost, false);

                        cells++;

                        /* "Same" is the FULL observable state, not the return
                         * code: a cell where both answer -200 but one of them
                         * erased somebody else's page is a difference. */
                        same = (runPre.result == runPost.result)
                            && (runPre.saveOutcome == runPost.saveOutcome)
                            && (runPre.typeHandedToSave == runPost.typeHandedToSave)
                            && (rtPre.installs == rtPost.installs)
                            && (rtPre.installedBank == rtPost.installedBank)
                            && (memcmp(nvmPre.page, nvmPost.page,
                                       sizeof(nvmPre.page)) == 0)
                            && (memcmp(nvmPre.erases, nvmPost.erases,
                                       sizeof(nvmPre.erases)) == 0)
                            && (memcmp(nvmPre.writes, nvmPost.writes,
                                       sizeof(nvmPre.writes)) == 0);
                        if (!same) {
                            differing++;
                        }

                        /* The predicate, stated independently of the arithmetic
                         * above: the shapes can only diverge at the save, and
                         * only when the stored type is not the one we asked
                         * for -- #1148's gate refuses everything else first. */
                        wrongType   = (storedTypes[t] != MODEL_TYPE_TOP_LEVEL);
                        reachesSave = runPre.saveAttempted;
                        ASSERT_EQ(runPre.saveAttempted, runPost.saveAttempted);
                        if (wrongType && reachesSave) {
                            expectedDiffering++;
                            if (same) {
                                printf("    storedType=%u param1=%d bank=%u "
                                       "erase=%u write=%u: expected the shapes "
                                       "to differ, they agreed\n",
                                       storedTypes[t], param1, bankValid,
                                       eraseFault, writeFault);
                            }
                            ASSERT_FALSE(same);
                        } else {
                            if (!same) {
                                printf("    storedType=%u param1=%d bank=%u "
                                       "erase=%u write=%u: the shapes differ "
                                       "where they must not\n",
                                       storedTypes[t], param1, bankValid,
                                       eraseFault, writeFault);
                            }
                            ASSERT_TRUE(same);
                        }
                    }
                }
            }
        }
    }

    ASSERT_EQ(cells, 6u * 2u * 2u * 2u * 2u);
    ASSERT_EQ(differing, expectedDiffering);
    /* The sweep is only meaningful if both halves are populated. */
    ASSERT_TRUE(differing > 0u);
    ASSERT_TRUE(cells - differing > 0u);
}

int main(void)
{
    printf("=== #1152: CONF:ADC:USECal must not trust the stored settings "
           "type ===\n");
    printf("---------------------------------------------\n");

    /* The premise */
    RUN(the_payload_checksum_does_not_cover_the_type_field);

    /* The defect, and the fix */
    RUN(pre_fix_installs_the_new_bank_then_cannot_persist_the_selector);
    RUN(post_fix_normalizes_the_type_and_the_same_record_persists);
    RUN(pre_fix_in_enum_wrong_type_overwrites_the_wifi_page_and_reports_success);
    RUN(pre_fix_stored_type_two_overwrites_the_factory_cal_bank_it_just_selected);
    RUN(post_fix_persists_correctly_for_every_stored_type_value);

    /* r2: the r1 audit's finding, closed. The pair is deliberate -- the
     * historical shapes leave the coefficients LIVE on a flash fault, r2
     * leaves them UNTOUCHED, and only both assertions together show the fix
     * changed the reachable state rather than the wording. */
    RUN(r2_a_flash_fault_leaves_the_coefficients_untouched);
    RUN(a_flash_fault_leaves_the_coefficients_live_in_the_two_historical_shapes);

    /* r2's own OPEN hazard, pinned. These assert the defective behaviour on
     * purpose -- see the block comment above them. */
    RUN(r6_accepts_the_intended_selector_despite_a_concurrent_unrelated_write);
    RUN(r5_the_concurrent_write_divergence_is_closed);
    RUN(r5_does_not_rescue_a_genuine_write_failure);
    RUN(r5_refuses_a_valid_record_that_is_not_the_intended_one);
    RUN(r2_erase_invalidation_destroys_the_stored_selector);
    RUN(r2_a_concurrent_settings_write_diverges_with_no_flash_fault);
    RUN(r2_a_write_protected_page_is_the_one_consistent_failure);

    /* What must NOT have moved */
    RUN(a_healthy_record_is_byte_for_byte_unchanged_by_the_fix);
    RUN(a_bank_that_fails_validation_still_refuses_before_install_or_persist);
    RUN(raw_mode_and_out_of_range_reach_no_nvm_in_either_shape);

    /* Round 8: ruling 9=A's raw-mode independence, and round 7's audit of
     * the TopLevelMatchesCommitted mismatch branch now installing. */
    RUN(round8_ruling_9a_leaves_raw_mode_despite_a_blank_bank);
    RUN(round8_out_of_range_does_not_touch_raw_mode);
    RUN(round8_mismatch_branch_still_returns_error);
    RUN(round8_mismatch_branch_installs_the_intended_bank);
    RUN(round8_mismatch_from_a_landed_write_also_installs);
    RUN(round8_fw1077_r6_sequence_does_not_strand_a_mismatched_selector);

    /* The headline */
    RUN(the_two_shapes_differ_in_exactly_the_wrong_stored_type_cells);

    return TEST_SUMMARY();
}
