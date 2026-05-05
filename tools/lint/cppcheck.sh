#!/bin/bash
# Reproducible cppcheck wrapper for the firmware tree.
# Used by audits #409 (uninit RAM) and #410 (volatile qualifier).
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

cppcheck \
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
  -j"$(nproc)" \
  firmware/src/ 2>"$OUT"

echo "Findings: $(wc -l < "$OUT") lines → $OUT"
echo "By severity:"
grep -oE '\[[a-zA-Z]+\]' "$OUT" | sort | uniq -c | sort -rn | head -20
