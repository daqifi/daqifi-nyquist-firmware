#!/bin/bash
# Reproducible cppcheck wrapper for the firmware tree.
# Used by audits #409 (uninit RAM) and #410 (volatile qualifier),
# and by CI to gate PRs against new findings.
#
# Excludes third-party trees that we don't own:
#   - firmware/src/third_party (FreeRTOS, wolfSSL, nanopb, zlib)
#   - firmware/src/libraries (scpi, microrl)
#   - firmware/src/config -- MOSTLY Harmony-generated, but it also holds
#     hand-edited sub-trees; those outside ANALYSED_CONFIG below are
#     known-unanalysed and are added one at a time (#1165 option B)
#
# Suppressions file: tools/lint/cppcheck-suppress.txt
# Include paths derived from firmware/daqifi.X/nbproject/Makefile-default.mk.
#
# Invocation (#1169 -- the DEFAULT checks, writing the baseline is opt-in):
#   bash tools/lint/cppcheck.sh                          CHECK: compare with the committed
#                                                        baseline (as CI does), exit 1 on any difference
#   bash tools/lint/cppcheck.sh --write-baseline         regenerate the committed baseline
#                                                        (refuses if any finding is not already in it)
#   bash tools/lint/cppcheck.sh --write-baseline --force regenerate even with new findings
#   bash tools/lint/cppcheck.sh <path>                   write findings to <path> (the CI form;
#                                                        <path> may not be the committed baseline)

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

BASELINE="tools/lint/cppcheck-baseline.txt"
WRITE=0
FORCE=0
OUT=""
for arg in "$@"; do
  case "$arg" in
    --write-baseline) WRITE=1 ;;
    --force) FORCE=1 ;;
    -*)
      echo "::error::unknown option: $arg (see the header of $0)" >&2
      exit 2
      ;;
    *)
      if [ -n "$OUT" ]; then
        echo "::error::more than one output path given" >&2
        exit 2
      fi
      OUT="$arg"
      ;;
  esac
done
# Validate AFTER parsing, so no combination depends on argument order.
if [ "$WRITE" = 1 ] && [ -n "$OUT" ]; then
  echo "::error::--write-baseline and an output path are mutually exclusive" >&2
  exit 2
fi
if [ "$FORCE" = 1 ] && [ "$WRITE" != 1 ]; then
  echo "::error::--force only applies to --write-baseline" >&2
  exit 2
fi
# The path form is unguarded by design (CI writes to a scratch file), so it
# must not reach the committed baseline: that would re-open #1169 under
# another spelling. Compare FILE IDENTITY (`-ef`: same device and inode), not
# spellings: on a case-insensitive checkout `tools/LINT/...` is the baseline
# under a different string, and so is a symlink or hard link. The realpath
# compare covers the case where the baseline file does not exist yet.
if [ -n "$OUT" ] && { [ "$OUT" -ef "$BASELINE" ] \
     || [ "$(realpath -m -- "$OUT")" = "$(realpath -m -- "$BASELINE")" ]; }; then
  echo "::error::an output path may not be the committed baseline; use --write-baseline" >&2
  exit 2
fi
if [ "$WRITE" = 1 ]; then
  MODE=write
elif [ -n "$OUT" ]; then
  MODE=path
else
  MODE=check
fi

# Capture cppcheck output to a temp file first, then sort into the final
# OUT path. Sorting (LC_ALL=C bytewise) eliminates baseline drift from
# filesystem-traversal-order differences across runners and OSes.
TMP_OUT="$(mktemp)"
SORTED_OUT="$(mktemp)"
trap 'rm -f "$TMP_OUT" "$SORTED_OUT"' EXIT

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
  # Without this, every translation unit that includes FreeRTOS hits
  # portmacro.h:150's FPU #error (suppressed in cppcheck-suppress.txt),
  # cppcheck drops that configuration, and the file is silently NOT
  # analysed (#1309: an injected out-of-bounds write in streaming.c
  # produced zero findings). The XC32 build defines it for the PIC32MZ.
  -D__mips_hard_float=1
)

# Hand-edited sub-trees of firmware/src/config that ARE analysed (#1165).
# driver/sdspi carries our shared-SPI4 lock / quarantine / detect-poll logic.
# Adding one is a deliberate act: regenerate the baseline and read the delta.
#
# They get their OWN pass only because they sit outside the main pass's
# `-i firmware/src/config` exclusion. Both passes carry
# -D__mips_hard_float=1 (see COMMON) so FreeRTOS-including files are analysed.
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
trap 'rm -f "$TMP_OUT" "$SORTED_OUT" "$TMP_CFG"' EXIT
if ! LC_ALL=C LANG=C cppcheck "${COMMON[@]}" \
  "${ANALYSED_CONFIG[@]}" 2>"$TMP_CFG"; then
  echo "::error::cppcheck (ANALYSED_CONFIG pass) exited non-zero — captured output below:"
  cat "$TMP_CFG"
  exit 1
fi
cat "$TMP_CFG" >>"$TMP_OUT"

LC_ALL=C sort "$TMP_OUT" >"$SORTED_OUT"

case "$MODE" in
  check)
    # The SAME comparator CI runs (line-insensitive multiset, #1309), so the
    # local check and the gate apply one rule. Exit 2 (cannot compare) stays 2.
    RC=0
    python3 -I tools/lint/cppcheck_compare.py "$BASELINE" "$SORTED_OUT" || RC=$?
    if [ "$RC" = 1 ]; then
      echo "::error::cppcheck findings differ from $BASELINE" >&2
      echo "Fix a real bug, or suppress a false positive in tools/lint/cppcheck-suppress.txt;" >&2
      echo "regenerate the baseline ONLY for resolved findings: $0 --write-baseline" >&2
    fi
    exit "$RC"
    ;;
  write)
    OUT="$BASELINE"
    # Refuse on any finding NOT already in the baseline, not on a count: a
    # change that fixes one finding and adds another keeps the count level
    # and would otherwise adopt the new one silently.
    # Line-insensitive, as the gate is: a finding that only moved is not new.
    # A baseline that does not exist yet accepts nothing.
    OLD="$BASELINE"; [ -e "$OLD" ] || OLD=/dev/null
    ADDED="$(python3 -I tools/lint/cppcheck_compare.py --count-added "$OLD" "$SORTED_OUT")"
    if [ "$ADDED" != 0 ] && [ "$FORCE" != 1 ]; then
      echo "::error::refusing to write the baseline: $ADDED finding(s) not in $BASELINE." >&2
      echo "Writing them would adopt new findings. Fix or suppress them; use --force only if deliberate." >&2
      python3 -I tools/lint/cppcheck_compare.py "$OLD" "$SORTED_OUT" >&2 || true
      exit 1
    fi
    ;;
esac

cp "$SORTED_OUT" "$OUT"

echo "Findings: $(wc -l < "$OUT") lines -> $OUT"
echo "By severity:"
# `|| true` so the script doesn't exit non-zero under `set -euo pipefail`
# when there are zero findings (grep returns 1 when nothing matches).
grep -oE '\[[[:alnum:]_]+\]' "$OUT" | sort | uniq -c | sort -rn | head -20 || true
