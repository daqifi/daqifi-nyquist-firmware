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


def main():
    raw = SRC.read_text()
    src = strip_comments(raw)
    check('#include "Util/WifiSettingsStage.h"' in src,
          "SCPILAN.c includes Util/WifiSettingsStage.h")
    for name in ("SCPI_LANSettingsLoad", "SCPI_LANSettingsFactoryLoad"):
        body = body_of(src, name)
        calls = re.findall(
            r"WifiSettingsStage_ApplyThenStage\s*\(\s*pRunTimeWifiSettings\s*,"
            r"\s*&daqifiSettings\.settings\.wifi\s*,"
            r"\s*wifi_manager_UpdateNetworkSettings\s*\)", body)
        check(len(calls) == 1,
              "%s stages through the helper from its OWN loaded struct into "
              "the runtime copy (found %d call)" % (name, len(calls)))
        check("wifi_manager_UpdateNetworkSettings(" not in body,
              "%s has no direct UpdateNetworkSettings call left" % name)
    print()
    if fails:
        print("FAILED: %d check(s)" % len(fails))
        return 1
    print("OK: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
