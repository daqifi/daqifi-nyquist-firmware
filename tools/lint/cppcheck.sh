#!/bin/bash
# Reproducible cppcheck wrapper for the firmware tree.
# Used by audits #409 (uninit RAM) and #410 (volatile qualifier),
# and by CI to gate PRs against new findings.
#
# Excludes third-party trees that we don't own:
#   - firmware/src/third_party (FreeRTOS, wolfSSL, nanopb, zlib)
#   - firmware/src/libraries (scpi, microrl)
#   - firmware/src/config (Harmony-generated)
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
JOBS="${CPPCHECK_JOBS:-$(nproc 2>/dev/null || echo 1)}"

# `if !` so set -e doesn't abort on cppcheck failure; we want to surface
# cppcheck's own error output (which goes to TMP_OUT via stderr) before
# exiting, otherwise CI just shows "step failed" with no diagnostic.
if ! LC_ALL=C LANG=C cppcheck \
  -j "$JOBS" \
  --quiet \
  --template='{file}:{line}:{column}: {severity}: {message} [{id}]' \
  --enable=warning,style,performance,portability \
  --inconclusive \
  --suppressions-list=tools/lint/cppcheck-suppress.txt \
  --suppress=missingIncludeSystem \
  --suppress=unusedFunction \
  --suppress=unusedStructMember \
  -i firmware/src/third_party \
  -i firmware/src/libraries \
  -i firmware/src/config \
  -I firmware/src \
  -I firmware/src/config/default \
  -I firmware/src/config/default/driver/winc/include \
  -I firmware/src/config/default/driver/winc/include/dev \
  -I firmware/src/config/default/peripheral/cache \
  -I firmware/src/config/default/system/fs/fat_fs/file_system \
  -I firmware/src/config/default/system/fs/fat_fs/hardware_access \
  -I firmware/src/libraries/scpi/libscpi/inc \
  -I firmware/src/third_party/rtos/FreeRTOS/Source/include \
  -I firmware/src/third_party/rtos/FreeRTOS/Source/portable/MPLAB/PIC32MZ \
  -DHAVE_CONFIG_H \
  firmware/src/ 2>"$TMP_OUT"; then
  echo "::error::cppcheck exited non-zero — captured output below:"
  cat "$TMP_OUT"
  exit 1
fi

LC_ALL=C sort "$TMP_OUT" >"$OUT"

echo "Findings: $(wc -l < "$OUT") lines → $OUT"
echo "By severity:"
# `|| true` so the script doesn't exit non-zero under `set -euo pipefail`
# when there are zero findings (grep returns 1 when nothing matches).
grep -oE '\[[[:alnum:]_]+\]' "$OUT" | sort | uniq -c | sort -rn | head -20 || true
