#!/usr/bin/env python3
"""Pin the #1279 call sites: SD operations restore the idle SD buffers first.

test_1279_sd_idle_buffer_restore.c runs the real sizing and the real
predicate. What it cannot see is whether anything ASKS: SCPIStorageSD.c and
SCPIInterface.c are not host-compilable. This reads them, comment- and
string-stripped, function by function, and checks:

  1. SCPI_StorageSDBenchmark calls `if (!SCPI_RestoreIdleSdBuffers())`, once,
     AFTER it owns the testInProgress claim (`ownsBenchFlag = true`) and BEFORE
     the SD claim and the arm, and the refusal leaves via `goto __exit_point`.
  2. SCPI_StorageSDGetData calls SCPI_RestoreIdleSdBuffers(), once, before the
     ReadBufferReady() pre-check and before it claims the SD manager.
  3. SCPI_RestoreIdleSdBuffers takes the config-change claim, re-asks the
     predicate, refuses while the SD manager is busy, re-partitions with the
     idle layout (`PrepareStreamingBuffers(..., true)`), and releases.
  4. PrepareStreamingBuffers only auto-balances when NOT asked for the idle
     layout, and every other caller still passes `false` -- so no streaming
     path changed.
  5. Queued output survives the restore: the helper pumps the USB and TCP
     output rings between the busy refusal and the re-partition (the drain's
     behaviour is run, spliced, by test_1279_restore_keeps_queued_output.c),
     and PrepareStreamingBuffers refuses an idle re-partition while either
     ring still holds bytes -- after taking the SD buffer lock, before the
     re-carve (StreamingBufferPool_Partition), releasing the lock on refusal.
     The rings' SetWriteBuffer reset would otherwise discard an earlier reply
     of the same message, or the command's own echo.

Function-scoped on purpose: SCPIStorageSD.c has many SD arms, and a file-wide
grep would pass while the one command this is about lost its call.

This is a text pin over two files (the #1033 class): it catches an ordinary
regression -- a deleted or moved call, a dropped refusal -- not a determined
obfuscation. `--self-test` proves each check can fail.

Run: python3 tests/host/check_1279_sd_idle_restore.py [--self-test]
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SD = ROOT / "firmware/src/services/SCPI/SCPIStorageSD.c"
IFACE = ROOT / "firmware/src/services/SCPI/SCPIInterface.c"
USB = ROOT / "firmware/src/services/UsbCdc/UsbCdc.c"
WIFI = ROOT / "firmware/src/services/wifi_services/wifi_manager.c"


def strip(src):
    """Blank comments and string/char literal CONTENTS, keep offsets stable."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i)); i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j])); i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            out.append(c + " " * (j - i - 1) + c); i = j + 1
        else:
            out.append(c); i += 1
    return "".join(out)


def body(text, name):
    """The body of the single DEFINITION of `name` (column-0 signature)."""
    defs = [m for m in re.finditer(
        r"^[A-Za-z_][\w \*]*\b%s\s*\([^;{]*\)\s*\{" % re.escape(name),
        text, re.M)]
    if len(defs) != 1:
        raise AssertionError("%s: expected 1 definition, found %d"
                             % (name, len(defs)))
    i = defs[0].end() - 1
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    raise AssertionError("%s: unbalanced braces" % name)


def ws(s):
    """Collapse whitespace and drop it around punctuation, so `f (x)`,
    `f( x )` and `f(x)` all read `f(x)` and the patterns below need none."""
    s = re.sub(r"\s+", " ", s)
    return re.sub(r" ?([(){};,!=&|?:<>*+\-]) ?", r"\1", s)


def once(b, pat, what):
    hits = [m.start() for m in re.finditer(pat, b)]
    if len(hits) != 1:
        raise AssertionError("%s: expected exactly 1, found %d"
                             % (what, len(hits)))
    return hits[0]


def first(b, pat, what):
    m = re.search(pat, b)
    if not m:
        raise AssertionError("%s: not found" % what)
    return m.start()


def check(sd_src, iface_src, usb_src=None, wifi_src=None):
    sd, iface = strip(sd_src), strip(iface_src)
    usb_src = USB.read_text() if usb_src is None else usb_src
    wifi_src = WIFI.read_text() if wifi_src is None else wifi_src

    # 1. SD:BENCHmark
    b = ws(body(sd, "SCPI_StorageSDBenchmark"))
    call = once(b, r"if\(!SCPI_RestoreIdleSdBuffers\(\)\)\{",
                "BENCH: `if (!SCPI_RestoreIdleSdBuffers())`")
    owns = first(b, r"\bownsBenchFlag=true;", "BENCH: ownsBenchFlag = true")
    claim = first(b, r"\bsd_card_manager_TryClaim\(", "BENCH: SD claim")
    arm = first(b, r"\bsd_card_manager_UpdateSettingsForPlainWrite\(",
                "BENCH: arm")
    if not owns < call < claim < arm:
        raise AssertionError(
            "BENCH: restore must follow `ownsBenchFlag = true` and precede the "
            "SD claim and the arm (offsets owns=%d call=%d claim=%d arm=%d)"
            % (owns, call, claim, arm))
    block = b[call:]
    block = block[:block.index("}") + 1]
    if "goto __exit_point;" not in block:
        raise AssertionError("BENCH: a failed restore no longer refuses "
                             "(no `goto __exit_point;` in its branch)")

    # 2. SD:GET
    g = ws(body(sd, "SCPI_StorageSDGetData"))
    call = once(g, r"\bSCPI_RestoreIdleSdBuffers\(", "GET: restore call")
    ready = first(g, r"\bsd_card_manager_ReadBufferReady\(",
                  "GET: ReadBufferReady")
    claim = first(g, r"\bSD_ClaimOrRefuse\(", "GET: SD claim")
    if not call < ready < claim:
        raise AssertionError("GET: restore must precede ReadBufferReady() and "
                             "the SD claim (call=%d ready=%d claim=%d)"
                             % (call, ready, claim))

    # 3. The helper
    h = ws(body(iface, "SCPI_RestoreIdleSdBuffers"))
    begin = once(h, r"\bStreaming_BeginConfigChange\(\)!=STREAM_CFG_CLAIM_OK", "helper: claim verdict")
    dues = [m.start() for m in re.finditer(r"\bSdIdleRestoreDue\(", h)]
    if len(dues) != 3 or not dues[0] < begin < dues[1]:
        raise AssertionError("helper: SdIdleRestoreDue must be asked before "
                             "the claim, again under it, and a third time "
                             "after the re-partition")
    busys = [m.start() for m in re.finditer(r"!sd_card_manager_IsBusy\(\)", h)]
    if len(busys) != 2:
        raise AssertionError("helper: busy refusal expected twice (before the "
                             "drain, and again after the command the drain "
                             "waited out), found %d" % len(busys))
    busy = busys[0]
    prep = once(h, r"\bPrepareStreamingBuffers\(", "helper: re-partition")
    if not re.search(r"\bPrepareStreamingBuffers\([^;]*,true\);", h):
        raise AssertionError("helper: re-partition must request the idle "
                             "layout (last argument `true`)")
    end = once(h, r"\bStreaming_EndConfigChange\(\);", "helper: release")
    raise_ = once(h, r"\bScpiCmdGate_Raise\(", "helper: gate raise")
    lower = once(h, r"\bScpiCmdGate_Lower\(", "helper: gate lower")
    if not begin < raise_ < dues[1]:
        raise AssertionError("helper: the gate must be raised after the claim "
                             "verdict and before the emptiness check")
    if not end < lower:
        raise AssertionError("helper: the gate must be lowered AFTER the "
                             "config claim is released")
    if re.search(r"\breturn\b", h[raise_:lower]):
        raise AssertionError("helper: a `return` between Raise and Lower "
                             "would leak the gate")
    if not h.endswith("return ok;}"):
        raise AssertionError("helper: single exit `return ok;` expected")
    if not (busy < busys[1] < prep):
        raise AssertionError("helper: second busy check must sit between the "
                             "drain and the re-partition")
    if not begin < dues[1] < busy < prep < dues[2] < end:
        raise AssertionError("helper: order must be claim -> re-ask -> busy "
                             "-> re-partition -> verify -> release")
    if not re.search(r"ok=ok&&!SdIdleRestoreDue\(", h[dues[2] - 10:]):
        raise AssertionError("helper: the post-re-partition predicate must "
                             "turn a fallback to minimum buffers into false "
                             "(`ok = ok && !SdIdleRestoreDue(...)`)")

    # 4. PrepareStreamingBuffers and its other callers
    p = ws(body(iface, "PrepareStreamingBuffers"))
    once(p, r"if\(isAutoMode&&!idleLayout\)",
         "prepare: auto-balance gated on !idleLayout")
    once(p, r"sdCircSize=mc->sdCircularBufSize\?mc->sdCircularBufSize:"
            r"SD_CARD_MANAGER_DEFAULT_CIRCULAR_SIZE;",
         "prepare: idle SD circular size")
    once(p, r"sdDmaSize=SD_CARD_MANAGER_CONF_WBUFFER_SIZE;",
         "prepare: idle SD DMA size")
    calls = re.findall(r"\bPrepareStreamingBuffers\(([^;]*?)\)\)?[{;]",
                       ws(iface))
    lasts = [c.rsplit(",", 1)[-1].strip(")") for c in calls
             if "uint32_t" not in c]
    if sorted(lasts) != ["false"] * 4 + ["true"]:
        raise AssertionError("prepare callers: expected four `false` and one "
                             "`true` (the helper), got %r" % lasts)

    # 5. Queued output is sent, or the idle re-partition refuses
    idle = once(h, r"\bScpiCmdGate_OthersIdle\(\)", "helper: others idle?")
    usbq = once(h, r"\bUsbCdc_PendingWriteBytes\(\)", "helper: USB queued?")
    wifiq = once(h, r"\bwifi_tcp_server_PendingWriteBytes\(\)",
                 "helper: TCP queued?")
    once(h, r"\bUsbCdc_PumpWrite\(\);", "helper: USB pump")
    once(h, r"if\(wifiQueued&&!UsbCdc_CallerIsUsbTask\(\)\)\{\(void\)"
            r"wifi_tcp_server_TransmitBufferedData\(\);",
         "helper: TCP pump, off the USB task only")
    once(h, r"ok=false;break;", "helper: drain deadline refuses")
    if not (idle < usbq and idle < wifiq):
        raise AssertionError("helper: OthersIdle must be read BEFORE the "
                             "pending counts")
    if not (busy < usbq < prep and busy < wifiq < prep):
        raise AssertionError("helper: the drain must sit between the busy "
                             "refusal and the re-partition")
    if not re.search(r"ok=ok&&PrepareStreamingBuffers\(", h):
        raise AssertionError("helper: a failed drain must skip the "
                             "re-partition (`ok = ok && Prepare...`)")
    guard = once(p, r"if\(idleLayout&&\(!ScpiCmdGate_OthersIdle\(\)\|\|"
                    r"UsbCdc_PendingWriteBytes\(\)!=0u\|\|"
                    r"wifi_tcp_server_PendingWriteBytes\(\)!=0u\)\)\{",
                 "prepare: idle re-partition refuses queued output")
    lock = first(p, r"\bsd_card_manager_TryLockBuffer\(\)", "prepare: lock")
    carve = first(p, r"\bStreamingBufferPool_Partition\(", "prepare: re-carve")
    if not lock < guard < carve:
        raise AssertionError("prepare: the queued-output refusal must follow "
                             "the SD buffer lock and precede the re-carve "
                             "(lock=%d guard=%d carve=%d)" % (lock, guard, carve))
    gblock = p[guard:]
    gblock = gblock[:gblock.index("}") + 1]
    if ("sd_card_manager_UnlockBuffer();" not in gblock
            or "return false;" not in gblock):
        raise AssertionError("prepare: the queued-output refusal must release "
                             "the SD buffer lock and return false")

    # 6. Both command-entry points are gated, and DEFER rather than drop
    u = ws(strip(usb_src))
    once(u, r"if\(ScpiCmdGate_TryEnter\(SCPI_CMD_GATE_USB\)\)\{"
            r"UsbCdc_FinalizeRead\(&gRunTimeUsbSttings\);"
            r"ScpiCmdGate_Exit\(SCPI_CMD_GATE_USB\);\}",
         "USB: the only command entry is gated")
    if len(re.findall(r"\bUsbCdc_FinalizeRead\(&", u)) != 1:
        raise AssertionError("USB: exactly one UsbCdc_FinalizeRead call")
    w = ws(strip(wifi_src))
    once(w, r"if\(drainTcpRx&&gTcpRxPending&&ScpiCmdGate_TryEnter"
            r"\(SCPI_CMD_GATE_TCP\)\)\{gTcpRxPending=false;",
         "TCP: the RX drain is gated and clears the flag only once entered")
    if len(re.findall(r"\bwifi_tcp_server_ProcessReceivedBuff\(\);", w)) != 2:
        raise AssertionError("TCP: exactly one ProcessReceivedBuff call (plus its extern declaration)")
    at = w.index("ScpiCmdGate_TryEnter(SCPI_CMD_GATE_TCP)")
    depth, k = 0, w.index("{", at)
    k2 = k
    for k2 in range(k, len(w)):
        depth += (w[k2] == "{") - (w[k2] == "}")
        if depth == 0:
            break
    blk = w[k:k2]
    if "wifi_tcp_server_ProcessReceivedBuff()" not in blk or \
            not blk.endswith("ScpiCmdGate_Exit(SCPI_CMD_GATE_TCP);"):
        raise AssertionError("TCP: ProcessReceivedBuff must sit inside the "
                             "gate, with Exit as the block's last statement")


def _move_guard_after_carve(iface):
    """Mutant: the queued-output refusal moved past the re-carve, where a
    refusal could no longer leave the old partition intact."""
    start = iface.find("    if (idleLayout && (!ScpiCmdGate_OthersIdle()")
    if start < 0:
        return iface
    end = iface.index("\n    }\n", start) + len("\n    }\n")
    guard = iface[start:end]
    rest = iface[:start] + iface[end:]
    carve = rest.index("StreamingBufferPool_Partition(", start)
    after = rest.index(";\n", carve) + 2
    return rest[:after] + guard + rest[after:]


def self_test():
    sd, iface = SD.read_text(), IFACE.read_text()
    check(sd, iface)  # the shipped tree must pass first
    mutants = {
        "BENCH call deleted": (sd.replace(
            "if (!SCPI_RestoreIdleSdBuffers()) {",
            "if (0) {", 1), iface),
        "BENCH refusal falls through": (sd.replace(
            "non-SD stream - retry\");\n        result = SCPI_RES_ERR;\n"
            "        goto __exit_point;", "non-SD stream - retry\");\n"
            "        result = SCPI_RES_ERR;", 1), iface),
        "GET call deleted": (sd.replace(
            "(void)SCPI_RestoreIdleSdBuffers();", ";", 1), iface),
        "helper asks for the auto layout": (sd, iface.replace(
            "                                     0, true);",
            "                                     0, false);", 1)),
        "prepare ignores idleLayout": (sd, iface.replace(
            "if (isAutoMode && !idleLayout) {", "if (isAutoMode) {", 1)),
        "helper drops the busy refusal": (sd, iface.replace(
            "ok = !sd_card_manager_IsBusy();", "ok = true;", 1)),
        "helper drops the second busy check": (sd, iface.replace(
            "ok = ok && !sd_card_manager_IsBusy();", "", 1)),
        "helper never raises the gate": (sd, iface.replace(
            "bool raised = ScpiCmdGate_Raise(self);",
            "bool raised = true;", 1)),
        "helper lowers the gate before the claim release": (sd, iface.replace(
            "    Streaming_EndConfigChange();\n    if (raised) {\n"
            "        ScpiCmdGate_Lower(self);\n    }\n",
            "    if (raised) {\n        ScpiCmdGate_Lower(self);\n    }\n"
            "    Streaming_EndConfigChange();\n", 1)),
        "helper returns early between Raise and Lower": (sd, iface.replace(
            "        ok = !sd_card_manager_IsBusy();\n        TickType_t",
            "        ok = !sd_card_manager_IsBusy();\n"
            "        if (!ok) return false;\n        TickType_t", 1)),
        "helper reads pending before OthersIdle": (sd, iface.replace(
            "bool othersIdle = ScpiCmdGate_OthersIdle();\n"
            "            bool usbQueued = (UsbCdc_PendingWriteBytes() != 0u);",
            "bool usbQueued = (UsbCdc_PendingWriteBytes() != 0u);\n"
            "            bool othersIdle = ScpiCmdGate_OthersIdle();", 1)),
        "prepare guard deleted": (sd, iface.replace(
            "if (idleLayout && (!ScpiCmdGate_OthersIdle() ||",
            "if (0 && (!ScpiCmdGate_OthersIdle() ||", 1)),
        "prepare guard drops OthersIdle": (sd, iface.replace(
            "if (idleLayout && (!ScpiCmdGate_OthersIdle() ||",
            "if (idleLayout && (0 ||", 1)),
        "prepare guard keeps the SD buffer lock": (sd, iface.replace(
            "output still queued (#1279)\");\n        "
            "sd_card_manager_UnlockBuffer();\n", "output still queued "
            "(#1279)\");\n", 1)),
        "prepare guard after the re-carve": (sd, _move_guard_after_carve(
            iface)),
        "helper re-partitions after a failed drain": (sd, iface.replace(
            "        ok = ok &&\n             PrepareStreamingBuffers(",
            "        ok =\n             PrepareStreamingBuffers(", 1)),
        "helper trusts the re-partition's true": (sd, iface.replace(
            "ok = ok && !SdIdleRestoreDue(", "ok = ok && SdIdleRestoreDue(", 1)),
        "helper drops the post-re-partition verify": (sd, iface.replace(
            "        ok = ok && !SdIdleRestoreDue(StreamingBufferPool_SdCircularSize(),\n"
            "                                     sd_card_manager_GetWriteBufferSize(),\n"
            "                                     mc->sdCircularBufSize);\n", "", 1)),
        "helper pumps TCP from the USB task": (sd, iface.replace(
            "if (wifiQueued && !UsbCdc_CallerIsUsbTask()) {",
            "if (wifiQueued) {", 1)),
    }
    usb, wifi = USB.read_text(), WIFI.read_text()
    check(sd, iface, usb, wifi)
    extra = {
        "USB entry ungated": (usb.replace(
            "if (ScpiCmdGate_TryEnter(SCPI_CMD_GATE_USB)) {", "if (1) {", 1), wifi),
        "TCP entry ungated": (usb, wifi.replace(
            " && ScpiCmdGate_TryEnter(SCPI_CMD_GATE_TCP)", "", 1)),
        "TCP flag cleared before entry": (usb, wifi.replace(
            "if (drainTcpRx && gTcpRxPending && ScpiCmdGate_TryEnter(SCPI_CMD_GATE_TCP)) {\n"
            "        gTcpRxPending = false;",
            "gTcpRxPending = false;\n"
            "    if (drainTcpRx && ScpiCmdGate_TryEnter(SCPI_CMD_GATE_TCP)) {", 1)),
        "TCP gate never exited": (usb, wifi.replace(
            "        ScpiCmdGate_Exit(SCPI_CMD_GATE_TCP);\n", "", 1)),
    }
    failed = 0
    for name, (u_, w_) in extra.items():
        if (u_, w_) == (usb, wifi):
            print("SELF-TEST FAIL: mutant %r did not apply" % name)
            failed += 1
            continue
        try:
            check(sd, iface, u_, w_)
        except AssertionError:
            continue
        print("SELF-TEST FAIL: mutant %r passed the check" % name)
        failed += 1
    mutants_n = len(mutants) + len(extra)
    for name, (s, i) in mutants.items():
        if (s, i) == (sd, iface):
            print("SELF-TEST FAIL: mutant %r did not apply" % name)
            failed += 1
            continue
        try:
            check(s, i)
        except AssertionError:
            continue
        print("SELF-TEST FAIL: mutant %r passed the check" % name)
        failed += 1
    if failed:
        return 1
    print("check_1279 self-test: %d mutants all caught" % mutants_n)
    return 0


def main():
    if "--self-test" in sys.argv[1:]:
        return self_test()
    try:
        check(SD.read_text(), IFACE.read_text())
    except AssertionError as e:
        print("FAIL: #1279 -- %s" % e)
        print("      SD:BENCHmark / SD:GET must restore the idle SD buffers "
              "before arming; see test_1279_sd_idle_buffer_restore.c.")
        return 1
    print("check_1279: SD idle-buffer restore call sites OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
