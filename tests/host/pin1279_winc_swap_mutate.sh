#!/bin/bash
# #1279 r3: prove test_1279_winc_swap.c can fail. Builds four deliberately
# broken copies of the driver / the swap helper in a temp dir, points the
# Makefile's WINC_SPI_SRC / WINC_SWAP_SRC at them, and requires the test to go
# RED for each. Run from tests/host.
set -u
D=../../firmware/src/config/default/driver/winc/dev/spi/wdrv_winc_spi.c
S=../../firmware/src/services/SCPI/SCPIInterface.c
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
python3 - "$D" "$S" "$T" <<'EOF'
import sys
d, s_path, t = sys.argv[1:4]
def sub(src, a, b):
    assert src.count(a) == 1, a
    return src.replace(a, b)
drv = open(d, newline='').read()
open(t + "/m1.c", "w", newline='').write(
    sub(drv, "gWincSpiRxBusy = true;", "gWincSpiRxBusy = false;"))
open(t + "/m2.c", "w", newline='').write(sub(
    drv, "return !gWincSpiTxBusy && !gWincSpiRxBusy;",
    "return spiDcpt.transferTxHandle == DRV_SPI_TRANSFER_HANDLE_INVALID && "
    "spiDcpt.transferRxHandle == DRV_SPI_TRANSFER_HANDLE_INVALID;"))
sw = open(s_path, newline='').read().replace('\r\n', '\n')
open(t + "/m3.c", "w").write(sub(
    sw, "            WDRV_WINC_SPI_SetBuffer(wifiDmaBuf, wifiDmaSize);\n            swapped = true;",
    "            swapped = true;\n            (void)xTaskResumeAll();\n"
    "            WDRV_WINC_SPI_SetBuffer(wifiDmaBuf, wifiDmaSize);\n            vTaskSuspendAll();"))
m4 = sub(sw, "    sd_card_manager_SetWriteBuffer(sdDmaBuf, sdDmaSize);\n    return true;\n}\n\n/* #844",
         "    return true;\n}\n\n/* #844")
m4 = sub(m4, "            swapped = true;\n        }\n        (void)xTaskResumeAll();",
         "            sd_card_manager_SetWriteBuffer(sdDmaBuf, sdDmaSize);\n            swapped = true;\n        }\n        (void)xTaskResumeAll();")
open(t + "/m4.c", "w").write(m4)
EOF
bad=0
run() {  # name, make-var
  rm -f run_1279_winc_swap_tests
  make run_1279_winc_swap_tests "$2=$T/$1.c" >/dev/null 2>&1
  if [ ! -x run_1279_winc_swap_tests ]; then echo "$1: BUILD FAILED (not a valid mutant)"; bad=1; return; fi
  if ./run_1279_winc_swap_tests >"$T/$1.out" 2>&1; then
    echo "$1: GREEN -- the test did NOT catch this mutant"; bad=1
  else
    echo "$1: red as required ($(grep -c 'ASSERT FAILED' "$T/$1.out") assertion(s))"
  fi
}
run m1 WINC_SPI_SRC     # RX busy flag never raised
run m2 WINC_SPI_SRC     # handles-only idle predicate (the pre-fix WaitIdle)
run m3 WINC_SWAP_SRC    # scheduler resumed before SetBuffer
run m4 WINC_SWAP_SRC    # SD setter (takes a mutex) moved into the suspended block
rm -f run_1279_winc_swap_tests gen_1279_swap.h
exit $bad
