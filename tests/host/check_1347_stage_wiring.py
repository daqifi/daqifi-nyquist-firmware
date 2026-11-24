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
    if else_arm is not None and re.search(r"WifiSettingsStage_ApplyThenStage|"
                                          r"wifi_manager_UpdateNetworkSettings",
                                          else_arm):
        p.append("%s: the no-argument arm touches the helper/update" % name)
    if "wifi_manager_UpdateNetworkSettings(" in body:
        p.append("%s: direct UpdateNetworkSettings call present" % name)
    if "memcpy(" in body:
        p.append("%s: a bare memcpy is left in the handler (the no-argument arm "
                 "must use WifiSettingsStage_Overwrite)" % name)
    if else_arm is not None:
        n_ow = len(re.findall(
            r"WifiSettingsStage_Overwrite\s*\(\s*pRunTimeWifiSettings\s*,"
            r"\s*&daqifiSettings\.settings\.wifi\s*\)", else_arm))
        if n_ow != 1:
            p.append("%s: the no-argument arm calls WifiSettingsStage_Overwrite "
                     "%d times, want 1" % (name, n_ow))
    if name == "SCPI_LANSettingsFactoryLoad":
        z = re.search(r"memset\s*\(\s*&daqifiSettings\s*,\s*0\s*,\s*"
                      r"sizeof\s*\(?\s*daqifiSettings\s*\)?\s*\)", body)
        ld = body.find("daqifi_settings_LoadFactoryDeafult")
        if not z:
            p.append("%s: daqifiSettings is not zeroed" % name)
        elif ld < 0 or z.start() > ld:
            p.append("%s: the memset is not BEFORE LoadFactoryDeafult" % name)
    return p


def snapshot_problems(name, body, runtime_expr):
    """SAVE and APPLY 1: the runtime snapshot goes through the helper, exactly
    once, and no bare memcpy is left in the handler."""
    p = []
    n = len(re.findall(
        r"WifiSettingsStage_Snapshot\s*\(\s*&daqifiSettings\.settings\.wifi\s*,"
        r"\s*" + runtime_expr + r"\s*\)", body))
    if n != 1:
        p.append("%s: WifiSettingsStage_Snapshot(&daqifiSettings.settings.wifi, %s) "
                 "found %d times, want 1" % (name, runtime_expr, n))
    if "memcpy(" in body:
        p.append("%s: a bare memcpy is left in the handler" % name)
    return p


def header_problems(hdr):
    """Every critical section in WifiSettingsStage.h exists and holds only
    memcpy / plain assignment: no other call (an NVM call, a log, a lookup) may
    sit inside it."""
    p = []
    h = strip_comments(hdr)
    for fn in ("WifiSettingsStage_IntoRuntime", "WifiSettingsStage_Snapshot",
               "WifiSettingsStage_Overwrite"):
        m = re.search(r"static inline void " + fn + r"\([^)]*\)\s*\{", h)
        if not m:
            p.append("%s: missing" % fn)
            continue
        depth, k = 0, m.end() - 1
        for k in range(m.end() - 1, len(h)):
            if h[k] == "{":
                depth += 1
            elif h[k] == "}":
                depth -= 1
                if depth == 0:
                    break
        body = h[m.end():k]
        sec = re.search(r"taskENTER_CRITICAL\(\);(.*?)taskEXIT_CRITICAL\(\);",
                        body, re.S)
        if not sec:
            p.append("%s: no taskENTER_CRITICAL/taskEXIT_CRITICAL section" % fn)
            continue
        inner = re.sub(r"memcpy\s*\(", "", sec.group(1))
        inner = re.sub(r"sizeof\s*\(", "", inner)
        calls = re.findall(r"\b[A-Za-z_]\w*\s*\(", inner)
        if calls:
            p.append("%s: something other than memcpy is inside the critical "
                     "section: %s" % (fn, ", ".join(c.strip() for c in calls)))
    return p


HDR = ROOT / "firmware/src/Util/WifiSettingsStage.h"


def all_problems(src, hdr):
    out = []
    for name in ("SCPI_LANSettingsLoad", "SCPI_LANSettingsFactoryLoad"):
        out += handler_problems(name, body_of(src, name))
    out += snapshot_problems("SCPI_LANSettingsSave", body_of(src, "SCPI_LANSettingsSave"),
                             "pWifiSettings")
    out += snapshot_problems("SCPI_LANSettingsApply", body_of(src, "SCPI_LANSettingsApply"),
                             "pRunTimeWifiSettings")
    out += header_problems(hdr)
    return out


def main():
    src = strip_comments(SRC.read_text())
    hdr = HDR.read_text()
    check('#include "Util/WifiSettingsStage.h"' in src,
          "SCPILAN.c includes Util/WifiSettingsStage.h")
    probs = all_problems(src, hdr)
    check(not probs, "wiring (Load, FactoryLoad, Save, Apply, header sections): "
          + ("ok" if not probs else "; ".join(probs)))
    print()
    if fails:
        print("FAILED: %d check(s)" % len(fails))
        return 1
    print("OK: all checks passed")
    return 0


def mutants():
    """Feed each wrong shape; every one must be reported."""
    src = strip_comments(SRC.read_text())
    hdr = HDR.read_text()
    assert not all_problems(src, hdr), all_problems(src, hdr)
    fa = body_of(src, "SCPI_LANSettingsFactoryLoad")
    lo = body_of(src, "SCPI_LANSettingsLoad")
    call = re.search(HELPER, fa).group(0)
    memset = re.search(r"memset\([^;]*;", fa).group(0)

    def swap(text, a, b):
        assert a in text, a
        return text.replace(a, b, 1)
    bare_ow = ("memcpy(pRunTimeWifiSettings, &daqifiSettings.settings.wifi, "
               "sizeof(wifi_manager_settings_t))")
    snap_save = "WifiSettingsStage_Snapshot(&daqifiSettings.settings.wifi, pWifiSettings)"
    snap_apply = ("WifiSettingsStage_Snapshot(&daqifiSettings.settings.wifi, "
                  "pRunTimeWifiSettings)")
    cases = {}
    cases["memset removed"] = swap(src, memset, "")
    cases["old direct call restored"] = swap(src, re.search(HELPER, lo).group(0),
        "wifi_manager_UpdateNetworkSettings(&daqifiSettings.settings.wifi)")
    cases["live copy as the source"] = swap(src,
        "&daqifiSettings.settings.wifi,\n                wifi_manager_UpdateNetworkSettings)",
        "BoardData_Get(BOARDDATA_WIFI_SETTINGS, 0),\n                wifi_manager_UpdateNetworkSettings)") \
        if "&daqifiSettings.settings.wifi,\n                wifi_manager_UpdateNetworkSettings)" in src else None
    cases["SAVE: helper replaced by bare memcpy"] = swap(src, snap_save,
        "memcpy(&daqifiSettings.settings.wifi, pWifiSettings, sizeof(wifi_manager_settings_t))")
    cases["APPLY 1: helper replaced by bare memcpy"] = swap(src, snap_apply,
        "memcpy(&daqifiSettings.settings.wifi, pRunTimeWifiSettings, sizeof(wifi_manager_settings_t))")
    ow = "WifiSettingsStage_Overwrite(pRunTimeWifiSettings, &daqifiSettings.settings.wifi)"
    first = src.index(ow)
    second = src.index(ow, first + 1)
    cases["LOAD no-arg arm: helper replaced by bare memcpy"] = src[:first] + bare_ow + src[first + len(ow):]
    cases["FACRESET no-arg arm: helper replaced by bare memcpy"] = src[:second] + bare_ow + src[second + len(ow):]
    bad = 0
    for label, text in cases.items():
        if text is None:
            continue
        probs = all_problems(text, hdr)
        print("  [%s] mutant %-52s -> %s" % ("RED " if probs else "GREEN!", label,
                                              probs[0][:90] if probs else "NOT CAUGHT"))
        bad += 0 if probs else 1
    hmut = {
        "critical section dropped from Snapshot": (
            "    taskENTER_CRITICAL();\n    memcpy(dst, runtime, sizeof(*dst));\n    taskEXIT_CRITICAL();",
            "    memcpy(dst, runtime, sizeof(*dst));"),
        "NVM call moved inside the Snapshot section": (
            "    memcpy(dst, runtime, sizeof(*dst));\n    taskEXIT_CRITICAL();",
            "    memcpy(dst, runtime, sizeof(*dst));\n    daqifi_settings_SaveToNvm(0);\n    taskEXIT_CRITICAL();"),
        "critical section dropped from Overwrite": (
            "    taskENTER_CRITICAL();\n    memcpy(runtime, loaded, sizeof(*runtime));\n    taskEXIT_CRITICAL();",
            "    memcpy(runtime, loaded, sizeof(*runtime));"),
    }
    h = hdr.replace("\r\n", "\n")
    for label, (a, b) in hmut.items():
        text = swap(h, a, b)
        probs = all_problems(src, text)
        print("  [%s] mutant %-52s -> %s" % ("RED " if probs else "GREEN!", label,
                                              probs[0][:90] if probs else "NOT CAUGHT"))
        bad += 0 if probs else 1
    return 1 if bad else 0


if __name__ == "__main__":
    if "--mutants" in sys.argv:
        sys.exit(mutants())
    sys.exit(main())
