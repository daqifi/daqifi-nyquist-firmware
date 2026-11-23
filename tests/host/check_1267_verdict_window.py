#!/usr/bin/env python3
"""Pin fw#1267 round 8: every tagged SD:GET outcome holds the data window.

sd_get_send_end is the ONE place a tagged GET queues its verdict. A failure
BEFORE the header (open, buffer, size, busy, abort) never went through
sd_get_open_window's success path, so no window was open, ReleaseWhenDrained was
a no-op and the framed `#SDGET <tag> #10 __END_OF_FILE__ FAILED <reason>`
verdict sat in the transport's write ring unprotected: a SYST:MEM:AUTO
processed before WifiTask transmitted it took the non-idle partition path,
reset the TCP ring and deleted the verdict (#1267 round-7 audit, finding 2).

The fix makes sure of the window at the choke point, BEFORE the verdict is
queued. This reads sd_card_manager.c, comment- and string-stripped, scoped to
the one function, and checks:

  1. sd_get_send_end calls sd_get_hold_window_for_verdict(), exactly once,
     under `if (!headerOut)`.
  2. That call precedes the function's FIRST sd_get_emit( -- a hold taken
     after the verdict is queued protects nothing.
  3. sd_get_hold_window_for_verdict opens a window with ScpiSdWindow_TryOpen and
     never releases one (no ReleaseWhenDrained / HoldUntilSessionEnd), so the
     single release site stays sd_get_send_end.

A text pin over one file (the #1033 class): it catches a deleted or moved
call, not determined obfuscation. `--self-test` proves each check can fail.

Run: python3 tests/host/check_1267_verdict_window.py [--self-test]
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SDM = ROOT / "firmware/src/services/sd_card_services/sd_card_manager.c"


def strip(src):
    """Blank comments and string/char literal contents; offsets stay stable."""
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
        elif src[i] in "\"'":
            q, j = src[i], i + 1
            while j < n and src[j] != q:
                j += 2 if src[j] == "\\" else 1
            out.append(q + " " * max(0, j - i - 1) + q)
            i = j + 1
        else:
            out.append(src[i])
            i += 1
    return "".join(out)


def body(src, name):
    m = re.search(r"\bstatic\s+\w[\w\s\*]*?\b" + re.escape(name) + r"\s*\([^)]*\)\s*\{", src)
    if not m:
        raise AssertionError("function %s not found" % name)
    depth, k = 0, m.end() - 1
    for k in range(m.end() - 1, len(src)):
        depth += (src[k] == "{") - (src[k] == "}")
        if depth == 0:
            break
    return src[m.end():k]


def ws(s):
    return re.sub(r"\s+", "", s)


def check(text):
    s = strip(text)
    send = body(s, "sd_get_send_end")
    w = ws(send)
    hold = re.findall(r"if\(!headerOut\)\{sd_get_hold_window_for_verdict\(\);\}", w)
    if len(hold) != 1 or w.count("sd_get_hold_window_for_verdict(") != 1:
        raise AssertionError("send_end: expected exactly one "
                             "`if (!headerOut) { sd_get_hold_window_for_verdict(); }`")
    if w.index("sd_get_hold_window_for_verdict(") > w.index("sd_get_emit("):
        raise AssertionError("send_end: the window hold must come BEFORE the first "
                             "sd_get_emit( -- a hold after the verdict is queued "
                             "protects nothing")
    h = ws(body(s, "sd_get_hold_window_for_verdict"))
    if "ScpiSdWindow_TryOpen(" not in h:
        raise AssertionError("hold: must open a window with ScpiSdWindow_TryOpen")
    for bad in ("ReleaseWhenDrained", "HoldUntilSessionEnd"):
        if bad in h:
            raise AssertionError("hold: must not release a window (%s): "
                                 "sd_get_send_end is the single release site" % bad)


def self_test():
    good = SDM.read_text(encoding="utf-8")
    check(good)
    muts = {
        "call deleted": lambda t: t.replace(
            "        sd_get_hold_window_for_verdict();\n", "", 1)
        .replace("        sd_get_hold_window_for_verdict();\r\n", "", 1),
        "call moved after the verdict": lambda t: t.replace(
            "    if (!headerOut) {\n        sd_get_hold_window_for_verdict();\n    }\n", "", 1)
        .replace("    if (!endQueued) {\n        if (transport == SCPI_SD_WINDOW_TCP) {",
                 "    if (!headerOut) { sd_get_hold_window_for_verdict(); }\n"
                 "    if (!endQueued) {\n        if (transport == SCPI_SD_WINDOW_TCP) {", 1),
        "guard dropped": lambda t: t.replace("if (!headerOut) {\n        sd_get_hold",
                                             "{\n        sd_get_hold", 1),
        "hold releases": lambda t: t.replace(
            "ScpiSdWindow_TryOpen(transport, generation)) {\n            return;",
            "ScpiSdWindow_TryOpen(transport, generation)) {\n"
            "            ScpiSdWindow_ReleaseWhenDrained(transport);\n            return;", 1),
    }
    bad = 0
    for name, mut in muts.items():
        m = mut(good)
        if m == good:
            print("SELF-TEST ERROR: mutant %r did not change the source" % name)
            bad += 1
            continue
        try:
            check(m)
        except AssertionError:
            print("  mutant %-32s killed" % name)
        else:
            print("  mutant %-32s SURVIVED" % name)
            bad += 1
    return bad


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        sys.exit(1 if self_test() else 0)
    try:
        check(SDM.read_text(encoding="utf-8"))
    except AssertionError as e:
        print("FAIL: %s" % e)
        sys.exit(1)
    print("OK: the tagged-GET verdict holds the window at sd_get_send_end")
