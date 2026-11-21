#!/bin/bash
# Reproducible cppcheck wrapper for the firmware tree.
# Used by audits #409 (uninit RAM) and #410 (volatile qualifier),
# and by CI to gate PRs against new findings.
#
# Excludes third-party trees that we don't own:
#   - firmware/src/third_party (FreeRTOS, wolfSSL, nanopb, zlib)
#   - firmware/src/libraries (scpi, microrl)
#   - firmware/src/config (Harmony-generated) -- EXCEPT the hand-maintained
#     sub-trees in ANALYSED_CONFIG below, which get a second pass (#1165)
#
# Suppressions file: tools/lint/cppcheck-suppress.txt
# Include paths derived from firmware/daqifi.X/nbproject/Makefile-default.mk.

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

OUT="${1:-tools/lint/cppcheck-baseline.txt}"

# Capture cppcheck output to a temp file first, then sort into the final
# OUT path. Sorting (LC_ALL=C bytewise) eliminates baseline drift from
# filesystem-traversal-order differences across runners and OSes.
TMP_OUT="$(mktemp)"
trap 'rm -f "$TMP_OUT"' EXIT

# Parallelism: cppcheck emits findings in non-deterministic order under
# `-j N`, but the LC_ALL=C sort below normalizes ordering — so parallel
# + sort produces exactly the same baseline as single-threaded + sort.
# Override with CPPCHECK_JOBS env var when bisecting an ordering oddity.
# Validate: cppcheck rejects non-positive / non-numeric values with a
# generic argument error; reject early with a clear message instead.
JOBS="${CPPCHECK_JOBS:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"
case "$JOBS" in
  ''|*[!0-9]*)
    echo "::error::CPPCHECK_JOBS must be a positive integer, got: '$JOBS'" >&2
    exit 2
    ;;
  0)
    echo "::error::CPPCHECK_JOBS must be > 0, got: '$JOBS'" >&2
    exit 2
    ;;
esac

# Flags shared by every pass.
COMMON=(
  -j "$JOBS"
  --quiet
  --template='{file}:{line}:{column}: {severity}: {message} [{id}]'
  --enable=warning,style,performance,portability
  --inconclusive
  --suppressions-list=tools/lint/cppcheck-suppress.txt
  --suppress=missingIncludeSystem
  --suppress=unusedFunction
  --suppress=unusedStructMember
  -I firmware/src
  -I firmware/src/config/default
  -I firmware/src/config/default/driver/winc/include
  -I firmware/src/config/default/driver/winc/include/dev
  -I firmware/src/config/default/peripheral/cache
  -I firmware/src/config/default/system/fs/fat_fs/file_system
  -I firmware/src/config/default/system/fs/fat_fs/hardware_access
  -I firmware/src/libraries/scpi/libscpi/inc
  -I firmware/src/third_party/rtos/FreeRTOS/Source/include
  -I firmware/src/third_party/rtos/FreeRTOS/Source/portable/MPLAB/PIC32MZ
  -DHAVE_CONFIG_H
)

# Hand-edited sub-trees of firmware/src/config that ARE analysed (#1165).
# driver/sdspi carries our shared-SPI4 lock / quarantine / detect-poll logic.
# Adding one is a deliberate act: regenerate the baseline and read the delta.
#
# They get their OWN pass because they need -D__mips_hard_float=1. Without it
# every translation unit that includes FreeRTOS hits portmacro.h's FPU #error
# (suppressed above), cppcheck drops that configuration, and the file is
# silently NOT analysed -- measured: an injected out-of-bounds write in
# drv_sdspi_file_system.c produced zero findings under the main pass's flags.
# The define is NOT added to the main pass: doing so surfaces ~250 findings
# in files that are currently blind (tracked separately, see #1165 comments).
ANALYSED_CONFIG=(firmware/src/config/default/driver/sdspi)

# `if !` so set -e doesn't abort on cppcheck failure; we want to surface
# cppcheck's own error output (which goes to TMP_OUT via stderr) before
# exiting, otherwise CI just shows "step failed" with no diagnostic.
if ! LC_ALL=C LANG=C cppcheck "${COMMON[@]}" \
  -i firmware/src/third_party \
  -i firmware/src/libraries \
  -i firmware/src/config \
  firmware/src/ 2>"$TMP_OUT"; then
  echo "::error::cppcheck exited non-zero — captured output below:"
  cat "$TMP_OUT"
  exit 1
fi

TMP_CFG="$(mktemp)"
trap 'rm -f "$TMP_OUT" "$TMP_CFG"' EXIT
if ! LC_ALL=C LANG=C cppcheck "${COMMON[@]}" -D__mips_hard_float=1 \
  "${ANALYSED_CONFIG[@]}" 2>"$TMP_CFG"; then
  echo "::error::cppcheck (ANALYSED_CONFIG pass) exited non-zero — captured output below:"
  cat "$TMP_CFG"
  exit 1
fi
cat "$TMP_CFG" >>"$TMP_OUT"

LC_ALL=C sort "$TMP_OUT" >"$OUT"

echo "Findings: $(wc -l < "$OUT") lines → $OUT"
echo "By severity:"
# `|| true` so the script doesn't exit non-zero under `set -euo pipefail`
# when there are zero findings (grep returns 1 when nothing matches).
grep -oE '\[[[:alnum:]_]+\]' "$OUT" | sort | uniq -c | sort -rn | head -20 || true
