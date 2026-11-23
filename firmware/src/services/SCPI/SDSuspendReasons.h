/* SDSuspendReasons.h -- the reason strings SD_SuspendReasonText() returns, as
 * string-literal macros, so the COMPILER can measure them (#1001).
 *
 * WHY MACROS: each reason is interpolated into a LOG_E whose line Logger cuts
 * at LOG_MESSAGE_SIZE - 3 = 125 bytes (Util/Logger.c). A reason that overruns
 * its prefix loses its tail, which for the quarantine reason is the command
 * that clears it (#986). Guards that pinned text ABOUT the strings were each
 * defeated in review (see #1001); sizeof on the real literal cannot be: it
 * measures the bytes after the compiler joins the literals, whatever the
 * source spacing.
 *
 * SD_SUSPEND_REASON_MAX is the length the log_budget gate's
 * `max=76` annotations at the SD_SuspendReasonText() callers assume
 * (SCPIInterface.c; tools/lint/log_budget.py measures prefix + that bound
 * against the ceiling). The asserts below make the bound true of the real
 * strings; tests/host/Makefile checks the annotations still say the same
 * number. Callers: SCPIStorageSD.c (SD_ArmOrRefuse, SD_RefuseIfSuspended,
 * SD:BENCH, startup-dir path) and SCPIInterface.c (the three "SD suspended"
 * sites and the #942 arm refusal).
 *
 * Pure literals, no includes: the host tests include this file directly. */
#ifndef SDSUSPENDREASONS_H
#define SDSUSPENDREASONS_H

#define SD_SUSPEND_REASON_QUARANTINE \
    "SD quarantined after a bus jam - reseat the card, then SYST:STOR:SD:ENAble 1"
#define SD_SUSPEND_REASON_FW_UPDATE \
    "a WiFi firmware update owns SPI4 - retry when it completes"
#define SD_SUSPEND_REASON_WIFI_STREAM \
    "WiFi streaming owns SPI4 - SYST:STR:STOP first"
#define SD_SUSPEND_REASON_NOT_RESUMED \
    "the SD task has not resumed yet - retry shortly"

/* Longest permitted reason, in characters (no NUL). Keep equal to the
 * `log_budget: max=` annotations on the callers. */
#define SD_SUSPEND_REASON_MAX 76

/* `s ""` makes anything that is not a string literal a compile error, so this
 * can never quietly measure sizeof(char *). */
#define SD_SUSPEND_REASON_LEN(s) (sizeof(s "") - 1u)

_Static_assert(SD_SUSPEND_REASON_LEN(SD_SUSPEND_REASON_QUARANTINE) <= SD_SUSPEND_REASON_MAX,
               "SD suspend reason (quarantine) exceeds SD_SUSPEND_REASON_MAX -- Logger would cut its tail");
_Static_assert(SD_SUSPEND_REASON_LEN(SD_SUSPEND_REASON_FW_UPDATE) <= SD_SUSPEND_REASON_MAX,
               "SD suspend reason (fw update) exceeds SD_SUSPEND_REASON_MAX");
_Static_assert(SD_SUSPEND_REASON_LEN(SD_SUSPEND_REASON_WIFI_STREAM) <= SD_SUSPEND_REASON_MAX,
               "SD suspend reason (wifi stream) exceeds SD_SUSPEND_REASON_MAX");
_Static_assert(SD_SUSPEND_REASON_LEN(SD_SUSPEND_REASON_NOT_RESUMED) <= SD_SUSPEND_REASON_MAX,
               "SD suspend reason (not resumed) exceeds SD_SUSPEND_REASON_MAX");

#endif /* SDSUSPENDREASONS_H */
