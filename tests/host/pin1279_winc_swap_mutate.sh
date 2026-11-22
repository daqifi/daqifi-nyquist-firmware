#!/bin/bash
# #1279 r3/r4: prove test_1279_winc_swap.c can fail. Builds deliberately broken
# copies of the WINC driver, the swap helper, UsbCdc.c and sd_card_manager.c in
# a temp dir, points the Makefile's WINC_SPI_SRC / WINC_SWAP_SRC / USB_SWAP_SRC /
# SD_SWAP_SRC at them, and requires the test to go RED for each. Run from
# tests/host.
set -u
D=../../firmware/src/config/default/driver/winc/dev/spi/wdrv_winc_spi.c
S=../../firmware/src/services/SCPI/SCPIInterface.c
U=../../firmware/src/services/UsbCdc/UsbCdc.c
Q=../../firmware/src/services/sd_card_services/sd_card_manager.c
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
python3 - "$D" "$S" "$U" "$Q" "$T" <<'PYEOF'
import sys
d, s_path, u_path, q_path, t = sys.argv[1:6]
def sub(src, a, b):
    assert src.count(a) == 1, a
    return src.replace(a, b)
def rd(p): return open(p, newline='').read().replace('\r\n', '\n')
def wr(n, x): open(t + "/" + n + ".c", "w").write(x)
drv = open(d, newline='').read()
wr("m1", sub(drv, "gWincSpiRxBusy = true;", "gWincSpiRxBusy = false;"))
wr("m2", sub(drv, "return !gWincSpiTxBusy && !gWincSpiRxBusy;",
    "return spiDcpt.transferTxHandle == DRV_SPI_TRANSFER_HANDLE_INVALID && "
    "spiDcpt.transferRxHandle == DRV_SPI_TRANSFER_HANDLE_INVALID;"))
sw = rd(s_path)
wr("m3", sub(sw, "            WDRV_WINC_SPI_SetBuffer(wifiDmaBuf, wifiDmaSize);\n            swapped = true;",
    "            swapped = true;\n            (void)xTaskResumeAll();\n"
    "            WDRV_WINC_SPI_SetBuffer(wifiDmaBuf, wifiDmaSize);\n            vTaskSuspendAll();"))
wr("m4", sub(sw, "        bool usbHeld = UsbCdc_DmaSwapAcquire(keepQueuedOutput, &discarded);",
    "        bool usbHeld = true; (void)discarded;"))
wr("m5", sub(sw, "        bool sdHeld  = usbHeld && sd_card_manager_DmaSwapAcquire();",
    "        bool sdHeld  = usbHeld;"))
wr("m6", sub(sw, "            if (usbHeld) UsbCdc_DmaSwapRelease();", ""))
wr("m7", sub(sw, "            if (sdHeld)  sd_card_manager_DmaSwapRelease();", ""))
usb = rd(u_path)
wr("u1", sub(usb, "    if (gRunTimeUsbSttings.writeInProgress ||\n        gRunTimeUsbSttings.writeTransferHandle !=",
    "    if (0 ||\n        gRunTimeUsbSttings.writeTransferHandle !="))
wr("u2", sub(usb, "        gRunTimeUsbSttings.writeTransferHandle != USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID ||\n        (keepDeliverable",
    "        (keepDeliverable"))
wr("u3", sub(usb, "    if (xSemaphoreTake(gRunTimeUsbSttings.wMutex, 0) != pdTRUE) {", "    if (0) {"))
wr("u4", sub(usb, "(keepDeliverable && deliverable && q != 0u)) {", "0) {"))
wr("u5", sub(usb, "    if (gUsbSwapDiscardRing) {\n        CircularBuf_Reset(&gRunTimeUsbSttings.wCirbuf);\n    }\n", ""))
sd = rd(q_path)
wr("s1", sub(sd, "    bool idle = (gSDCardData.currentProcessState == SD_CARD_MANAGER_PROCESS_STATE_IDLE ||\n"
    "                 gSDCardData.currentProcessState == SD_CARD_MANAGER_PROCESS_STATE_INIT) &&\n                gpSDCardSettings",
    "    bool idle = 1 &&\n                gpSDCardSettings"))
wr("s2", sub(sd, "gpSDCardSettings->mode != SD_CARD_MANAGER_MODE_WRITE;", "1;"))
wr("s3", sub(sd, "    if (xSemaphoreTake(gSDCardData.wMutex, 0) != pdTRUE) {\n        return false;\n    }\n    bool idle",
    "    bool idle"))
PYEOF
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
run m4 WINC_SWAP_SRC    # USB owner never checked (the r4 finding)
run m5 WINC_SWAP_SRC    # SD owner never checked
run m6 WINC_SWAP_SRC    # refusal path leaks the USB wMutex
run m7 WINC_SWAP_SRC    # refusal path leaks the SD wMutex
run u1 USB_SWAP_SRC     # writeInProgress dropped from the USB idle predicate
run u2 USB_SWAP_SRC     # transfer handle dropped
run u3 USB_SWAP_SRC     # no try-take: a task mid-BeginWrite is invisible
run u4 USB_SWAP_SRC     # a deliverable ring no longer refuses
run u5 USB_SWAP_SRC     # an undeliverable ring is not discarded at commit
run s1 SD_SWAP_SRC      # SD state clause dropped
run s2 SD_SWAP_SRC      # SD armed-write (mode) clause dropped
run s3 SD_SWAP_SRC      # SD wMutex try-take dropped
rm -f run_1279_winc_swap_tests gen_1279_swap.h gen_1279_usb.h gen_1279_sd.h
exit $bad
