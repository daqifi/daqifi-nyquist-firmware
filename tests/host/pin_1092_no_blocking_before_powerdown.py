#!/usr/bin/env python3
"""#1092 r4b -- nothing that can block unboundedly may run on the shutdown pass.

WHY A CALL-GRAPH PIN AND NOT A GREP. r4 hoisted Power_UpdateStatusFromGPIO()
to Power_HandleStandbyState's entry to get a fresh VBUS reading, and that put
BQ24297_Read_I2C(REG08) ahead of Power_Down(). That read takes the I2C mutex
with portMAX_DELAY and then waits on a blocking Harmony transfer, so a stuck
bus leaves the rails and the MCU self-hold ON -- the very failure this row is
about, reintroduced by its own fix.

The name of the hoisted function says nothing about that. `UpdateStatusFromGPIO`
reads as cheap and local; the I2C is two calls down. So this pin derives the
dangerous set from the CALLEES, transitively, and a wrapper renamed or
introduced in between is caught because it inherits its callees' reachability.

THE PROPERTY: at statement depth 1 of Power_HandleStandbyState -- i.e. the
calls that run UNCONDITIONALLY on every pass, including the shutdown pass --
no called function may transitively reach a blocking sink.

Depth 1 is the honest scope, and the limitation is stated rather than papered
over: a blocking call nested inside a conditional branch is NOT flagged,
because deciding whether such a branch lies on the path to Power_Down() needs
real path analysis. Depth 1 is exactly the shape of the r4 regression (a hoist
to the top of the function) and of any future hoist, which is what this guards.
The power-UP arm legitimately calls the full refresh at depth 2; that call is
fine because it powers up rather than down, so a stuck bus delays a power-up
instead of stranding a power-down.

FAILS CLOSED. If the source cannot be read, the function cannot be found, no
sinks can be identified, or the call graph comes back empty, this exits
non-zero. A pin that cannot find its subject must never report success -- that
is the one failure mode that would make every later green meaningless.

Usage:
    pin_1092_no_blocking_before_powerdown.py --power-src PATH --bq-src PATH
    pin_1092_no_blocking_before_powerdown.py --self-test
"""

import argparse
import re
import subprocess
import sys

# Direct sinks: a call to any of these is treated as potentially unbounded.
# BQ24297_{Read,Write}_I2C each take the I2C mutex with portMAX_DELAY before a
# blocking DRV_I2C transfer; the DRV_I2C_* entry points block in the driver.
SINK_PATTERNS = (
    r"BQ24297_Read_I2C",
    r"BQ24297_Write_I2C",
    r"DRV_I2C_\w+Transfer",
)
# A direct unbounded wait written inline, rather than reached through a sink.
INLINE_WAIT_PATTERNS = (
    r"xSemaphoreTake\s*\([^;]*portMAX_DELAY",
    r"OSAL_SEM_Pend\s*\([^;]*OSAL_WAIT_FOREVER",
    r"ulTaskNotifyTake\s*\([^;]*portMAX_DELAY",
)

GUARDED_FUNCTION = "Power_HandleStandbyState"
MUST_PRECEDE = "Power_Down"

# A C function definition at column 0: optional `static`, a return type, the
# name, an argument list, and the opening brace on the same line. Every
# function in PowerApi.c is written this way; a definition this misses simply
# contributes no edges, which the non-vacuity checks below then catch.
DEF_RE = re.compile(
    r"^(?:static\s+)?(?:inline\s+)?[A-Za-z_][\w\s\*]*?"
    r"\b(?P<name>[A-Za-z_]\w*)\s*\([^;{]*\)\s*\{", re.MULTILINE)
CALL_RE = re.compile(r"\b(?P<name>[A-Za-z_]\w*)\s*\(")
# Keywords that look like calls but are not.
NOT_CALLS = {
    "if", "for", "while", "switch", "return", "sizeof", "do", "else",
    "case", "defined", "offsetof", "va_start", "va_end",
}


def strip_comments(src, cc="cc"):
    """Remove comments without expanding includes, preserving line geometry.

    `-fpreprocessed` stops the preprocessor from chasing headers, so this is
    the file's own text with comments gone. Comment stripping is mandatory,
    not cosmetic: the prose in PowerApi.c NAMES BQ24297_Read_I2C repeatedly
    (that is how the defect is explained), so a scan over raw text would find
    sinks in the explanation and flag the fixed code.
    """
    try:
        out = subprocess.run([cc, "-fpreprocessed", "-E", "-"],
                             input=src, capture_output=True, text=True)
    except (OSError, subprocess.SubprocessError) as exc:
        raise RuntimeError("could not run %s to strip comments: %s" % (cc, exc))
    if out.returncode != 0:
        raise RuntimeError("%s -fpreprocessed -E failed: %s"
                           % (cc, out.stderr.strip()[:200]))
    return out.stdout


def function_bodies(src):
    """Map each function name -> its body text, by brace matching."""
    bodies = {}
    for m in DEF_RE.finditer(src):
        name = m.group("name")
        if name in NOT_CALLS:
            continue
        i = src.index("{", m.start())
        depth, j = 0, i
        while j < len(src):
            if src[j] == "{":
                depth += 1
            elif src[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        bodies[name] = src[i + 1:j]
    return bodies


def calls_in(body):
    return {m.group("name") for m in CALL_RE.finditer(body)
            if m.group("name") not in NOT_CALLS}


def is_sink(name):
    return any(re.fullmatch(p, name) for p in SINK_PATTERNS)


def has_inline_wait(body):
    return any(re.search(p, body) for p in INLINE_WAIT_PATTERNS)


def reaching_set(bodies, extra_sink_names):
    """Functions that transitively reach a blocking sink or an inline wait."""
    direct = set()
    for name, body in bodies.items():
        callees = calls_in(body)
        if any(is_sink(c) or c in extra_sink_names for c in callees):
            direct.add(name)
        elif has_inline_wait(body):
            direct.add(name)
    reaching = set(direct)
    changed = True
    while changed:                      # transitive closure
        changed = False
        for name, body in bodies.items():
            if name in reaching:
                continue
            if calls_in(body) & reaching:
                reaching.add(name)
                changed = True
    return reaching


def region_before(body, callee):
    """The body text up to the first call of `callee`, or None if absent.

    ⛔ THE REGION IS THE PROPERTY. An earlier revision of this file scanned
    the WHOLE function body, which contradicted this file's own stated
    property ("runs AHEAD of Power_Down") and produced a false positive on
    main: Power_UpdateStatusIfNeeded() is called unconditionally there, but
    AFTER Power_Down() has already executed, where an unbounded wait cannot
    strand a shutdown. Scanning past the sink being protected reports calls
    that are not on the path to it.
    """
    m = re.search(r"\b" + re.escape(callee) + r"\s*\(", body)
    if m is None:
        return None
    return body[:m.start()]


def depth1_calls(body):
    """Calls that run unconditionally: brace depth 0 within the body.

    Tracks parenthesis depth too, so a call written as an argument to another
    call still counts, and a call inside `if (...)`'s condition is attributed
    at the depth of the `if` -- which is correct, since the condition is
    evaluated unconditionally.
    """
    out, depth, i = [], 0, 0
    while i < len(body):
        ch = body[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        elif depth == 0:
            m = CALL_RE.match(body, i)
            if m and m.group("name") not in NOT_CALLS:
                out.append(m.group("name"))
                i = m.end()
                continue
        i += 1
    return out


def check(power_src_text, bq_src_text, cc="cc"):
    """Returns a list of error strings; empty means the property holds."""
    errs = []
    power = strip_comments(power_src_text, cc)
    bodies = function_bodies(power)

    # --- non-vacuity, before any verdict ---------------------------------
    if len(bodies) < 5:
        return ["extracted only %d function bodies from the power source -- "
                "this pin is measuring nothing" % len(bodies)]
    if GUARDED_FUNCTION not in bodies:
        return ["%s not found in the power source; it may have been renamed. "
                "This pin cannot measure its property and will not pass."
                % GUARDED_FUNCTION]

    # Sinks named in the BQ driver itself, so a rename there is picked up
    # rather than silently dropping the sink set to empty.
    bq = strip_comments(bq_src_text, cc)
    bq_defs = set(function_bodies(bq))
    bq_sinks = {n for n in bq_defs if is_sink(n)}
    if not bq_sinks:
        return ["no blocking sink found in the BQ24297 source. The sink names "
                "this pin keys on (%s) are gone or renamed; the reaching set "
                "would be empty and every check below vacuous."
                % ", ".join(SINK_PATTERNS)]

    reaching = reaching_set(bodies, bq_sinks)
    if not reaching:
        return ["no function in the power source reaches a blocking sink. "
                "PowerApi.c is known to read the BQ over I2C, so an empty "
                "reaching set means the call graph was not built."]

    body = bodies[GUARDED_FUNCTION]
    if MUST_PRECEDE not in calls_in(body):
        errs.append("%s does not call %s. The property this pin states is "
                    "about the shutdown path; without that call the scope is "
                    "unknown, so this is a failure, not a pass."
                    % (GUARDED_FUNCTION, MUST_PRECEDE))

    # Only the stretch that precedes the shutdown itself: a blocking call
    # after Power_Down() has run cannot strand it.
    region = region_before(body, MUST_PRECEDE)
    if region is None:
        return errs + ["%s's body has no %s() call to bound the checked "
                       "region; failing closed." % (GUARDED_FUNCTION,
                                                    MUST_PRECEDE)]
    unconditional = depth1_calls(region)
    for name in unconditional:
        if is_sink(name) or name in bq_sinks:
            errs.append("%s calls the blocking sink %s unconditionally."
                        % (GUARDED_FUNCTION, name))
        elif name in reaching:
            errs.append(
                "%s calls %s unconditionally, and %s transitively reaches a "
                "blocking sink (I2C mutex with portMAX_DELAY, then a blocking "
                "transfer). On the shutdown pass that runs AHEAD of %s(), so a "
                "stuck bus leaves the rails and the MCU self-hold ON. Refresh "
                "VBUS from the PHY directly instead -- see "
                "Power_RefreshVbusFromPhy()."
                % (GUARDED_FUNCTION, name, name, MUST_PRECEDE))
    return errs


# ---------------------------------------------------------------------------
# Self-test. The passing fixture is not evidence until the failing one exists,
# so every arm below is paired: a shape that must be accepted and the matching
# shape that must be rejected, for the same reason.
# ---------------------------------------------------------------------------
BQ_FIXTURE = """
bool BQ24297_Read_I2C(uint8_t reg, uint8_t *out) {
    xSemaphoreTake(i2cMutex, portMAX_DELAY);
    bool ok = DRV_I2C_WriteReadTransfer(h, a, b, 1, c, 1);
    return ok;
}
bool BQ24297_Write_I2C(uint8_t reg, uint8_t v) {
    xSemaphoreTake(i2cMutex, portMAX_DELAY);
    return DRV_I2C_WriteTransfer(h, a, b, 2);
}
"""

POWER_FIXED = """
static void Power_UpdateChgPct(void) { adc_read(); }
static void Power_RefreshVbusFromPhy(void) {
    pData->BQ24297Data.status.pgStat = PLIB_USBHS_VBUSLevelGet(USBHS_ID_0);
}
static void Power_UpdateStatusFromGPIO(void) {
    Power_UpdateChgPct();
    Power_RefreshVbusFromPhy();
    uint8_t r;
    if (pData->BQ24297Data.initComplete) { BQ24297_Read_I2C(0x08, &r); }
}
void Power_Down(void) { rails_off(); }
static void Power_HandleStandbyState(void) {
    Power_RefreshVbusFromPhy();
    if (!pData->BQ24297Data.status.pgStat) { latch = false; }
    if (req == DO_POWER_UP) {
        Power_UpdateStatusFromGPIO();
        if (Power_HasSufficientPower()) { Power_Up(true); return; }
    }
    if (shutdownNotified) { Power_Down(); }
}
"""

# The r4 defect: the full refresh hoisted to the entry.
POWER_R4_DEFECT = POWER_FIXED.replace(
    "static void Power_HandleStandbyState(void) {\n    Power_RefreshVbusFromPhy();",
    "static void Power_HandleStandbyState(void) {\n    Power_UpdateStatusFromGPIO();")

# A renamed wrapper: the name gives nothing away, the callees do.
POWER_WRAPPER = POWER_FIXED.replace(
    "static void Power_HandleStandbyState(void) {\n    Power_RefreshVbusFromPhy();",
    "static void Power_CheapLocalPeek(void) { Power_UpdateStatusFromGPIO(); }\n"
    "static void Power_HandleStandbyState(void) {\n    Power_CheapLocalPeek();")

# The sink called straight from the handler.
POWER_DIRECT = POWER_FIXED.replace(
    "static void Power_HandleStandbyState(void) {\n    Power_RefreshVbusFromPhy();",
    "static void Power_HandleStandbyState(void) {\n"
    "    uint8_t rr; BQ24297_Read_I2C(0x08, &rr);")

# An inline unbounded wait, reaching no named sink at all.
POWER_INLINE_WAIT = POWER_FIXED.replace(
    "static void Power_RefreshVbusFromPhy(void) {",
    "static void Power_RefreshVbusFromPhy(void) {\n"
    "    xSemaphoreTake(someMutex, portMAX_DELAY);")

# Comments must not be scanned: this prose names the sink, as the real file's
# does, and the code is correct.
POWER_PROSE = POWER_FIXED.replace(
    "static void Power_HandleStandbyState(void) {",
    "/* Must never call BQ24297_Read_I2C or DRV_I2C_WriteReadTransfer here,\n"
    " * and must not take a mutex with portMAX_DELAY. */\n"
    "static void Power_HandleStandbyState(void) {")

# main's shape: an unconditional blocking call AFTER Power_Down(). Must be
# ACCEPTED. This is the control for the region bound itself -- without it, a
# whole-body scan passes every other arm in this list and still cannot be
# switched on, because it rejects the baseline.
POWER_AFTER_PD = POWER_FIXED.replace(
    "    if (shutdownNotified) { Power_Down(); }",
    "    if (shutdownNotified) { Power_Down(); }\n"
    "    Power_UpdateStatusFromGPIO();")

# Non-vacuity: the guarded function renamed away.
POWER_RENAMED = POWER_FIXED.replace("Power_HandleStandbyState", "Power_Standby2")
# Non-vacuity: no Power_Down call on the path.
POWER_NO_PD = POWER_FIXED.replace("if (shutdownNotified) { Power_Down(); }", "")


def self_test(cc="cc"):
    cases = [
        ("the fixed shape is accepted", POWER_FIXED, BQ_FIXTURE, None),
        ("prose naming the sink does not trip it", POWER_PROSE, BQ_FIXTURE, None),
        ("an unconditional blocking call AFTER Power_Down is accepted",
         POWER_AFTER_PD, BQ_FIXTURE, None),
        ("r4's hoisted full refresh is rejected", POWER_R4_DEFECT, BQ_FIXTURE,
         "transitively reaches"),
        ("a renamed wrapper is rejected by its callees", POWER_WRAPPER,
         BQ_FIXTURE, "transitively reaches"),
        ("the sink called directly is rejected", POWER_DIRECT, BQ_FIXTURE,
         "blocking sink"),
        ("an inline portMAX_DELAY wait is rejected", POWER_INLINE_WAIT,
         BQ_FIXTURE, "transitively reaches"),
        ("a renamed guarded function fails closed", POWER_RENAMED, BQ_FIXTURE,
         "not found"),
        ("no Power_Down on the path fails closed", POWER_NO_PD, BQ_FIXTURE,
         "does not call"),
        ("sinks gone from the driver fails closed", POWER_FIXED,
         "bool Unrelated_Thing(void) { return 1; }", "no blocking sink"),
    ]
    bad = 0
    for label, power, bq, want in cases:
        errs = check(power, bq, cc)
        if want is None:
            if errs:
                print("  FAIL %s -- expected clean, got: %s" % (label, errs[0]))
                bad += 1
        elif not errs:
            print("  FAIL %s -- expected a rejection, got clean" % label)
            bad += 1
        elif not any(want in e for e in errs):
            print("  FAIL %s -- rejected for the wrong reason: %s"
                  % (label, errs[0]))
            bad += 1
    if bad:
        print("pin_1092 self-test: %d/%d FAILED" % (bad, len(cases)))
        return 1
    print("pin_1092 self-test: %d/%d cases pass" % (len(cases), len(cases)))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--power-src")
    ap.add_argument("--bq-src")
    ap.add_argument("--cc", default="cc")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test(a.cc)
    if not a.power_src or not a.bq_src:
        print("ERROR: --power-src and --bq-src are required.")
        return 2
    try:
        power = open(a.power_src, newline="").read()
        bq = open(a.bq_src, newline="").read()
    except OSError as exc:
        print("ERROR: #1092 pin could not read its source: %s" % exc)
        return 2
    try:
        errs = check(power, bq, a.cc)
    except RuntimeError as exc:
        print("ERROR: #1092 pin could not run: %s" % exc)
        return 2
    if errs:
        for e in errs:
            print("ERROR: #1092 %s" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
