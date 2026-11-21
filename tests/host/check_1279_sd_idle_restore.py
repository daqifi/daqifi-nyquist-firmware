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


def check(sd_src, iface_src):
    sd, iface = strip(sd_src), strip(iface_src)

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
    if len(dues) != 2 or not dues[0] < begin < dues[1]:
        raise AssertionError("helper: SdIdleRestoreDue must be asked before "
                             "the claim AND again under it")
    busy = once(h, r"!sd_card_manager_IsBusy\(\)", "helper: busy refusal")
    prep = once(h, r"\bPrepareStreamingBuffers\(", "helper: re-partition")
    if not re.search(r"\bPrepareStreamingBuffers\([^;]*,true\);", h):
        raise AssertionError("helper: re-partition must request the idle "
                             "layout (last argument `true`)")
    end = once(h, r"\bStreaming_EndConfigChange\(\);", "helper: release")
    if not begin < dues[1] < busy < prep < end:
        raise AssertionError("helper: order must be claim -> re-ask -> busy "
                             "-> re-partition -> release")

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
            "ok = !sd_card_manager_IsBusy() &&", "ok =", 1)),
    }
    failed = 0
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
    print("check_1279 self-test: %d mutants all caught" % len(mutants))
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
