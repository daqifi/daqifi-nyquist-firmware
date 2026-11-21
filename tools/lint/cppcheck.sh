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
#
# Invocation (#1169 -- the DEFAULT checks, writing the baseline is opt-in):
#   bash tools/lint/cppcheck.sh                          CHECK: diff against the
#                                                        committed baseline, exit 1 on any difference
#   bash tools/lint/cppcheck.sh --write-baseline         regenerate the committed baseline
#                                                        (refuses if the finding count grew)
#   bash tools/lint/cppcheck.sh --write-baseline --force regenerate even when findings grew
#   bash tools/lint/cppcheck.sh <path>                   write findings to <path> (the CI form)

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

BASELINE="tools/lint/cppcheck-baseline.txt"
MODE=check
FORCE=0
OUT=""
for arg in "$@"; do
  case "$arg" in
    --write-baseline) MODE=write ;;
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
      MODE=path
      OUT="$arg"
      ;;
  esac
done
if [ "$FORCE" = 1 ] && [ "$MODE" != write ]; then
  echo "::error::--force only applies to --write-baseline" >&2
  exit 2
fi
if [ "$MODE" = write ] && [ -n "$OUT" ]; then
  echo "::error::--write-baseline and an output path are mutually exclusive" >&2
  exit 2
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

LC_ALL=C sort "$TMP_OUT" >"$SORTED_OUT"

case "$MODE" in
  check)
    # --strip-trailing-cr matches the CI comparison (CRLF stragglers).
    if diff -u --strip-trailing-cr "$BASELINE" "$SORTED_OUT"; then
      echo "OK: cppcheck findings match $BASELINE ($(wc -l < "$BASELINE") lines)"
      exit 0
    fi
    echo "::error::cppcheck findings differ from $BASELINE ('+' = new finding, '-' = resolved)" >&2
    echo "Fix a real bug, or suppress a false positive in tools/lint/cppcheck-suppress.txt;" >&2
    echo "regenerate the baseline ONLY for resolved findings: $0 --write-baseline" >&2
    exit 1
    ;;
  write)
    OUT="$BASELINE"
    OLD_COUNT="$(wc -l < "$BASELINE" 2>/dev/null || echo 0)"
    NEW_COUNT="$(wc -l < "$SORTED_OUT")"
    if [ "$NEW_COUNT" -gt "$OLD_COUNT" ] && [ "$FORCE" != 1 ]; then
      echo "::error::refusing to write the baseline: findings grew $OLD_COUNT -> $NEW_COUNT." >&2
      echo "A growing baseline adopts new findings. Fix or suppress them; use --force only if deliberate." >&2
      diff -u --strip-trailing-cr "$BASELINE" "$SORTED_OUT" >&2 || true
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
