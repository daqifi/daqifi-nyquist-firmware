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
        return SAVE_ERASE_FAILED;
    }

    /* [crc(4) | zero(12)], computed over the arm's OWN dataSize. */
    crc = CRC32_Compute(&rec->settings, dataSize);
    memset(rec->checksum, 0, sizeof(rec->checksum));
    memcpy(rec->checksum, &crc, sizeof(crc));

    nvm->writes[type]++;
    if (nvm->writeFails) {
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

/* ==========================================================================
 * ADCUseCalSetClaimed(), in its two shapes
 * ========================================================================== */

typedef enum { RES_OK = 0, RES_ERR } ModelScpiResult;

typedef enum {
    USECAL_PREFIX = 0,   /* dc967c9e1: the loaded type is reused for the save */
    USECAL_POSTFIX       /* #1152: the type is normalized right after the load */
} UseCalShape;

typedef struct {
    ModelScpiResult  result;
    bool             loadOk;          /* step 1 succeeded */
    bool             installAttempted; /* step 2 was reached */
    bool             saveAttempted;   /* step 3 was reached */
    uint32_t         typeHandedToSave;/* what SaveToNvm's switch actually saw */
    ModelSaveOutcome saveOutcome;
    bool             rawOutputMode;
} UseCalRun;

static UseCalRun model_usecal_set(UseCalShape shape, int param1,
                                  ModelNvm *nvm, ModelRuntimeArray *rt)
{
    UseCalRun run;
    ModelSettings tmp;
    uint32_t calType;

    memset(&run, 0, sizeof(run));
    run.typeHandedToSave = MODEL_NO_BANK;
    run.saveOutcome = SAVE_OK;

    /* #620: reject an out-of-range value before ANY state mutation. */
    if (param1 < 0 || param1 > 2) {
        run.result = RES_ERR;
        return run;
    }

    /* #158/#270: value 2 is runtime-only raw output; it never reaches NVM. */
    if (param1 == 2) {
        run.rawOutputMode = true;
        run.result = RES_OK;
        return run;
    }

    /* --- step 1 --------------------------------------------------------- */
    memset(&tmp, 0, sizeof(tmp));
    if (!model_LoadFromNvm(nvm, MODEL_TYPE_TOP_LEVEL, &tmp)) {
        run.result = RES_ERR;
        return run;
    }
    run.loadOk = true;

    /* --- #1152, and the ONLY difference between the two shapes ----------- */
    if (shape == USECAL_POSTFIX) {
        tmp.type = MODEL_TYPE_TOP_LEVEL;
    }

    switch (param1) {
    case 0:  calType = MODEL_TYPE_FACT_CAL; break;
    case 1:  calType = MODEL_TYPE_USER_CAL; break;
    default: run.result = RES_ERR; return run;   /* unreachable, kept as in situ */
    }

    /* --- step 2: validate the bank by INSTALLING it ---------------------- */
    run.installAttempted = true;
    if (!model_LoadADCCalSettings(nvm, calType, rt)) {
        run.result = RES_ERR;
        return run;
    }

    /* --- step 3: only now persist the selector --------------------------- */
    tmp.settings.topLevelSettings.calVals = (uint8_t)param1;
    run.saveAttempted = true;
    run.typeHandedToSave = tmp.type;
    run.saveOutcome = model_SaveToNvm(nvm, &tmp);
    if (run.saveOutcome != SAVE_OK) {
        run.result = RES_ERR;
        return run;
    }

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

    run = model_usecal_set(USECAL_PREFIX, 1, &nvm, &rt);   /* CONF:ADC:USECal 1 */

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

    run = model_usecal_set(USECAL_POSTFIX, 1, &nvm, &rt);

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

    run = model_usecal_set(USECAL_PREFIX, 1, &nvm, &rt);

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
    run = model_usecal_set(USECAL_POSTFIX, 1, &nvm, &rt);
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

    run = model_usecal_set(USECAL_PREFIX, 0, &nvm, &rt);   /* CONF:ADC:USECal 0 */

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
    run = model_usecal_set(USECAL_POSTFIX, 0, &nvm, &rt);
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
        run = model_usecal_set(USECAL_PREFIX, 1, &nvm, &rt);
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
        run = model_usecal_set(USECAL_POSTFIX, 1, &nvm, &rt);
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

        runPre  = model_usecal_set(USECAL_PREFIX,  param1, &nvmPre,  &rtPre);
        runPost = model_usecal_set(USECAL_POSTFIX, param1, &nvmPost, &rtPost);

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

        run = model_usecal_set(shapes[s], 1, &nvm, &rt);

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
TEST(a_flash_fault_still_leaves_the_coefficients_live_in_both_shapes)
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
        run = model_usecal_set(shapes[s], 1, &nvm, &rt);
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
        run = model_usecal_set(shapes[s], 1, &nvm, &rt);
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
        run = model_usecal_set(shapes[s], 2, &nvm, &rt);
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
        run = model_usecal_set(shapes[s], 7, &nvm, &rt);
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
                                                   &nvmPre,  &rtPre);
                        runPost = model_usecal_set(USECAL_POSTFIX, param1,
                                                   &nvmPost, &rtPost);

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

    /* What must NOT have moved */
    RUN(a_healthy_record_is_byte_for_byte_unchanged_by_the_fix);
    RUN(a_bank_that_fails_validation_still_refuses_before_install_or_persist);
    RUN(a_flash_fault_still_leaves_the_coefficients_live_in_both_shapes);
    RUN(raw_mode_and_out_of_range_reach_no_nvm_in_either_shape);

    /* The headline */
    RUN(the_two_shapes_differ_in_exactly_the_wrong_stored_type_cells);

    return TEST_SUMMARY();
}
