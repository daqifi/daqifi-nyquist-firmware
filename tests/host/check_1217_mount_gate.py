#!/usr/bin/env python3
"""Pin that no SD mount can start while an SD:PROTect change is pending (#1217).

SD:PROTect applies a new CRC mode by having the driver drop the card and
re-initialize it at its next quiet detect poll. A volume mounted before that
poll would have the re-init land under it. Two audit rounds on PR #1228 found
this twice: first inside the driver (fixed there, and pinned by the real-driver
test mode_change_pending_during_io_waits_for_a_quiet_poll), then across
transports: SD:PROTect over TCP while SD:ENAble 1 arrives over USB.

The exclusion lives in three SCPI handlers, and every one of them has to keep
its half:

  SCPI_StorageSDProtectSet  takes the manager claim, then, in ONE critical
                            section, checks `enable` and makes the request.
  SCPI_StorageSDEnableSet   sets `enable = true` only inside a critical section
                            and only under `!DRV_SDSPI_CrcModePending(0)`.
  SCPI_StorageSDSpaceGet    (the one mount that runs with SD disabled) checks
                            for a pending request after taking the claim and
                            before arming GET_SPACE.

And the two mount triggers must stay the only ones: `enable = true` is written
nowhere else, and GET_SPACE is armed nowhere else.

SCPIStorageSD.c cannot be host-compiled (FreeRTOS, libscpi, Harmony, the board
graph), so this reads the REAL source, like check_996_eviction_permanence.py.
Comments are stripped by the compiler first: the handlers' comments name these
very shapes in prose.

WHAT A CORRECT CHANGE WILL BREAK: this reads source text, so renaming these
functions or moving the exclusion into a helper fails it with the behaviour
intact. Update the pin in the same PR; do not delete it.

usage: check_1217_mount_gate.py [--source PATH]
"""
import argparse
import re
import subprocess
import sys

DEFAULT_SOURCE = "../../firmware/src/services/SCPI/SCPIStorageSD.c"

fails = []


def check(ok, msg):
    print("  [%s] %s" % ("ok" if ok else "FAIL", msg))
    if not ok:
        fails.append(msg)


def strip_comments(src, cc="cc"):
    out = subprocess.run([cc, "-fpreprocessed", "-E", "-"],
                         input=src, capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError("%s -fpreprocessed -E failed: %s"
                           % (cc, out.stderr.strip()[:200]))
    return out.stdout


def norm(text):
    return re.sub(r"\s+", " ", text).strip()


def body(code, name):
    """The brace-matched body of the function definition `name`, or ''."""
    m = re.search(r"scpi_result_t\s+" + re.escape(name) + r"\s*\([^)]*\)\s*\{", code)
    if not m:
        return ""
    i = m.end() - 1
    depth = 0
    for j in range(i, len(code)):
        if code[j] == "{":
            depth += 1
        elif code[j] == "}":
            depth -= 1
            if depth == 0:
                return code[i + 1:j]
    return ""


ENTER = "taskENTER_CRITICAL()"
EXIT = "taskEXIT_CRITICAL()"
SET_TRUE = re.compile(r"->\s*enable\s*=\s*true\b")


def in_critical(b, pos):
    """pos lies after an ENTER with no EXIT between them, and before an EXIT."""
    enter = b.rfind(ENTER, 0, pos)
    if enter < 0 or b.rfind(EXIT, enter, pos) >= 0:
        return False, -1
    return b.find(EXIT, pos) >= 0, enter


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", default=DEFAULT_SOURCE)
    a = ap.parse_args()
    code = norm(strip_comments(open(a.source, encoding="utf-8", errors="replace").read()))

    print("check_1217_mount_gate: %s" % a.source)

    # ---- the two mount triggers stay the only ones
    sets = [m.start() for m in SET_TRUE.finditer(code)]
    check(len(sets) == 1, "`enable = true` is written exactly once in the file (found %d)" % len(sets))
    space_arms = len(re.findall(r"mode\s*=\s*SD_CARD_MANAGER_MODE_GET_SPACE\b", code))
    check(space_arms == 1, "GET_SPACE is armed exactly once in the file (found %d)" % space_arms)

    # ---- SD:ENAble 1
    en = body(code, "SCPI_StorageSDEnableSet")
    check(bool(en), "SCPI_StorageSDEnableSet found")
    m = SET_TRUE.search(en)
    check(m is not None, "SD:ENAble writes `enable = true`")
    if m:
        ok, enter = in_critical(en, m.start())
        check(ok, "SD:ENAble's `enable = true` is inside a critical section")
        guard = en.rfind("if (!DRV_SDSPI_CrcModePending(0))", 0, m.start())
        check(enter >= 0 and guard > enter,
              "...and under `if (!DRV_SDSPI_CrcModePending(0))` inside that section")

    # ---- SD:PROTect
    pr = body(code, "SCPI_StorageSDProtectSet")
    check(bool(pr), "SCPI_StorageSDProtectSet found")
    claim = pr.find("sd_card_manager_TryClaim()")
    setmode = pr.find("DRV_SDSPI_SetCrcMode(")
    check(pr.count("DRV_SDSPI_SetCrcMode(") == 1, "SD:PROTect makes its request at exactly one site")
    check(claim >= 0 and setmode > claim, "SD:PROTect takes the manager claim before making the request")
    if setmode >= 0:
        ok, enter = in_critical(pr, setmode)
        check(ok, "SD:PROTect's request is inside a critical section")
        echk = pr.rfind("->enable", enter if enter >= 0 else 0, setmode)
        check(enter >= 0 and echk > enter,
              "...and that same section checks `enable` before the request")
        rel = pr.find("sd_card_manager_ReleaseClaim()", setmode)
        check(rel > setmode, "SD:PROTect releases the claim after the request")

    # ---- SD:SPACe?
    sp = body(code, "SCPI_StorageSDSpaceGet")
    check(bool(sp), "SCPI_StorageSDSpaceGet found")
    cl = sp.find('SD_ClaimOrRefuse(context, "SPACe")')
    pend = sp.find("DRV_SDSPI_CrcModePending(0)")
    arm = sp.find("mode = SD_CARD_MANAGER_MODE_GET_SPACE")
    check(cl >= 0 and pend > cl and arm > pend,
          "SD:SPACe? checks for a pending request after its claim and before arming GET_SPACE")

    if fails:
        print("check_1217_mount_gate: FAIL (%d)" % len(fails))
        return 1
    print("check_1217_mount_gate: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
