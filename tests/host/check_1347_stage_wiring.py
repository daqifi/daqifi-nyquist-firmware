#!/usr/bin/env python3
"""Pin that #1347's staging helper is WIRED into both handlers.

test_1347_wifi_settings_stage.c proves WifiSettingsStage.h decides correctly.
That is a different claim from SCPI_LANSettingsLoad / SCPI_LANSettingsFactoryLoad
using it for the `1` (apply) argument, which is the half that was missing.

For each handler body, extracted by brace balance (comments stripped first so
a comment naming the helper cannot satisfy a check):
  1. it includes the header and calls WifiSettingsStage_ApplyThenStage with
     wifi_manager_UpdateNetworkSettings as the update, and the loaded struct
     (daqifiSettings.settings.wifi) as the source -- never the live copy;
  2. it no longer calls wifi_manager_UpdateNetworkSettings directly (a direct
     call is the old, runtime-stale path).

Run: python3 tests/host/check_1347_stage_wiring.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "firmware/src/services/SCPI/SCPILAN.c"
fails = []


def check(ok, msg):
    print(("  [OK]   " if ok else "  [FAIL] ") + msg)
    if not ok:
        fails.append(msg)


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), s, flags=re.S)
    return re.sub(r"//[^\n]*", "", s)


def body_of(src, name):
    i = src.index("scpi_result_t " + name + "(")
    j = src.index("{", i)
    depth = 0
    for k in range(j, len(src)):
        if src[k] == "{":
            depth += 1
        elif src[k] == "}":
            depth -= 1
            if depth == 0:
                return src[j:k + 1]
    raise AssertionError("unbalanced braces in " + name)


def arm_of(body, kw):
    """Text of the brace-balanced block that follows `if (applySettings)` (kw
    'if') or its `else`. None if absent."""
    m = re.search(r"if\s*\(\s*applySettings\s*\)\s*\{", body)
    if not m:
        return None
    def block(start):
        depth = 0
        for k in range(start, len(body)):
            if body[k] == "{":
                depth += 1
            elif body[k] == "}":
                depth -= 1
                if depth == 0:
                    return k
        return None
    j = m.end() - 1
    e = block(j)
    if kw == "if":
        return body[j:e + 1]
    m2 = re.match(r"\s*else\s*\{", body[e + 1:])
    if not m2:
        return None
    j2 = e + 1 + m2.end() - 1
    return body[j2:block(j2) + 1]


HELPER = (r"WifiSettingsStage_ApplyThenStage\s*\(\s*pRunTimeWifiSettings\s*,"
          r"\s*&daqifiSettings\.settings\.wifi\s*,"
          r"\s*wifi_manager_UpdateNetworkSettings\s*\)")


def handler_problems(name, body):
    """Every wiring problem in one handler body (comments already stripped).
    Pure text -> list of strings, so the mutants below can feed wrong shapes."""
    p = []
    if_arm = arm_of(body, "if")
    else_arm = arm_of(body, "else")
    if if_arm is None:
        return ["%s: no `if (applySettings)` arm" % name]
    n_if = len(re.findall(HELPER, if_arm))
    if n_if != 1:
        p.append("%s: helper call inside the applySettings arm found %d times, "
                 "want 1" % (name, n_if))
    if len(re.findall(HELPER, body)) != 1:
        p.append("%s: helper call appears %d times in the handler, want 1"
                 % (name, len(re.findall(HELPER, body))))
    if else_arm is not None and re.search(r"WifiSettingsStage_|"
                                          r"wifi_manager_UpdateNetworkSettings",
                                          else_arm):
        p.append("%s: the no-argument arm touches the helper/update" % name)
    if "wifi_manager_UpdateNetworkSettings(" in body:
        p.append("%s: direct UpdateNetworkSettings call present" % name)
    if name == "SCPI_LANSettingsFactoryLoad":
        z = re.search(r"memset\s*\(\s*&daqifiSettings\s*,\s*0\s*,\s*"
                      r"sizeof\s*\(?\s*daqifiSettings\s*\)?\s*\)", body)
        ld = body.find("daqifi_settings_LoadFactoryDeafult")
        if not z:
            p.append("%s: daqifiSettings is not zeroed" % name)
        elif ld < 0 or z.start() > ld:
            p.append("%s: the memset is not BEFORE LoadFactoryDeafult" % name)
    return p


def main():
    raw = SRC.read_text()
    src = strip_comments(raw)
    check('#include "Util/WifiSettingsStage.h"' in src,
          "SCPILAN.c includes Util/WifiSettingsStage.h")
    for name in ("SCPI_LANSettingsLoad", "SCPI_LANSettingsFactoryLoad"):
        probs = handler_problems(name, body_of(src, name))
        check(not probs, "%s wiring: %s" % (name, "ok" if not probs else "; ".join(probs)))
    print()
    if fails:
        print("FAILED: %d check(s)" % len(fails))
        return 1
    print("OK: all checks passed")
    return 0


def mutants():
    """Feed each wrong shape to handler_problems; every one must be reported."""
    src = strip_comments(SRC.read_text())
    lo = body_of(src, "SCPI_LANSettingsLoad")
    fa = body_of(src, "SCPI_LANSettingsFactoryLoad")
    assert not handler_problems("SCPI_LANSettingsLoad", lo)
    assert not handler_problems("SCPI_LANSettingsFactoryLoad", fa)
    call = re.search(HELPER, fa).group(0)
    memset = re.search(r"memset\([^;]*;", fa).group(0)
    cases = {
        "memset removed": ("SCPI_LANSettingsFactoryLoad", fa.replace(memset, "")),
        "memset after the load": ("SCPI_LANSettingsFactoryLoad", fa.replace(memset, "").replace(
            "return SCPI_RES_OK;\n}", memset + "\n    return SCPI_RES_OK;\n}")),
        "old direct call restored": ("SCPI_LANSettingsLoad", lo.replace(
            call, "wifi_manager_UpdateNetworkSettings(&daqifiSettings.settings.wifi)")),
        "helper moved to the else arm": ("SCPI_LANSettingsLoad", lo.replace(
            call, "0").replace("memcpy(pRunTimeWifiSettings,", call + "; memcpy(pRunTimeWifiSettings,")),
        "live copy as the source": ("SCPI_LANSettingsLoad", lo.replace(
            "&daqifiSettings.settings.wifi,\n                wifi_manager_Up",
            "BoardData_Get(BOARDDATA_WIFI_SETTINGS, 0),\n                wifi_manager_Up")),
    }
    bad = 0
    for label, (name, text) in cases.items():
        assert text != (lo if name.endswith("Load") and "Factory" not in name else fa), \
            "mutant %r did not change the text" % label
        probs = handler_problems(name, text)
        print("  [%s] mutant %-30s -> %s" % ("RED " if probs else "GREEN!", label,
                                              probs[0] if probs else "NOT CAUGHT"))
        bad += 0 if probs else 1
    return 1 if bad else 0


if __name__ == "__main__":
    if "--mutants" in sys.argv:
        sys.exit(mutants())
    sys.exit(main())
