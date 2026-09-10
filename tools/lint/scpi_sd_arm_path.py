#!/usr/bin/env python3
"""A refused SD arm must undo what the caller published, UNDER the claim.

## What this protects

Every SD entry point in `SCPIStorageSD.c` follows one shape (#829): take the
manager's claim (`SD_ClaimOrRefuse` -> `sd_card_manager_TryClaim`), write the
operands, write `mode` LAST, then arm through `SD_ArmOrRefuseWithCleanup`,
which releases the claim on both of its paths. Two merged fixes then moved two
writes INTO that helper, and both are ordering properties:

* **#955** moved the `mode = SD_CARD_MANAGER_MODE_NONE` clear on a refused arm
  from the caller (which ran it after the helper had already released) to
  inside the helper, before the release.
* **#964** did the same for FORmat's format-pending retraction, by passing
  `sd_card_manager_ClearFormatStatus` in as the helper's `onRefused` callback
  so it runs on the refusal path while the claim is still held.

Past the release, either write is unowned: the other SCPI transport (USB pri 7
preempts WiFi pri 2, no shared dispatch mutex) can claim, publish and arm its
own operation in the gap, and the late store then lands on THAT owner's state.

A future refactor can move either write back outside the claim and every test
that shipped with those PRs still passes. The race needs a preemption window
that cannot be aimed at from a client: single-threaded, the entry guard always
wins, so the companion tests (daqifi-python-test-suite#317) assert equivalence
and the new contract, not the ordering. #971 is that gap, and this is the same
answer `tools/lint/scpi_claim_path.py` gives for the streaming claim path --
check the property where it lives, in the source, for no bench time.

## The same shape at a second site, in a second file

#942 (PR #974) found it in `SCPI_StartStreamingClaimed` (`SCPIInterface.c`):
the streaming-log arm, `sd_card_manager_UpdateSettingsForStreamingLog`, with
its return DISCARDED. The #589 suspend pre-check ~50 lines above closes the
common case; what it cannot close is the LOST RACE -- a WiFi FW update, a
WiFi-streaming start on the other transport, or a bus-jam quarantine landing
between that check and the arm. On that path the callee's #589 gate puts `mode`
back to `MODE_NONE` and arms nothing, so `sd_card_manager_IsWriteReady()`
(which requires `mode == MODE_WRITE`) can never become true: the poll below
could only spend its full 5 s and then report "SD file not ready", blaming the
media for the SD task simply not running. The fix captures the verdict and
returns -- clearing `mode` under the claim, releasing, logging, pushing the
error -- before the poll is ever reached.

Qodo raised on that PR the objection #971 raises here: nothing automated stops
a future refactor from dropping the check, and the bench cannot see it, for the
same reason -- a losing arbitration cannot be aimed at from a client. So it is
checked here rather than in a file of its own. It is the SAME property (a
refused SD arm undoing what the caller published, under the claim) at a second
site, and one checker over two sites stays honest more easily than two
checkers over one site each.

## The five properties

1. **The helper holds the claim across the arm, and both refusal-path writes
   fall inside it AND run only on refusal.** In `SD_ArmOrRefuseWithCleanup`:
   the arm (`sd_card_manager_UpdateSettings`) precedes every
   `sd_card_manager_ReleaseClaim`; its verdict is consumed (captured into a
   variable or tested inline); the success path is an `if` gated on that
   verdict which RETURNS; and the `mode` clear and the `onRefused` invocation
   both fall after that block and before the refusal-path release. Caller-side:
   every function that arms calls `SD_ClaimOrRefuse` exactly once, before its
   arm.
2. **FORmat reaches its retraction THROUGH the callback parameter.**
   `SCPI_StorageSDFormat` publishes format-pending before arming, passes a
   non-NULL retraction in the callback slot, and does NOT also call that
   function at its own call site -- the shape #964 removed.
3. **The five commands with nothing published before the arm go through the
   `SD_ArmOrRefuse` wrapper**, which passes NULL. Exactly one call site uses
   the `WithCleanup` form, and it is FORmat's, so a future caller that
   publishes state before arming has to make a deliberate choice here rather
   than inherit silence.
4. **The streaming-log arm's verdict is consumed.** In
   `SCPI_StartStreamingClaimed` (`SCPIInterface.c`): the one call to
   `sd_card_manager_UpdateSettingsForStreamingLog` is captured into a variable
   or tested inline in an `if` condition -- not left as a bare expression
   statement, and not cast to `(void)`.
5. **Its refusal exits before the readiness poll, under the claim.** The branch
   that verdict gates contains a `return`, and the whole branch is positioned
   before the first `sd_card_manager_IsWriteReady()` call. Inside it the `mode`
   clear precedes the `sd_card_manager_ReleaseClaim()` and puts back the object
   that was armed; and the function takes the claim (`sd_card_manager_TryClaim`)
   exactly once, before the arm.

## Property 1 does not read the way #971 wrote it, and here is why

#971 asks for "exactly one `sd_card_manager_TryClaim`-side entry and one
`sd_card_manager_ReleaseClaim`" inside `SD_ArmOrRefuseWithCleanup`. Neither
count matches the C, and taking the sentence literally would have produced a
checker that fails on the correct tree:

* The helper **never calls `sd_card_manager_TryClaim`**. The claim is the
  CALLER's -- taken by `SD_ClaimOrRefuse` before the operands are written,
  because #829's whole point is that the claim precedes every shared write.
  The helper's opener is therefore its own entry, under a precondition it
  cannot see. So the checkable analogue is split in two: inside the helper,
  the absence of a `TryClaim` is asserted (its presence would mean the
  ownership model this reasons about had changed, so it is refused rather than
  interpreted); and at each ARM SITE, the enclosing function is required to
  call `SD_ClaimOrRefuse` exactly once, before the arm.
* The helper releases **twice**, once per path -- inside the success `if`, and
  again after the refusal-path writes. That is not two regions in the
  `scpi_claim_path.py` sense (two claims taken and dropped); it is one claim
  with two exits. One release is accepted too, for a single-exit restructure.
  Three or more is refused with the counts, because deciding which exit each
  belongs to needs control flow this does not have.

With two releases the refusal region is `(END of the success block, last
release)`, and with one release it is `(arm, release)`. Requiring every release
to follow the arm is what stops a release hoisted above the arm from
re-admitting both writes into a region that no longer holds anything.

## A region is not a path (Qodo, PR #976)

The first version of this file used `(FIRST release, last release)` as the
two-release region and asked nothing else. Lexical containment between two
calls is not "runs only on refusal", and two shapes slipped straight through
it -- both re-admitting exactly the bug #955/#964 fixed:

* **A write hoisted OUT of the failure guard**, so it runs unconditionally
  between the arm and the release. On a SUCCESSFUL arm it then clears the
  `mode` that arm just set, and retracts the caller's published state under an
  operation that is now running. Reproduced on a one-release miniature: zero
  problems reported.
* **A write inside the SUCCESS block, after that block's own release.** It is
  positionally "between the first and the last release" and is on the success
  path by construction.

Two things close them, and neither needs control flow:

* The arm's verdict has to be IDENTIFIABLE -- captured into a variable, or the
  arm call itself standing as an `if` condition. Without it there is nothing to
  say a branch tests, and the checker refuses rather than reporting on a shape
  where "success path" has no textual meaning.
* Then, per shape: with TWO releases, the block holding the first release must
  be gated on the verdict being TRUE and must contain a `return`, and the
  region starts at that block's END -- so everything in it is on the
  fall-through, which is the failure path only because the success path left.
  With ONE release there is no such exit, so each write must instead sit inside
  a block gated on the verdict being FALSE (`if (!armed)`, `armed == false`,
  `armed != true`, or the `else` of a true-gated `if`). Nesting counts: the
  `onRefused()` call inside its own `!= NULL` guard is confined by the
  failure-gated block that guard sits in, so the walk goes OUTWARD to function
  scope.

A condition that tests the verdict AND something else (`!armed && x`) is
deliberately UNRECOGNISED, not accepted: `&&` narrows the branch and `||`
widens it, the second is unsound, and telling them apart is the control flow
this file does not have. Refusing both reds loudly instead of passing quietly.

## Properties 4 and 5 do not read the way #942's follow-up wrote them

The directive was "the return must be CAPTURED into a variable, not left as a
bare expression statement", and "wherever that variable is checked in a
conditional leading to an early return, that return must precede the poll".
Two refinements, for the same reason property 1 has one:

* **An inline test is accepted.**
  `if (!sd_card_manager_UpdateSettingsForStreamingLog(cfg)) { ... }` captures
  nothing and consumes the verdict just as completely -- it is the spelling
  `SD_ArmOrRefuse`'s five callers use in the other file, and the spelling
  `SD_ArmOrRefuseWithCleanup` itself uses. Demanding the variable would red a
  correct restructure, which is the failure taking #971 literally would have
  produced here. What is refused is the verdict going NOWHERE: a bare
  statement, an explicit `(void)` cast (its own message -- a deliberate discard
  is still a discard, and naming it says which one happened), or a consumer
  this checker cannot recognise at all.
* **Property 5 also carries the clear-before-release pair and the claim.** The
  directive named neither. They are not additions: they are property 1 at the
  second site, and this file's subject is the word UNDER in its own title.
  Without them a branch that releases and THEN clears passes green while
  storing on the next owner's state, and a branch that never releases passes
  green while wedging the manager for the rest of the session -- both the class
  the gate exists for. `TryClaim`-before-arm is what makes "under the claim"
  mean anything here at all; it is the caller-side half property 1 already
  asserts for the five plain arm sites.

## Two files, two entry points

`check()` reads `SCPIStorageSD.c` (properties 1-3) and `check_stream()` reads
`SCPIInterface.c` (properties 4-5); `main()` requires BOTH and defaults the
second to `--interface`. Separate entry points rather than one call taking two
texts, because an optional second text is a vacuity hazard -- a run that
silently examines one site and reports a pass is the failure half this file's
self-test exists to refuse. Each fails if it cannot find its own function.

## What a green run does NOT mean

Textual, positional, no control flow and no reachability -- the same stated
limits as `scpi_claim_path.py`, tracked as #896, which this ticket does not
close.

* A dead branch satisfies these as readily as a live one. An `onRefused` call
  inside `if (0)`, or a `mode` clear in a branch that cannot be reached, is in
  the region as far as this is concerned.
* Property 1's "the callback parameter is read before it is called"
  establishes that the pointer is inspected somewhere ahead of the call, NOT
  that the inspection guards it. Any mention counts.
* Property 2 establishes that FORmat does not call the retraction it passes
  in. It does not establish that no OTHER function retracts FORmat's state
  after a release -- only FORmat's own body is read.
* Nothing here says the claim is a real exclusion, or that `UpdateSettings`
  actually arms. That is the manager's business and this file never opens it.
* The gating above is a TEXTUAL match on a condition. It establishes that a
  branch is spelled as a test of the verdict, not that the variable it names
  still holds the arm's result -- an assignment between the arm and the branch
  is invisible here. And a `return` is established to EXIST inside the success
  block, not to be unconditional: `if (armed) { release; if (x) return true; }`
  satisfies it and can still fall through on success.
* Property 5's branch analysis reads ONE shape: a braced block gated by the
  verdict, with the poll after it. A restructure that puts the poll in the
  success arm and the refusal in an `else` is REFUSED -- the poll then sits
  textually first. That is a refusal of a shape this cannot decide, not a claim
  that the shape is wrong; the message says to teach this file the new shape.
* Whether the RELEASE itself runs on every path is not examined at either site.
  A release moved inside a conditional would leak the claim, and only the
  writes' position relative to it is checked here.

Non-goal at the second site: `SCPI_ClearStreamingOperBits` and
`SCPI_UnpublishStartInterface`, which the refusal branch also calls, are NOT
asserted. They undo streaming-side publication that has nothing to do with the
SD claim and no ordering hazard against the release; a checker demanding them
would be freezing the branch's whole body rather than its property. Nor is the
open-timeout branch further down, which calls `SCPI_ReleaseSdLoggingArm()`:
that is the teardown of an arm that SUCCEEDED, a different contract, and its
own comment says why it must not be used on the refusal path.

Non-goal: `SCPI_StorageSDBenchmark` open-codes the same shape
(`sd_card_manager_TryClaim` ... `sd_card_manager_ReleaseClaim`) because it
arms through `sd_card_manager_UpdateSettingsForPlainWrite` rather than the
generic setter this helper wraps (#925). It is deliberately not examined here;
a checker that demanded it route through `SD_ArmOrRefuse` would be asserting a
refactor nobody has agreed to.

Run:
    python3 tools/lint/scpi_sd_arm_path.py
    python3 tools/lint/scpi_sd_arm_path.py --self-test
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# Reused rather than re-implemented, for the same reason scpi_claim_path.py
# reuses it: a checker that counts calls inside comments reports on code that
# is not shipped, and this file is heavily commented.
from scpi_wiki_sync import strip_c_comments    # noqa: E402

# The helper that owns the refusal path, its NULL-passing wrapper, and the
# claim taker each arm site must go through first.
ARM_HELPER = "SD_ArmOrRefuseWithCleanup"
ARM_WRAPPER = "SD_ArmOrRefuse"
CLAIM_TAKER = "SD_ClaimOrRefuse"
# The manager primitives. TRY_CLAIM is asserted ABSENT from the helper (see the
# docstring); ARM and RELEASE are the positional anchors.
TRY_CLAIM = "sd_card_manager_TryClaim"
RELEASE_CLAIM = "sd_card_manager_ReleaseClaim"
ARM_CALL = "sd_card_manager_UpdateSettings"
# FORmat: the one caller that publishes state before arming, and the call that
# publishes it. Both names are spelled out rather than discovered -- unlike the
# retraction, which IS discovered (from the argument FORmat passes), because
# there the discovery is what keeps a rename from silently disarming property
# 2. A rename of either name below reds the gate loudly instead, which is the
# safe direction: it costs one line here and cannot be mistaken for a pass.
FORMAT_FN = "SCPI_StorageSDFormat"
FORMAT_PUBLISH = "sd_card_manager_SetFormatPending"
# The refusal-path clear, as it is actually written: `<cfg>->mode = <sentinel>`.
MODE_FIELD = "mode"
MODE_NONE = "SD_CARD_MANAGER_MODE_NONE"

# ---- the second site: the streaming-log arm in SCPIInterface.c (#942/#974) --
# `static`, one caller (SCPI_StartStreaming), and the arm is a ~130-line slice
# of it. Named rather than discovered for the same reason FORMAT_FN is: a
# rename reds this gate loudly, which is the safe direction.
STREAM_FN = "SCPI_StartStreamingClaimed"
STREAM_ARM_CALL = "sd_card_manager_UpdateSettingsForStreamingLog"
# The poll the refusal has to beat. It requires `mode == MODE_WRITE`, so on a
# refused arm it can never become true -- the 5 s wait and the "SD file not
# ready" that follows are what #942 removed.
STREAM_POLL = "sd_card_manager_IsWriteReady"
# This site takes the manager's claim directly; there is no SD_ClaimOrRefuse
# in SCPIInterface.c.
STREAM_CLAIM_TAKER = "sd_card_manager_TryClaim"

# The five plain arm sites (CRC, GET, LISt, DELete, SPACe) at the time this was
# written. A FLOOR, not an equality: a new command that publishes nothing
# before arming is expected to use the wrapper and needs no edit here. Dropping
# below five means a caller was rerouted or the scan broke, and a checker that
# silently examines fewer sites than it thinks is the failure this guards
# against.
KNOWN_PLAIN_ARM_SITES = 5

_STR_OR_CHAR = re.compile(r'"(?:\\.|[^"\\])*"' r"|'(?:\\.|[^'\\])*'", re.S)


def _blank(text):
    """`text` with string/char literals replaced by spaces, offsets preserved.

    Every scan below runs on this, so a literal that merely SPELLS a call
    cannot satisfy one. Copied in shape from `scpi_claim_path.py`; not imported,
    because it is private there and that file must not be edited from here.
    """
    return _STR_OR_CHAR.sub(lambda m: " " * len(m.group(0)), text)


# Anchored to a line that STARTS with a return type. The bare form also matches
# a CALL inside a condition -- in `if (foo(a)) {` the parameter class can
# absorb `a)` and take the outer `)` before the brace -- so an unanchored scan
# reads a call as a definition and hands back the wrong body. A prototype is
# excluded by `[^;{]*`, which cannot cross the `;`.
_DEF = re.compile(r"(?m)^[A-Za-z_][\w \t\*]*\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{")


def _match_brace(masked, start):
    """Index just past the `}` closing the `{` at `start`, or None."""
    depth = 0
    for i in range(start, len(masked)):
        if masked[i] == "{":
            depth += 1
        elif masked[i] == "}":
            depth -= 1
            if depth == 0:
                return i + 1
    return None


def function_body(text, name):
    """The brace-delimited body of C function `name`, or None if not found.

    `text` must already have comments stripped. Literals are blanked before
    brace counting so a brace inside a format string cannot unbalance the scan.
    """
    sig = re.search(r"(?m)^[A-Za-z_][\w \t\*]*\b%s\s*\([^;{]*\)\s*\{"
                    % re.escape(name), text)
    if not sig:
        return None
    start = sig.end() - 1
    end = _match_brace(_blank(text), start)
    return None if end is None else text[start:end]


def function_spans(text):
    """[(name, body_start, body_end)] for every definition, outermost first.

    Used to attribute a call OFFSET to the function it sits in, which is how
    the arm-site census below tells FORmat's `WithCleanup` call from the one
    inside the wrapper -- and how a DEFINITION is told from a call at all: a
    definition's own name occurrence lies before its body starts, so it falls
    in no span and is never counted as a call site.
    """
    masked = _blank(text)
    spans = []
    for m in _DEF.finditer(text):
        start = m.end() - 1
        if any(s <= start < e for _, s, e in spans):
            continue                     # inside a body already collected
        end = _match_brace(masked, start)
        if end is not None:
            spans.append((m.group(1), start, end))
    return spans


def enclosing_function(spans, pos):
    """Name of the function whose BODY contains `pos`, or None."""
    for name, start, end in spans:
        if start < pos < end:
            return name
    return None


def _calls(body, name):
    """True iff `body` CALLS `name`, rather than merely mentioning it."""
    return re.search(r"\b%s\s*\(" % re.escape(name), _blank(body)) is not None


def _call_positions(body, name):
    """Offsets of every CALL to `name` in `body`, in order.

    Positional, because presence is not the property: a helper that takes and
    releases the claim around nothing satisfies presence.
    """
    return [m.start() for m in
            re.finditer(r"\b%s\s*\(" % re.escape(name), _blank(body))]


def _split_top_level(text):
    """`text` split at commas that are not inside brackets."""
    out, depth, cur = [], 0, []
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    out.append("".join(cur).strip())
    return out


def call_arguments(body, name):
    """[[arg, ...] | None] for each call to `name`, arguments split top-level.

    `None` for a call whose parenthesis never closes inside `body`. Arguments
    come from the BLANKED text, so a string-literal argument reads as empty --
    positions are what matter here, and every argument this file inspects is an
    identifier.
    """
    b = _blank(body)
    out = []
    for m in re.finditer(r"\b%s\s*\(" % re.escape(name), b):
        i = b.index("(", m.start())
        depth, end = 0, None
        for j in range(i, len(b)):
            if b[j] == "(":
                depth += 1
            elif b[j] == ")":
                depth -= 1
                if depth == 0:
                    end = j
                    break
        if end is None:
            out.append(None)
            continue
        inner = b[i + 1:end]
        out.append(_split_top_level(inner) if inner.strip() else [])
    return out


def signature_params(text, name):
    """[parameter declaration, ...] of C function `name`, or None."""
    sig = re.search(r"(?m)^[A-Za-z_][\w \t\*]*\b%s\s*\(([^;{]*)\)\s*\{"
                    % re.escape(name), text)
    if not sig:
        return None
    inner = sig.group(1).strip()
    return _split_top_level(inner) if inner else []


def callback_param(text, name):
    """-> (param name, index) | (None, reason) for the function-POINTER param.

    Discovered from the signature rather than hard-coded, so a rename is
    FOLLOWED rather than turned into a silent pass -- the same reasoning
    `scpi_claim_path.py` applies to its dispatch parameter and its claim flag.
    """
    params = signature_params(text, name)
    if params is None:
        return None, "%s() not found" % name
    found = [(m.group(1), i) for i, m in
             ((i, re.search(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*\(", p))
              for i, p in enumerate(params)) if m]
    if len(found) != 1:
        return None, (
            "%s() has %d function-pointer parameter(s); this checker needs "
            "exactly one to know which argument carries the refusal-path "
            "retraction, so it refuses rather than guessing." % (name, len(found)))
    return found[0], None


_ID = re.compile(r"[A-Za-z_]\w*$")


# --------------------------------------------------------------------------
# Control-SHAPE primitives
#
# Positional containment says a write sits between two calls. It does not say
# the write runs only on refusal, and that is the half a restructure erases
# most quietly -- hoist the `mode` clear and the `onRefused()` call out of the
# failure guard and a region test alone still reports a pass, while a
# SUCCESSFUL arm now clears the mode it just set (Qodo, PR #976). These read
# the branch a position sits in, still textually and with no control flow.
# --------------------------------------------------------------------------


def _statement_prefix(blanked, pos):
    """The blanked text from the start of `pos`'s statement up to `pos`."""
    start = max(blanked.rfind(ch, 0, pos) for ch in (";", "{", "}"))
    return blanked[start + 1:pos]


# `<type> v = f(...)` and `v = f(...)`. `==`, `!=`, `<=`, `>=`, `+=` cannot
# match: each needs a non-identifier character exactly where the `=` has to be.
_ASSIGN_TAIL = re.compile(r"([A-Za-z_]\w*)\s*=\s*$")
_VOID_TAIL = re.compile(r"\(\s*void\s*\)\s*$")
_ELSE_TAIL = re.compile(r"\belse$")
_BOOL_CMP = re.compile(r"^(.*?)\s*(==|!=)\s*(true|false|TRUE|FALSE)$", re.S)


def _if_statements(body):
    """[(cond_start, cond_end, block_start, block_end)] for every `if` in body.

    `block_start`/`block_end` are None for a brace-less `if`. Those are KEPT in
    the list rather than dropped, so a verdict tested in one is reported as a
    shape this cannot read rather than as a verdict never tested at all.
    """
    b = _blank(body)
    out = []
    for m in re.finditer(r"\bif\s*\(", b):
        i = b.index("(", m.start())
        depth, close = 0, None
        for j in range(i, len(b)):
            if b[j] == "(":
                depth += 1
            elif b[j] == ")":
                depth -= 1
                if depth == 0:
                    close = j
                    break
        if close is None:
            continue
        k = close + 1
        while k < len(b) and b[k].isspace():
            k += 1
        end = _match_brace(b, k) if k < len(b) and b[k] == "{" else None
        out.append((i + 1, close, k, end) if end is not None
                   else (i + 1, close, None, None))
    return out


def _enclosing_blocks(blanked, pos):
    """[(start, end)] of every `{...}` containing `pos`, INNERMOST first.

    The walk goes outward because nesting inside a failure-gated block still
    runs only on failure -- `onRefused()` inside its own `!= NULL` guard is
    confined by the branch that guard sits in, not by the guard.
    """
    out, i = [], pos
    while True:
        depth, found = 0, None
        for j in range(i - 1, -1, -1):
            c = blanked[j]
            if c == "}":
                depth += 1
            elif c == "{":
                if depth == 0:
                    found = j
                    break
                depth -= 1
        if found is None:
            return out
        end = _match_brace(blanked, found)
        if end is None:
            return out
        out.append((found, end))
        if found == 0:
            return out                   # the function body itself
        i = found


def _strip_parens(text):
    """`text` with fully-enclosing parentheses removed, repeatedly."""
    c = text.strip()
    while c.startswith("(") and c.endswith(")"):
        depth, close = 0, None
        for i, ch in enumerate(c):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    close = i
                    break
        if close != len(c) - 1:
            return c                     # `(a) && (b)` -- not enclosing
        c = c[1:-1].strip()
    return c


def _verdict_test(cond, verdict):
    """'true' | 'false' | None -- what `cond` tests about the arm's verdict.

    Recognised, and ONLY these: the verdict bare, negated with `!`, or compared
    to `true`/`false` with `==`/`!=`, in any combination. A condition saying
    anything ELSE as well (`!armed && x`) is deliberately unrecognised -- `&&`
    narrows the branch and `||` widens it, the second is unsound, and telling
    them apart is control flow this file does not have. Refusing both reds
    loudly rather than passing quietly.
    """
    c = _strip_parens(cond)
    negated = False
    while True:
        c = _strip_parens(c)
        if c.startswith("!") and not c.startswith("!="):
            negated, c = not negated, c[1:]
            continue
        m = _BOOL_CMP.match(c)
        if m:
            if (m.group(2) == "==") != (m.group(3).lower() == "true"):
                negated = not negated
            c = m.group(1)
            continue
        break
    if not re.fullmatch(verdict, c, re.S):
        return None
    return "false" if negated else "true"


def _arm_verdict(body, arm, arm_call, ifs):
    """-> (kind, var, pattern) for how the arm's return is consumed.

    kind: "captured" | "inline" | "bare" | "void" | "other". `pattern` is a
    regex matching the expression that carries the verdict (the variable, or
    the arm call itself), and is None when there is nothing to match -- which
    is a refusal, not a pass: with no identifiable verdict, "the success path"
    has no textual meaning and no branch can be placed on either side of it.
    """
    prefix = _statement_prefix(_blank(body), arm)
    m = _ASSIGN_TAIL.search(prefix)
    if m:
        return "captured", m.group(1), re.escape(m.group(1))
    if _VOID_TAIL.search(prefix):
        return "void", None, None
    if not prefix.strip():
        return "bare", None, None
    if any(cs <= arm < ce for cs, ce, _bs, _be in ifs):
        return "inline", None, re.escape(arm_call) + r"\s*\(.*\)"
    return "other", None, None


def _gate_of(body, ifs, block_start):
    """-> (condition text, inverted) gating the block at `block_start`.

    `inverted` is True for the `else` side of an `if`, whose condition is the
    paired `if`'s. (None, False) when the block is not an if/else body at all.
    """
    b = _blank(body)
    for cs, ce, bs, _be in ifs:
        if bs == block_start:
            return b[cs:ce], False
    pre = b[:block_start].rstrip()
    if not _ELSE_TAIL.search(pre):
        return None, False
    pre = pre[:-4].rstrip()
    if not pre.endswith("}"):
        return None, False               # `else` of a brace-less `if`
    for cs, ce, _bs, be in ifs:
        if be == len(pre):
            return b[cs:ce], True
    return None, False


def _path_of(body, ifs, pos, pattern):
    """'true' | 'false' | None -- the verdict path `pos` is confined to."""
    for start, _end in _enclosing_blocks(_blank(body), pos):
        cond, inverted = _gate_of(body, ifs, start)
        if cond is None:
            continue
        verdict = _verdict_test(cond, pattern)
        if verdict is None:
            continue
        if inverted:
            verdict = "true" if verdict == "false" else "false"
        return verdict
    return None


_NO_VERDICT = (
    "%(who)s does not consume %(arm)s()'s return in a form this checker can "
    "read (%(kind)s). It needs the verdict either CAPTURED into a variable or "
    "standing as an `if` condition, because without it there is no text that "
    "says which branch is the success path -- and 'the write is between the "
    "arm and the release' is then not 'the write runs only on refusal', which "
    "is the hole this closes. Refusing rather than reporting a pass it did not "
    "establish (#955/#964; Qodo, PR #976).")


# Positional reasoning is only sound over a region with one identifiable start
# and one identifiable end. The shape here is one claim with TWO exits, not two
# claims, so two releases are sound and are what the tree has; one release is a
# single-exit restructure and is equally sound. Beyond that, deciding which
# exit a given release belongs to needs control flow, and "between the first
# and the last" would then be satisfied by a write sitting in neither path.
_TOO_MANY_EXITS = (
    "%(who)s calls %(release)s() %(n)d times. This checker can reason about "
    "ONE claim with one or two exits (the success return and the refusal "
    "return); with more, deciding which exit each release belongs to needs "
    "control flow it does not have, and 'between the first and the last' is "
    "not 'on the refusal path'. It refuses rather than reporting a pass it "
    "did not establish. If the helper was deliberately restructured, teach "
    "this file the new shape in the same commit and say why (#971; limits "
    "tracked in #896).")


def _helper_problems(text, helper):
    """Property 1, helper side: the claim is held across the arm, and both
    refusal-path writes fall inside it AND run only on refusal."""
    problems = []

    # The claim is the CALLER's. A TryClaim appearing here would mean the
    # ownership model the region reasoning rests on had changed, so it is
    # refused rather than reinterpreted (see the docstring on #971's wording).
    if _calls(helper, TRY_CLAIM):
        problems.append(
            "%s() calls %s(): the claim is supposed to be the CALLER's, taken "
            "by %s() before the operands are written (#829). With the claim "
            "taken here instead, this checker's premise -- that the helper is "
            "entered holding it -- no longer holds, so it refuses rather than "
            "reporting on a shape it was not written for."
            % (ARM_HELPER, TRY_CLAIM, CLAIM_TAKER))

    arms = _call_positions(helper, ARM_CALL)
    releases = _call_positions(helper, RELEASE_CLAIM)
    if len(arms) != 1:
        problems.append(
            "%s() calls %s() %d times; this checker needs exactly one arm to "
            "place the claim around. Refusing rather than reporting a pass it "
            "did not establish." % (ARM_HELPER, ARM_CALL, len(arms)))
    if not releases:
        problems.append(
            "%s() never calls %s(), so a claim once taken is never released "
            "and every later SD command is refused for the rest of the "
            "session (#829)." % (ARM_HELPER, RELEASE_CLAIM))
    elif len(releases) > 2:
        problems.append(_TOO_MANY_EXITS % {
            "who": "%s()" % ARM_HELPER, "release": RELEASE_CLAIM,
            "n": len(releases)})
    if len(arms) != 1 or not releases or len(releases) > 2:
        return problems                  # no sound region to reason about

    arm = arms[0]
    if any(r < arm for r in releases):
        problems.append(
            "%s() calls %s() before %s(): the claim is dropped before the arm, "
            "so the arm itself -- and everything this checker would then place "
            "'inside' the claim -- runs unowned (#829)."
            % (ARM_HELPER, RELEASE_CLAIM, ARM_CALL))
        return problems

    # Which branch is which has to be readable before any of it means
    # anything -- see "A region is not a path" in the docstring.
    ifs = _if_statements(helper)
    kind, _var, pattern = _arm_verdict(helper, arm, ARM_CALL, ifs)
    if pattern is None:
        problems.append(_NO_VERDICT % {
            "who": "%s()" % ARM_HELPER, "arm": ARM_CALL,
            "kind": {"bare": "the call stands alone as a statement",
                     "void": "the return is cast to `(void)`"}.get(
                         kind, "unrecognised consumer")})
        return problems

    # The refusal region, and what confines a write to the refusal PATH.
    # Two releases: the success branch takes the first one and LEAVES, so the
    # fall-through past its closing brace is the refusal path. One release:
    # there is no such exit, so each write must be gated on failure itself.
    if len(releases) == 2:
        blocks = _enclosing_blocks(_blank(helper), releases[0])
        success = blocks[0] if blocks else None
        if success is None or success[0] == 0:
            problems.append(
                "%s() releases twice, but the FIRST %s() is not inside a "
                "conditional block -- it runs on every path, so the `%s` clear "
                "and the caller's retraction after it are unowned writes no "
                "matter which branch took them. The shape this reasons about "
                "is `if (<the arm's verdict>) { release; return; }` and then "
                "the refusal path (#955/#964)."
                % (ARM_HELPER, RELEASE_CLAIM, MODE_FIELD))
            return problems
        cond, inverted = _gate_of(helper, ifs, success[0])
        verdict = None if cond is None else _verdict_test(cond, pattern)
        if verdict is not None and inverted:
            verdict = "true" if verdict == "false" else "false"
        if verdict != "true":
            problems.append(
                "%s()'s first %s() sits in a block this checker cannot read as "
                "the SUCCESS path (its condition is `%s`). Everything after "
                "that block is then called the refusal path on no evidence -- "
                "the hole Qodo found in PR #976. Recognised: the arm's verdict "
                "bare, `!`-negated, or compared with `== true` / `== false`, "
                "and the `else` of those. Anything else is refused rather than "
                "guessed."
                % (ARM_HELPER, RELEASE_CLAIM,
                   "not an if/else body" if cond is None
                   else " ".join(cond.split())))
            return problems
        if not re.search(r"\breturn\b",
                         _blank(helper[success[0]:success[1]])):
            problems.append(
                "%s()'s success block takes the first %s() but never RETURNS, "
                "so execution falls out of it into what this checker would "
                "otherwise call the refusal path. On a successful arm the `%s` "
                "clear and the caller's retraction would then run anyway -- "
                "clearing the mode that arm just set, which is #955 exactly "
                "(Qodo, PR #976)." % (ARM_HELPER, RELEASE_CLAIM, MODE_FIELD))
            return problems
        lo, hi = success[1], releases[1]
        where = ("after the success block has returned and before the "
                 "refusal-path %s()" % RELEASE_CLAIM)
        gated = False
    else:
        lo, hi = arm, releases[0]
        where = "between %s() and the release" % ARM_CALL
        gated = True                     # no success exit to sit behind

    # (1e) #955: the `mode` clear
    clears = [(m.start(), m.group(1)) for m in re.finditer(
        r"\b([A-Za-z_]\w*)\s*->\s*%s\s*=\s*%s\s*;"
        % (re.escape(MODE_FIELD), re.escape(MODE_NONE)), _blank(helper))]
    if len(clears) != 1:
        problems.append(
            "%s() contains %d `<cfg>->%s = %s;` statement(s); expected exactly "
            "one, the refused arm's clear. Anything else -- none, or several "
            "whose paths cannot be told apart -- leaves this checker unable to "
            "say the clear happens under the claim, so it refuses (#955)."
            % (ARM_HELPER, len(clears), MODE_FIELD, MODE_NONE))
    else:
        pos, target = clears[0]
        args = call_arguments(helper, ARM_CALL)[0]
        if args is None or len(args) != 1 or not _ID.fullmatch(args[0]):
            problems.append(
                "%s()'s call to %s() does not take a single bare identifier, "
                "so the object the clear puts back could not be compared "
                "against the object that was armed. That comparison is "
                "UNVERIFIED and this refuses rather than assuming they match."
                % (ARM_HELPER, ARM_CALL))
        elif target != args[0]:
            problems.append(
                "%s() arms `%s` but clears `%s->%s`: the refused arm puts back "
                "the mode of a different object than the one it tried to arm "
                "(#955)." % (ARM_HELPER, args[0], target, MODE_FIELD))
        if not lo < pos < hi:
            problems.append(
                "%s() does not clear `%s->%s` %s: past the release the store "
                "is unowned and can land on the NEXT owner's state, which is "
                "the race #955 closed by moving it here."
                % (ARM_HELPER, target, MODE_FIELD, where))
        elif gated and _path_of(helper, ifs, pos, pattern) != "false":
            problems.append(
                "%s() clears `%s->%s` inside the claim but NOT on a branch "
                "gated by the refused arm, so it runs on every path -- a "
                "successful arm clears the `%s` it just set. With a single "
                "release there is no success-path exit for it to sit behind, "
                "so the clear must be inside `if (!<verdict>)`, `== false`, or "
                "the `else` of the true-gated form (#955; Qodo, PR #976)."
                % (ARM_HELPER, target, MODE_FIELD, MODE_FIELD))

    # (1f) #964: the caller-published retraction, through the callback param
    return problems + _callback_problems(text, helper, lo, hi, where,
                                         gated, ifs, pattern)


def _callback_problems(text, helper, lo, hi, where, gated, ifs, pattern):
    """Property 1, the `onRefused` half: called exactly once, inside the
    region, on the refusal path, and inspected before it is called."""
    problems = []
    found, why = callback_param(text, ARM_HELPER)
    if found is None:
        problems.append(
            "%s -- so where the refusal path invokes the caller's retraction "
            "could not be located, and its position relative to the release "
            "is UNVERIFIED (#964)." % why)
        return problems
    cb, _index = found
    calls = _call_positions(helper, cb)
    if len(calls) != 1:
        problems.append(
            "%s() calls its %s() parameter %d times; expected exactly one, on "
            "the refusal path. None means the retraction a caller passed is "
            "never run (its state stays published over a refused arm); more "
            "than one means this checker cannot say which call it is placing "
            "(#964)." % (ARM_HELPER, cb, len(calls)))
        return problems
    pos = calls[0]
    if not lo < pos < hi:
        problems.append(
            "%s() does not invoke %s() %s: past the release it is an unowned "
            "write, exactly like the `%s` clear #955 moved, and can retract "
            "state belonging to the NEXT owner (#964)."
            % (ARM_HELPER, cb, where, MODE_FIELD))
    elif gated and _path_of(helper, ifs, pos, pattern) != "false":
        problems.append(
            "%s() invokes %s() inside the claim but NOT on a branch gated by "
            "the refused arm, so it runs on every path -- a SUCCESSFUL arm "
            "would retract the state its own caller published for it. The "
            "`%s != NULL` guard is not that gate; nesting inside a "
            "failure-gated block is what counts, and there is none here "
            "(#964; Qodo, PR #976)." % (ARM_HELPER, cb, cb))
    reads = [m.start() for m in
             re.finditer(r"\b%s\b" % re.escape(cb), _blank(helper))
             if m.start() not in calls]
    if not any(r < pos for r in reads):
        problems.append(
            "%s() calls %s() without inspecting it first -- the commands that "
            "pass NULL through %s() would dereference it. This "
            "establishes that the pointer is READ ahead of the call, not that "
            "the read guards it; regex cannot show that (#896)."
            % (ARM_HELPER, cb, ARM_WRAPPER))
    return problems


def _format_problems(text):
    """Property 2: FORmat's retraction goes THROUGH the callback parameter."""
    problems = []
    body = function_body(text, FORMAT_FN)
    if body is None:
        problems.append(
            "%s() not found, so the one caller that publishes state before "
            "arming was NOT checked. Refusing rather than reporting a pass on "
            "a function this checker could not read (#964)." % FORMAT_FN)
        return problems, None

    calls = _call_positions(body, ARM_HELPER)
    if len(calls) != 1:
        problems.append(
            "%s() calls %s() %d times; expected exactly one arm. With none it "
            "no longer routes its refusal through the helper that retracts "
            "under the claim; with several this cannot say which one carries "
            "the retraction (#964)." % (FORMAT_FN, ARM_HELPER, len(calls)))
        return problems, None

    found, why = callback_param(text, ARM_HELPER)
    if found is None:
        problems.append("%s -- so %s()'s retraction argument could not be "
                        "located." % (why, FORMAT_FN))
        return problems, None
    cb, index = found
    params = signature_params(text, ARM_HELPER)
    args = call_arguments(body, ARM_HELPER)[0]
    if args is None or len(args) != len(params):
        problems.append(
            "%s() passes %s argument(s) to %s(), which declares %d. This "
            "checker cannot line the retraction up with the %s parameter, so "
            "it refuses." % (FORMAT_FN, "?" if args is None else len(args),
                             ARM_HELPER, len(params), cb))
        return problems, None

    retraction = args[index]
    if not _ID.fullmatch(retraction) or retraction == "NULL":
        problems.append(
            "%s() passes `%s` as %s()'s %s argument. #829 has it publish "
            "format-pending BEFORE arming, so a refused arm must retract it "
            "under the claim; passing nothing there leaves a format advertised "
            "that will never run (#964)."
            % (FORMAT_FN, retraction or "nothing", ARM_HELPER, cb))
        return problems, None

    # THE shape #964 removed: retracting at the call site, after the helper has
    # already released. Both spellings pass property 3 and the helper checks.
    if _calls(body, retraction):
        problems.append(
            "%s() calls %s() at its own call site as well as passing it to "
            "%s(). That is the pre-#964 shape: at the call site the helper has "
            "already released, so the store is unowned and can retract the "
            "NEXT owner's format state. Pass it in and let the refusal path "
            "run it under the claim." % (FORMAT_FN, retraction, ARM_HELPER))

    pubs = _call_positions(body, FORMAT_PUBLISH)
    if len(pubs) != 1:
        problems.append(
            "%s() calls %s() %d times; expected exactly one publish before the "
            "arm. It is the premise of the retraction above -- with no publish "
            "this checker cannot say whether the callback is still needed, and "
            "with several it cannot say which the retraction undoes -- so it "
            "refuses rather than quietly dropping the property (#829/#964)."
            % (FORMAT_FN, FORMAT_PUBLISH, len(pubs)))
    elif pubs[0] > calls[0]:
        problems.append(
            "%s() publishes format-pending AFTER arming. #829 requires the "
            "claim, then the publish, then the arm, so that only the owner "
            "ever advertises a format." % FORMAT_FN)
    return problems, retraction


def _census_problems(text, spans):
    """Property 3, plus property 1's caller-side half.

    -> (problems, [(function, arm offset, helper_form)]).
    """
    problems = []
    sites = []
    for name, positions in ((ARM_WRAPPER, _call_positions(text, ARM_WRAPPER)),
                            (ARM_HELPER, _call_positions(text, ARM_HELPER))):
        for pos in positions:
            fn = enclosing_function(spans, pos)
            if fn is None or fn == name:
                continue                 # the definition itself, or recursion
            if name == ARM_HELPER and fn == ARM_WRAPPER:
                continue                 # the wrapper's own NULL-passing call
            sites.append((fn, pos, name))
    sites.sort(key=lambda s: s[1])

    plain = [s for s in sites if s[2] == ARM_WRAPPER]
    cleanup = [s for s in sites if s[2] == ARM_HELPER]

    if not sites:
        problems.append(
            "no calls to %s() or %s() found outside their own definitions -- "
            "the file moved or the call scan broke. Refusing to report a pass "
            "on a file this checker could not read." % (ARM_WRAPPER, ARM_HELPER))
        return problems, sites

    if len(plain) < KNOWN_PLAIN_ARM_SITES:
        problems.append(
            "found only %d call(s) to %s(), expected at least %d (CRC, GET, "
            "LISt, DELete, SPACe). If one was genuinely removed or rerouted, "
            "lower KNOWN_PLAIN_ARM_SITES in this file and say why in the "
            "commit." % (len(plain), ARM_WRAPPER, KNOWN_PLAIN_ARM_SITES))

    if len(cleanup) != 1 or cleanup[0][0] != FORMAT_FN:
        problems.append(
            "%s() is called from %s; expected exactly one caller, %s(). The "
            "wrapper %s() exists so a command with nothing published before "
            "the arm passes NULL by construction; a NEW caller of the "
            "WithCleanup form is a deliberate choice about state that must be "
            "retracted under the claim, so it fails here until this file is "
            "taught about it (#971)."
            % (ARM_HELPER,
               ", ".join("%s()" % f for f, _, _ in cleanup) or "nowhere",
               FORMAT_FN, ARM_WRAPPER))

    # The wrapper must pass NULL, or the five inherit something else.
    wrapper = function_body(text, ARM_WRAPPER)
    found, why = callback_param(text, ARM_HELPER)
    if wrapper is None:
        problems.append(
            "%s() not found, so what the five plain arm sites pass in the "
            "retraction slot is UNVERIFIED." % ARM_WRAPPER)
    elif found is not None:
        _cb, index = found
        args = (call_arguments(wrapper, ARM_HELPER) or [None])[0]
        params = signature_params(text, ARM_HELPER)
        if args is None or len(args) != len(params):
            problems.append(
                "%s() does not call %s() with its full argument list, so what "
                "it passes in the retraction slot could not be read."
                % (ARM_WRAPPER, ARM_HELPER))
        elif args[index] != "NULL":
            problems.append(
                "%s() passes `%s` rather than NULL in %s()'s retraction slot. "
                "The five commands that route through it publish nothing "
                "before arming; retracting format state from a refused "
                "CRC/GET/LISt/DELete/SPACe would destroy a result those "
                "commands never published (#964)."
                % (ARM_WRAPPER, args[index], ARM_HELPER))

    # Property 1, caller side: the claim precedes the arm, in the arm's own
    # function. This is the checkable half of #971's "TryClaim-side entry".
    for fn in sorted({f for f, _, _ in sites}):
        body = function_body(text, fn)
        if body is None:
            problems.append(
                "%s() arms but its body could not be re-read to check the "
                "claim precedes the arm." % fn)
            continue
        base = next(s for n, s, _e in spans if n == fn)
        takers = _call_positions(body, CLAIM_TAKER)
        arms = [pos - base for f, pos, _ in sites if f == fn]
        if len(takers) != 1:
            problems.append(
                "%s() calls %s() %d times before arming; expected exactly one. "
                "The arm is where ownership hands over from the claim to "
                "`%s`, so an arm with no claim taken -- or with a claim this "
                "checker cannot place -- has nothing to hand over (#829)."
                % (fn, CLAIM_TAKER, len(takers), MODE_FIELD))
        elif any(a < takers[0] for a in arms):
            problems.append(
                "%s() arms before it calls %s(): every write to the shared "
                "operands, `%s` included, must follow the claim (#829)."
                % (fn, CLAIM_TAKER, MODE_FIELD))
    return problems, sites


def _stream_arm_problems(text):
    """Properties 4 and 5, in `SCPIInterface.c`: the streaming-log arm's
    verdict is consumed, and its refusal returns -- under the claim -- before
    the readiness poll. -> (problems, sites examined)."""
    problems = []
    body = function_body(text, STREAM_FN)
    if body is None:
        problems.append(
            "%s() not found, so the streaming-log arm this checker exists to "
            "order was NOT examined. Refusing rather than reporting a pass on "
            "a function it could not read (#942)." % STREAM_FN)
        return problems, 0

    arms = _call_positions(body, STREAM_ARM_CALL)
    if len(arms) != 1:
        problems.append(
            "%s() calls %s() %d times; this checker needs exactly one arm to "
            "order the refusal against. None means the arm moved or was "
            "renamed and nothing here is being checked at all; several means "
            "it cannot say which verdict a refusal branch belongs to (#942)."
            % (STREAM_FN, STREAM_ARM_CALL, len(arms)))
        return problems, 0
    arm = arms[0]

    polls = _call_positions(body, STREAM_POLL)
    if not polls:
        problems.append(
            "%s() never calls %s(): that poll is what property 5 orders the "
            "refusal against, so with it gone the ordering is UNVERIFIED and "
            "this refuses rather than passing on it (#942)."
            % (STREAM_FN, STREAM_POLL))
        return problems, 1
    poll = polls[0]
    if poll < arm:
        problems.append(
            "%s() calls %s() before it arms. The poll waits on the file THIS "
            "arm opens, so ahead of the arm it reads the previous session's "
            "state and the ordering property 5 states no longer means "
            "anything (#942)." % (STREAM_FN, STREAM_POLL))
        return problems, 1

    # The claim first: without it, "under the claim" below says nothing.
    takers = _call_positions(body, STREAM_CLAIM_TAKER)
    if len(takers) != 1:
        problems.append(
            "%s() calls %s() %d times; expected exactly one, before the arm. "
            "The arm is where ownership hands over from the claim to `%s`, so "
            "an arm with no claim taken -- or with a claim this checker cannot "
            "place -- has nothing to hand over, and the refusal branch's clear "
            "is an unowned write however it is ordered (#829/#836)."
            % (STREAM_FN, STREAM_CLAIM_TAKER, len(takers), MODE_FIELD))
    elif takers[0] > arm:
        problems.append(
            "%s() arms before it calls %s(): every write to the shared "
            "operands, `%s` included, must follow the claim (#829/#836)."
            % (STREAM_FN, STREAM_CLAIM_TAKER, MODE_FIELD))

    # ---- property 4: the verdict is consumed --------------------------------
    ifs = _if_statements(body)
    kind, var, pattern = _arm_verdict(body, arm, STREAM_ARM_CALL, ifs)
    if kind == "bare":
        problems.append(
            "%s() DISCARDS %s()'s return -- the call stands alone as a "
            "statement. That is the pre-#942 shape: on the arbitration the "
            "#589 pre-check above cannot see (a WiFi FW update, a "
            "WiFi-streaming start on the other transport, a bus-jam "
            "quarantine, all landing after that check), the callee's gate puts "
            "`%s` back to %s and arms NOTHING -- so %s() can never become true "
            "and the poll below can only spend its full 5 s and then blame the "
            "media for the SD task not running."
            % (STREAM_FN, STREAM_ARM_CALL, MODE_FIELD, MODE_NONE, STREAM_POLL))
        return problems, 1
    if kind == "void":
        problems.append(
            "%s() casts %s()'s return to `(void)`. A deliberate discard is "
            "still a discard, with the identical consequence to the bare "
            "statement #942 replaced: on a lost arbitration nothing is armed "
            "and the readiness poll below can only time out. If the verdict is "
            "genuinely not worth acting on, that is an argument to make in a "
            "PR, not a cast." % (STREAM_FN, STREAM_ARM_CALL))
        return problems, 1
    if pattern is None:
        problems.append(
            "%s() consumes %s()'s return in a form this checker cannot read: "
            "neither assigned to a variable nor standing as an `if` condition. "
            "Refusing rather than reporting a pass on a shape it did not "
            "understand (#942)." % (STREAM_FN, STREAM_ARM_CALL))
        return problems, 1

    # ---- property 5: the refusal branch, and where it sits ------------------
    b = _blank(body)
    if kind == "inline":
        gating = [it for it in ifs if it[0] <= arm < it[1]]
    else:
        gating = [it for it in ifs if it[1] > arm
                  and re.search(r"\b%s\b" % re.escape(var), b[it[0]:it[1]])]
    if not gating:
        problems.append(
            "%s() captures %s()'s return as `%s`, but no `if` after the arm "
            "tests it. A verdict read into a variable and never branched on is "
            "the discard of #942 with an extra line (#942)."
            % (STREAM_FN, STREAM_ARM_CALL, var))
        return problems, 1
    cond_start, cond_end, block_start, block_end = gating[0]
    cond = " ".join(b[cond_start:cond_end].split())
    verdict = _verdict_test(b[cond_start:cond_end], pattern)
    if verdict != "false":
        problems.append(
            "%s()'s first test of the arm's verdict, `%s`, is not one this "
            "checker can read as the REFUSAL branch. It needs the failure "
            "form -- the verdict `!`-negated, `== false`, or `!= true` -- "
            "because that is the branch property 5 places before %s(). A "
            "restructure that tests success and puts the refusal in an `else` "
            "(or falls through to it) is REFUSED, not judged wrong: teach this "
            "file the new shape in the same commit and say why (#942)."
            % (STREAM_FN, cond, STREAM_POLL))
        return problems, 1
    if block_start is None:
        problems.append(
            "%s()'s refusal test `%s` has no braced block, so what it does -- "
            "the `%s` clear, the release, the return -- could not be read. "
            "Refusing rather than reporting a pass on a branch it never saw "
            "(#942)." % (STREAM_FN, cond, MODE_FIELD))
        return problems, 1

    block = body[block_start:block_end]
    if not re.search(r"\breturn\b", _blank(block)):
        problems.append(
            "%s()'s refusal branch does not RETURN. Execution falls out of it "
            "into the %s() poll, which on a refused arm can never become true "
            "-- so the 5 s wait and the misdirected 'SD file not ready' that "
            "#942 removed are both back, with the error already pushed (#942)."
            % (STREAM_FN, STREAM_POLL))
    if block_end > poll:
        problems.append(
            "%s()'s refusal branch is positioned AFTER the first %s() call. "
            "The poll runs first and, on a refused arm, can only time out "
            "before the refusal is ever reached -- the whole point of #942 is "
            "that the refusal beats the poll. (If this is a deliberate "
            "restructure that polls in the success arm and refuses in an "
            "`else`, this checker cannot decide it: teach it the new shape.)"
            % (STREAM_FN, STREAM_POLL))

    # ---- property 5, the claim half: clear, THEN release, in the branch -----
    clears = [(m.start(), m.group(1)) for m in re.finditer(
        r"\b([A-Za-z_]\w*)\s*->\s*%s\s*=\s*%s\s*;"
        % (re.escape(MODE_FIELD), re.escape(MODE_NONE)), _blank(block))]
    releases = _call_positions(block, RELEASE_CLAIM)
    if len(clears) != 1:
        problems.append(
            "%s()'s refusal branch contains %d `<cfg>->%s = %s;` statement(s); "
            "expected exactly one. None means a refused arm leaves `%s` "
            "advertising a WRITE nobody armed; several leave this checker "
            "unable to say which one it is placing against the release "
            "(#955/#942)."
            % (STREAM_FN, len(clears), MODE_FIELD, MODE_NONE, MODE_FIELD))
    if len(releases) != 1:
        problems.append(
            "%s()'s refusal branch calls %s() %d times; expected exactly one. "
            "None leaks the claim: nothing else releases on this path, so "
            "every later SD command -- including SYST:STOR:SD:ENAble, the one "
            "escape hatch -- is refused for the rest of the session. Several "
            "cannot be placed against the clear (#836/#955)."
            % (STREAM_FN, RELEASE_CLAIM, len(releases)))
    if len(clears) == 1 and len(releases) == 1:
        pos, target = clears[0]
        if pos > releases[0]:
            problems.append(
                "%s()'s refusal branch clears `%s->%s` AFTER %s(): past the "
                "release the store is unowned, and the other SCPI transport "
                "(USB pri 7 preempts WiFi pri 2, no shared dispatch mutex) can "
                "claim and arm in the gap -- so the clear lands on THAT "
                "owner's state. Clear under the claim, then release (#955)."
                % (STREAM_FN, target, MODE_FIELD, RELEASE_CLAIM))
        args = call_arguments(body, STREAM_ARM_CALL)[0]
        if args is None or len(args) != 1 or not _ID.fullmatch(args[0]):
            problems.append(
                "%s()'s call to %s() does not take a single bare identifier, "
                "so the object its refusal puts back could not be compared "
                "against the object that was armed. That comparison is "
                "UNVERIFIED and this refuses rather than assuming they match."
                % (STREAM_FN, STREAM_ARM_CALL))
        elif target != args[0]:
            problems.append(
                "%s() arms `%s` but its refusal clears `%s->%s`: the refused "
                "arm puts back the mode of a different object than the one it "
                "tried to arm (#955)."
                % (STREAM_FN, args[0], target, MODE_FIELD))
    return problems, 1


def check(source_text):
    """-> (problems, examined). Pure, so --self-test can drive it."""
    text = strip_c_comments(source_text)
    problems = []
    spans = function_spans(text)

    helper = function_body(text, ARM_HELPER)
    if helper is None:
        problems.append(
            "%s() not found -- the refusal path this checker exists to place "
            "could not be read at all. Refusing rather than reporting a pass "
            "(#971)." % ARM_HELPER)
        return problems, 0

    problems.extend(_helper_problems(text, helper))
    fmt_problems, _retraction = _format_problems(text)
    problems.extend(fmt_problems)
    census, sites = _census_problems(text, spans)
    problems.extend(census)
    return problems, len(sites)


def check_stream(source_text):
    """-> (problems, examined) for `SCPIInterface.c`. Pure, like check()."""
    return _stream_arm_problems(strip_c_comments(source_text))


# --------------------------------------------------------------------------
# self-test: pure, no source tree needed
# --------------------------------------------------------------------------
# A miniature of the real shape: the helper, its NULL-passing wrapper, the
# claim taker, one plain arm site standing in for the five, and FORmat with its
# publish-then-arm-with-retraction. The floor is lowered around the fixture so
# one plain site is a valid sample.
_GOOD = '''
static bool SD_ArmOrRefuseWithCleanup(scpi_t *context, const char *cmd,
                                      sd_card_manager_settings_t *cfg,
                                      void (*onRefused)(void))
{
    if (sd_card_manager_UpdateSettings(cfg)) {
        sd_card_manager_ReleaseClaim();
        return true;
    }
    cfg->mode = SD_CARD_MANAGER_MODE_NONE;
    if (onRefused != NULL) {
        onRefused();
    }
    sd_card_manager_ReleaseClaim();
    LOG_E("SD:%s - could not arm {the} operation\\r\\n", cmd);
    SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
    return false;
}

static bool SD_ArmOrRefuse(scpi_t *context, const char *cmd,
                           sd_card_manager_settings_t *cfg)
{
    return SD_ArmOrRefuseWithCleanup(context, cmd, cfg, NULL);
}

static bool SD_ClaimOrRefuse(scpi_t *context, const char *cmd)
{
    if (!sd_card_manager_TryClaim()) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return false;
    }
    return true;
}

scpi_result_t SCPI_StorageSDSpaceGet(scpi_t * context) {
    sd_card_manager_settings_t* pCfg = BoardRunTimeConfig_Get(BOARDRUNTIME_SD);
    if (!SD_ClaimOrRefuse(context, "SPACe")) {
        return SCPI_RES_ERR;
    }
    pCfg->mode = SD_CARD_MANAGER_MODE_GET_SPACE;
    if (!SD_ArmOrRefuse(context, "SPACe", pCfg)) {
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t SCPI_StorageSDFormat(scpi_t * context) {
    sd_card_manager_settings_t* pCfg = BoardRunTimeConfig_Get(BOARDRUNTIME_SD);
    if (!SD_ClaimOrRefuse(context, "FORmat")) {
        return SCPI_RES_ERR;
    }
    sd_card_manager_SetFormatPending();
    pCfg->mode = SD_CARD_MANAGER_MODE_FORMAT;
    if (!SD_ArmOrRefuseWithCleanup(context, "FORmat", pCfg,
                                   sd_card_manager_ClearFormatStatus)) {
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}
'''

# A miniature of the SECOND site. The real SCPI_StartStreamingClaimed is ~1200
# lines and the arm is a ~130-line slice of it; everything around the ordering
# -- the frequency parse, the interface publication, the #589 pre-check, the
# five-second poll's own error reporting -- is left out. The refusal branch and
# the poll are separate constants so a mutation can move one past the other,
# which is property 5 stated as an edit.
_STREAM_REFUSAL = '''    if (!sdArmed) {
        pSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
        sd_card_manager_ReleaseClaim();
        SCPI_ClearStreamingOperBits(pRunTimeStreamConfig);
        LOG_E("Cannot start SD logging - could not arm the write\\r\\n");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
'''

_STREAM_POLL_LOOP = '''    int readyWait = 0;
    while (!sd_card_manager_IsWriteReady() && readyWait < 500) {
        vTaskDelay(pdMS_TO_TICKS(10));
        readyWait++;
    }
    if (!sd_card_manager_IsWriteReady()) {
        LOG_E("[SD] STR:START refused: SD file not ready\\r\\n");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
'''

_GOOD_STREAM = '''
static scpi_result_t SCPI_StartStreamingClaimed(scpi_t * context, int32_t freq)
{
    sd_card_manager_settings_t *pSDCardSettings = BoardRunTimeConfig_Get(BOARDRUNTIME_SD);
    if (!sd_card_manager_TryClaim()) {
        LOG_E("Cannot start SD logging - SD card busy\\r\\n");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    pSDCardSettings->mode = SD_CARD_MANAGER_MODE_WRITE;
    bool sdArmed = sd_card_manager_UpdateSettingsForStreamingLog(pSDCardSettings);
''' + _STREAM_REFUSAL + '''    sd_card_manager_ReleaseClaim();
''' + _STREAM_POLL_LOOP + '''    return SCPI_RES_OK;
}
'''

_CHECKS = []


def _ck(name, got, want):
    ok = got == want
    _CHECKS.append(ok)
    if not ok:
        print("  self-test FAIL: %s: got %r want %r" % (name, got, want))


def self_test():
    global KNOWN_PLAIN_ARM_SITES
    saved, KNOWN_PLAIN_ARM_SITES = KNOWN_PLAIN_ARM_SITES, 1
    try:
        probs, n = check(_GOOD)
        _ck("a compliant file is clean", probs, [])
        _ck("both arm sites are examined", n, 2)

        # The parsing this rests on, asserted directly rather than only
        # through a verdict: a definition is not a call site, and a call in a
        # condition is not a definition.
        _ck("the callback parameter is discovered from the signature",
            callback_param(strip_c_comments(_GOOD), ARM_HELPER)[0],
            ("onRefused", 3))
        called = _GOOD + """
static scpi_result_t decoy(scpi_t * c) {
    if (helper_probe(c)) { return SCPI_RES_ERR; }
    return SCPI_RES_OK;
}
"""
        _ck("a call inside a condition is not read as a definition",
            function_body(called, "helper_probe"), None)
        _ck("literal braces do not unbalance the body scan",
            "SCPI_ErrorPush" in function_body(strip_c_comments(_GOOD),
                                              ARM_HELPER), True)

        # ---- #971 acceptance 1: the retraction moved past the release ------
        # The mutation the ticket names. Every other property still holds: the
        # callback is still passed in, still called exactly once, still on the
        # refusal path -- only no longer under the claim.
        after = _GOOD.replace(
            "    if (onRefused != NULL) {\n        onRefused();\n    }\n"
            "    sd_card_manager_ReleaseClaim();",
            "    sd_card_manager_ReleaseClaim();\n"
            "    if (onRefused != NULL) {\n        onRefused();\n    }")
        assert after != _GOOD
        probs, _ = check(after)
        _ck("an onRefused() call AFTER the release is caught",
            any("does not invoke onRefused()" in p for p in probs), True)

        # ---- #971 acceptance 2: the pre-#964 FORmat shape ------------------
        # Retract at the call site, after the helper has released. The helper
        # itself is untouched and still passes property 1.
        pre964 = _GOOD.replace(
            "    if (!SD_ArmOrRefuseWithCleanup(context, \"FORmat\", pCfg,\n"
            "                                   sd_card_manager_ClearFormatStatus)) {\n"
            "        return SCPI_RES_ERR;",
            "    if (!SD_ArmOrRefuse(context, \"FORmat\", pCfg)) {\n"
            "        sd_card_manager_ClearFormatStatus();\n"
            "        return SCPI_RES_ERR;")
        assert pre964 != _GOOD
        probs, _ = check(pre964)
        _ck("the pre-#964 call-site retraction is caught",
            any("calls SD_ArmOrRefuseWithCleanup() 0 times" in p
                for p in probs), True)

        # ...and its half-way twin, which the count check above cannot see:
        # the callback is still passed AND retracted again at the call site.
        both = _GOOD.replace(
            "                                   sd_card_manager_ClearFormatStatus)) {\n"
            "        return SCPI_RES_ERR;",
            "                                   sd_card_manager_ClearFormatStatus)) {\n"
            "        sd_card_manager_ClearFormatStatus();\n"
            "        return SCPI_RES_ERR;")
        assert both != _GOOD
        probs, _ = check(both)
        _ck("retracting at the call site AS WELL is caught",
            any("at its own call site" in p for p in probs), True)

        # A retraction passed as NULL is the retraction deleted.
        nullcb = _GOOD.replace(
            "    if (!SD_ArmOrRefuseWithCleanup(context, \"FORmat\", pCfg,\n"
            "                                   sd_card_manager_ClearFormatStatus)) {",
            "    if (!SD_ArmOrRefuseWithCleanup(context, \"FORmat\", pCfg, NULL)) {")
        assert nullcb != _GOOD
        probs, _ = check(nullcb)
        _ck("passing NULL where FORmat must retract is caught",
            any("passes `NULL`" in p for p in probs), True)

        # ---- property 1: the #955 `mode` clear ------------------------------
        modeafter = _GOOD.replace(
            "    cfg->mode = SD_CARD_MANAGER_MODE_NONE;\n"
            "    if (onRefused != NULL) {\n        onRefused();\n    }\n"
            "    sd_card_manager_ReleaseClaim();",
            "    if (onRefused != NULL) {\n        onRefused();\n    }\n"
            "    sd_card_manager_ReleaseClaim();\n"
            "    cfg->mode = SD_CARD_MANAGER_MODE_NONE;")
        assert modeafter != _GOOD
        probs, _ = check(modeafter)
        _ck("a `mode` clear AFTER the release is caught",
            any("does not clear `cfg->mode`" in p for p in probs), True)

        # Hoisted above the success return: still textually before the last
        # release, and still not on the refusal path. This is why the region
        # starts at the FIRST release rather than at the function's entry.
        hoisted = _GOOD.replace(
            "    if (sd_card_manager_UpdateSettings(cfg)) {",
            "    cfg->mode = SD_CARD_MANAGER_MODE_NONE;\n"
            "    if (sd_card_manager_UpdateSettings(cfg)) {").replace(
            "    cfg->mode = SD_CARD_MANAGER_MODE_NONE;\n"
            "    if (onRefused != NULL) {", "    if (onRefused != NULL) {")
        assert hoisted != _GOOD
        probs, _ = check(hoisted)
        _ck("a clear hoisted above the success exit is caught",
            any("does not clear `cfg->mode`" in p for p in probs), True)

        # A clear that puts back a DIFFERENT object than the one armed.
        other = _GOOD.replace("    cfg->mode = SD_CARD_MANAGER_MODE_NONE;",
                              "    gLastCfg->mode = SD_CARD_MANAGER_MODE_NONE;")
        assert other != _GOOD
        probs, _ = check(other)
        _ck("clearing a different object than the one armed is caught",
            any("arms `cfg` but clears `gLastCfg->mode`" in p for p in probs),
            True)

        # No clear at all, and two clears whose paths cannot be told apart:
        # both leave the property unestablished and both must fail.
        noclear = _GOOD.replace(
            "    cfg->mode = SD_CARD_MANAGER_MODE_NONE;\n", "")
        assert noclear != _GOOD
        _ck("no `mode` clear at all is caught",
            any("statement(s); expected exactly one" in p
                for p in check(noclear)[0]), True)
        twoclear = _GOOD.replace(
            "    cfg->mode = SD_CARD_MANAGER_MODE_NONE;",
            "    cfg->mode = SD_CARD_MANAGER_MODE_NONE;\n"
            "    cfg->mode = SD_CARD_MANAGER_MODE_NONE;")
        _ck("two indistinguishable clears are refused",
            any("statement(s); expected exactly one" in p
                for p in check(twoclear)[0]), True)

        # ---- property 1: the claim must be held ACROSS the arm --------------
        # Hoisting the success-path release above the arm keeps the count at
        # two, so the region (first release, last release) would re-admit both
        # writes -- while the arm itself now runs unowned. This is the arm the
        # "every release follows the arm" test exists for.
        early = _GOOD.replace(
            "    if (sd_card_manager_UpdateSettings(cfg)) {\n"
            "        sd_card_manager_ReleaseClaim();\n"
            "        return true;\n    }",
            "    sd_card_manager_ReleaseClaim();\n"
            "    if (sd_card_manager_UpdateSettings(cfg)) {\n"
            "        return true;\n    }")
        assert early != _GOOD
        probs, _ = check(early)
        _ck("a release hoisted above the arm is caught",
            any("before sd_card_manager_UpdateSettings()" in p
                for p in probs), True)

        norelease = _GOOD.replace(
            "    sd_card_manager_ReleaseClaim();\n"
            "    LOG_E(", "    LOG_E(").replace(
            "        sd_card_manager_ReleaseClaim();\n        return true;",
            "        return true;")
        assert "sd_card_manager_ReleaseClaim" not in norelease
        _ck("a helper that never releases is caught",
            any("never calls sd_card_manager_ReleaseClaim()" in p
                for p in check(norelease)[0]), True)

        threeexits = _GOOD.replace(
            "    sd_card_manager_ReleaseClaim();\n    LOG_E(",
            "    sd_card_manager_ReleaseClaim();\n"
            "    sd_card_manager_ReleaseClaim();\n    LOG_E(")
        assert threeexits != _GOOD
        probs, _ = check(threeexits)
        _ck("three releases are refused, not reasoned about",
            any("ONE claim with one or two exits" in p for p in probs), True)

        # A single-exit restructure is sound and must stay CLEAN: the region
        # is then (arm, release) and both writes are still inside it.
        single = _GOOD.replace(
            "    if (sd_card_manager_UpdateSettings(cfg)) {\n"
            "        sd_card_manager_ReleaseClaim();\n"
            "        return true;\n    }\n"
            "    cfg->mode = SD_CARD_MANAGER_MODE_NONE;\n"
            "    if (onRefused != NULL) {\n        onRefused();\n    }\n"
            "    sd_card_manager_ReleaseClaim();",
            "    bool armed = sd_card_manager_UpdateSettings(cfg);\n"
            "    if (!armed) {\n"
            "        cfg->mode = SD_CARD_MANAGER_MODE_NONE;\n"
            "        if (onRefused != NULL) {\n            onRefused();\n        }\n"
            "    }\n"
            "    sd_card_manager_ReleaseClaim();\n"
            "    if (armed) { return true; }")
        assert single != _GOOD
        _ck("a single-exit restructure is accepted, not red", check(single)[0], [])

        # ---- a region is not a path (Qodo, PR #976) -------------------------
        # THE hole: both writes hoisted out of the failure guard so they run
        # unconditionally between the arm and the release. Every positional
        # test still passes -- and a SUCCESSFUL arm now clears the mode it just
        # set and retracts the state its caller published for it.
        hoistedout = _GOOD.replace(
            "    if (sd_card_manager_UpdateSettings(cfg)) {\n"
            "        sd_card_manager_ReleaseClaim();\n"
            "        return true;\n    }\n"
            "    cfg->mode = SD_CARD_MANAGER_MODE_NONE;\n"
            "    if (onRefused != NULL) {\n        onRefused();\n    }\n"
            "    sd_card_manager_ReleaseClaim();",
            "    bool armed = sd_card_manager_UpdateSettings(cfg);\n"
            "    cfg->mode = SD_CARD_MANAGER_MODE_NONE;\n"
            "    if (onRefused != NULL) {\n        onRefused();\n    }\n"
            "    sd_card_manager_ReleaseClaim();\n"
            "    if (armed) { return true; }")
        assert hoistedout != _GOOD
        probs = check(hoistedout)[0]
        _ck("an unconditional `mode` clear is caught, not just a misplaced one",
            any("clears `cfg->mode` inside the claim but NOT on a branch" in p
                for p in probs), True)
        _ck("an unconditional onRefused() is caught the same way",
            any("invokes onRefused() inside the claim but NOT on a branch" in p
                for p in probs), True)

        # The two-release twin: the success branch takes its release but does
        # not LEAVE, so a successful arm falls through into the refusal writes.
        fallthrough = _GOOD.replace(
            "    if (sd_card_manager_UpdateSettings(cfg)) {\n"
            "        sd_card_manager_ReleaseClaim();\n"
            "        return true;\n    }",
            "    bool armed = sd_card_manager_UpdateSettings(cfg);\n"
            "    if (armed) {\n        sd_card_manager_ReleaseClaim();\n    }")
        assert fallthrough != _GOOD
        _ck("a success block that releases but never returns is caught",
            any("never RETURNS" in p for p in check(fallthrough)[0]), True)

        # A write INSIDE the success block, after that block's own release: it
        # is between the two releases and squarely on the success path. This is
        # why the region starts at the block's END, not at the first release.
        insidesuccess = _GOOD.replace(
            "        sd_card_manager_ReleaseClaim();\n        return true;\n    }\n"
            "    cfg->mode = SD_CARD_MANAGER_MODE_NONE;\n",
            "        sd_card_manager_ReleaseClaim();\n"
            "        cfg->mode = SD_CARD_MANAGER_MODE_NONE;\n"
            "        return true;\n    }\n")
        assert insidesuccess != _GOOD
        _ck("a clear inside the SUCCESS block is caught",
            any("does not clear `cfg->mode`" in p
                for p in check(insidesuccess)[0]), True)

        # The first release at function scope: it then runs on every path, so
        # nothing after it is owned whichever branch took it.
        flatrelease = _GOOD.replace(
            "    if (sd_card_manager_UpdateSettings(cfg)) {\n"
            "        sd_card_manager_ReleaseClaim();\n"
            "        return true;\n    }",
            "    bool armed = sd_card_manager_UpdateSettings(cfg);\n"
            "    sd_card_manager_ReleaseClaim();\n"
            "    if (armed) { return true; }")
        assert flatrelease != _GOOD
        _ck("an unconditional first release is refused",
            any("not inside a conditional block" in p
                for p in check(flatrelease)[0]), True)

        # A success block gated on something that is not the arm's verdict:
        # everything after it would be called the refusal path on no evidence.
        wronggate = _GOOD.replace(
            "    if (sd_card_manager_UpdateSettings(cfg)) {",
            "    bool armed = sd_card_manager_UpdateSettings(cfg);\n"
            "    if (gSomethingElse) {")
        assert wronggate != _GOOD
        _ck("a success block gated on an unrelated condition is refused",
            any("cannot read as the SUCCESS path" in p
                for p in check(wronggate)[0]), True)

        # No identifiable verdict at all: with nothing naming the arm's result,
        # "the success path" has no textual meaning.
        noverdict = _GOOD.replace(
            "    if (sd_card_manager_UpdateSettings(cfg)) {\n"
            "        sd_card_manager_ReleaseClaim();\n"
            "        return true;\n    }",
            "    sd_card_manager_UpdateSettings(cfg);\n"
            "    if (gArmed) {\n        sd_card_manager_ReleaseClaim();\n"
            "        return true;\n    }")
        assert noverdict != _GOOD
        _ck("an arm whose verdict is discarded is refused, not placed",
            any("does not consume sd_card_manager_UpdateSettings()'s return"
                in p for p in check(noverdict)[0]), True)

        # The sound restructures this must NOT red: the `else` side of a
        # true-gated `if`, and the `== false` spelling of the guard.
        elseform = _GOOD.replace(
            "    if (sd_card_manager_UpdateSettings(cfg)) {\n"
            "        sd_card_manager_ReleaseClaim();\n"
            "        return true;\n    }\n"
            "    cfg->mode = SD_CARD_MANAGER_MODE_NONE;\n"
            "    if (onRefused != NULL) {\n        onRefused();\n    }\n"
            "    sd_card_manager_ReleaseClaim();",
            "    bool armed = sd_card_manager_UpdateSettings(cfg);\n"
            "    if (armed) {\n"
            "        (void)armed;\n"
            "    } else {\n"
            "        cfg->mode = SD_CARD_MANAGER_MODE_NONE;\n"
            "        if (onRefused != NULL) {\n            onRefused();\n        }\n"
            "    }\n"
            "    sd_card_manager_ReleaseClaim();\n"
            "    if (armed) { return true; }")
        assert elseform != _GOOD
        _ck("the `else` of a true-gated `if` is a failure branch, not red",
            check(elseform)[0], [])
        cmpform = single.replace("    if (!armed) {", "    if (armed == false) {")
        assert cmpform != single
        _ck("`armed == false` reads as the failure branch too",
            check(cmpform)[0], [])

        # ...and the one it must red rather than interpret: a guard that says
        # something ELSE as well. `&&` narrows and `||` widens; only the second
        # is unsound, and nothing here can tell them apart.
        compound = single.replace("    if (!armed) {",
                                  "    if (!armed && gRetryOnce) {")
        assert compound != single
        _ck("a compound guard is refused rather than interpreted",
            any("NOT on a branch gated by the refused arm" in p
                for p in check(compound)[0]), True)

        # ---- property 1: the callback itself --------------------------------
        never = _GOOD.replace(
            "    if (onRefused != NULL) {\n        onRefused();\n    }\n", "")
        assert never != _GOOD
        _ck("a retraction that is passed but never invoked is caught",
            any("parameter 0 times" in p for p in check(never)[0]), True)

        unguarded = _GOOD.replace(
            "    if (onRefused != NULL) {\n        onRefused();\n    }",
            "    onRefused();")
        assert unguarded != _GOOD
        _ck("an unguarded call through the parameter is caught",
            any("without inspecting it first" in p
                for p in check(unguarded)[0]), True)

        # A rename must be FOLLOWED, not reported.
        renamed = _GOOD.replace("onRefused", "retract")
        assert renamed != _GOOD
        _ck("renaming the callback parameter is followed, not flagged",
            check(renamed)[0], [])

        # The claim taken INSIDE the helper changes the model this reasons
        # about, so it is refused rather than reinterpreted.
        inside = _GOOD.replace(
            "    if (sd_card_manager_UpdateSettings(cfg)) {",
            "    if (!sd_card_manager_TryClaim()) { return false; }\n"
            "    if (sd_card_manager_UpdateSettings(cfg)) {")
        assert inside != _GOOD
        _ck("a claim taken inside the helper is refused",
            any("supposed to be the CALLER's" in p
                for p in check(inside)[0]), True)

        # ---- property 3: the census ----------------------------------------
        second = _GOOD.replace(
            "    if (!SD_ArmOrRefuse(context, \"SPACe\", pCfg)) {",
            "    if (!SD_ArmOrRefuseWithCleanup(context, \"SPACe\", pCfg,\n"
            "                                   sd_card_manager_ClearFormatStatus)) {")
        assert second != _GOOD
        probs, _ = check(second)
        _ck("a SECOND WithCleanup caller must be a deliberate choice",
            any("expected exactly one caller" in p for p in probs), True)

        notnull = _GOOD.replace(
            "    return SD_ArmOrRefuseWithCleanup(context, cmd, cfg, NULL);",
            "    return SD_ArmOrRefuseWithCleanup(context, cmd, cfg, "
            "sd_card_manager_ClearFormatStatus);")
        assert notnull != _GOOD
        probs, _ = check(notnull)
        _ck("the wrapper passing something other than NULL is caught",
            any("rather than NULL" in p for p in probs), True)

        # Property 1, caller side: the arm without the claim ahead of it.
        noclaim = _GOOD.replace(
            "    if (!SD_ClaimOrRefuse(context, \"SPACe\")) {\n"
            "        return SCPI_RES_ERR;\n    }\n", "")
        assert noclaim != _GOOD
        probs, _ = check(noclaim)
        _ck("an arm with no claim taken in its own function is caught",
            any("calls SD_ClaimOrRefuse() 0 times" in p for p in probs), True)

        late = _GOOD.replace(
            "    if (!SD_ClaimOrRefuse(context, \"SPACe\")) {\n"
            "        return SCPI_RES_ERR;\n    }\n"
            "    pCfg->mode = SD_CARD_MANAGER_MODE_GET_SPACE;\n"
            "    if (!SD_ArmOrRefuse(context, \"SPACe\", pCfg)) {\n"
            "        return SCPI_RES_ERR;\n    }",
            "    pCfg->mode = SD_CARD_MANAGER_MODE_GET_SPACE;\n"
            "    if (!SD_ArmOrRefuse(context, \"SPACe\", pCfg)) {\n"
            "        return SCPI_RES_ERR;\n    }\n"
            "    if (!SD_ClaimOrRefuse(context, \"SPACe\")) {\n"
            "        return SCPI_RES_ERR;\n    }")
        assert late != _GOOD
        probs, _ = check(late)
        _ck("a claim taken AFTER the arm is caught",
            any("arms before it calls SD_ClaimOrRefuse()" in p
                for p in probs), True)

        # ---- property 2: the publish that makes the retraction necessary ---
        nopub = _GOOD.replace("    sd_card_manager_SetFormatPending();\n", "")
        assert nopub != _GOOD
        probs, _ = check(nopub)
        _ck("FORmat with no publish at all is refused, not passed",
            any("expected exactly one publish" in p for p in probs), True)

        latepub = _GOOD.replace(
            "    sd_card_manager_SetFormatPending();\n"
            "    pCfg->mode = SD_CARD_MANAGER_MODE_FORMAT;\n"
            "    if (!SD_ArmOrRefuseWithCleanup(context, \"FORmat\", pCfg,\n"
            "                                   sd_card_manager_ClearFormatStatus)) {\n"
            "        return SCPI_RES_ERR;\n    }",
            "    pCfg->mode = SD_CARD_MANAGER_MODE_FORMAT;\n"
            "    if (!SD_ArmOrRefuseWithCleanup(context, \"FORmat\", pCfg,\n"
            "                                   sd_card_manager_ClearFormatStatus)) {\n"
            "        return SCPI_RES_ERR;\n    }\n"
            "    sd_card_manager_SetFormatPending();")
        assert latepub != _GOOD
        probs, _ = check(latepub)
        _ck("publishing AFTER the arm is caught",
            any("publishes format-pending AFTER arming" in p
                for p in probs), True)

        # ---- comments are not code -----------------------------------------
        commented = _GOOD.replace(
            "    if (onRefused != NULL) {\n        onRefused();\n    }",
            "    /* if (onRefused != NULL) { onRefused(); } */")
        assert commented != _GOOD
        _ck("a commented-out retraction does not count as one",
            any("parameter 0 times" in p for p in check(commented)[0]), True)
        mention = _GOOD.replace(
            "    if (onRefused != NULL) {\n        onRefused();\n    }",
            '    const char *m = "onRefused();"; (void)m;')
        assert mention != _GOOD
        _ck("a string mention is not accepted as a call",
            any("parameter 0 times" in p for p in check(mention)[0]), True)

        # ---- vacuity: a file this cannot read must FAIL ---------------------
        probs, n2 = check("int main(void) { return 0; }")
        _ck("a source with no arm helper fails rather than passing",
            any("not found" in p for p in probs), True)
        _ck("...and reports nothing examined", n2, 0)

        gutted = _GOOD.replace(
            "static bool SD_ArmOrRefuseWithCleanup(scpi_t *context, const char *cmd,\n"
            "                                      sd_card_manager_settings_t *cfg,\n"
            "                                      void (*onRefused)(void))",
            "static bool SD_ArmOrRefuseWithCleanup(scpi_t *context, const char *cmd,\n"
            "                                      sd_card_manager_settings_t *cfg)")
        assert gutted != _GOOD
        probs, _ = check(gutted)
        _ck("an unidentifiable retraction parameter is refused, not skipped",
            any("UNVERIFIED" in p for p in probs), True)

        noformat = _GOOD.replace("SCPI_StorageSDFormat", "SCPI_StorageSDFmt")
        assert noformat != _GOOD
        probs, _ = check(noformat)
        _ck("FORmat missing is refused rather than quietly unchecked",
            any("was NOT checked" in p for p in probs), True)

        nosites = _GOOD.split("scpi_result_t SCPI_StorageSDSpaceGet")[0]
        probs, n3 = check(nosites)
        _ck("a file with no arm sites at all fails",
            any("the file moved or the call scan broke" in p for p in probs),
            True)
        _ck("...and reports nothing examined", n3, 0)
    finally:
        KNOWN_PLAIN_ARM_SITES = saved

    # ======================================================================
    # properties 4 and 5: the streaming-log arm in SCPIInterface.c (#942)
    # ======================================================================
    probs, n = check_stream(_GOOD_STREAM)
    _ck("a compliant streaming-log arm is clean", probs, [])
    _ck("...and reports the one site examined", n, 1)

    # ---- property 4: the verdict must go somewhere -------------------------
    # The #942 shape itself, verbatim: the call as a bare statement and no
    # refusal branch at all.
    discarded = _GOOD_STREAM.replace(
        "    bool sdArmed = sd_card_manager_UpdateSettingsForStreamingLog",
        "    sd_card_manager_UpdateSettingsForStreamingLog").replace(
        _STREAM_REFUSAL, "")
    assert discarded != _GOOD_STREAM
    _ck("the pre-#942 discarded return is caught",
        any("DISCARDS" in p for p in check_stream(discarded)[0]), True)

    voided = _GOOD_STREAM.replace(
        "    bool sdArmed = sd_card_manager_UpdateSettingsForStreamingLog",
        "    (void)sd_card_manager_UpdateSettingsForStreamingLog").replace(
        _STREAM_REFUSAL, "")
    assert voided != _GOOD_STREAM
    _ck("a deliberate (void) discard gets its own message",
        any("casts" in p and "`(void)`" in p
            for p in check_stream(voided)[0]), True)

    untested = _GOOD_STREAM.replace(_STREAM_REFUSAL, "")
    assert untested != _GOOD_STREAM
    _ck("a verdict captured and never branched on is caught",
        any("no `if` after the arm tests it" in p
            for p in check_stream(untested)[0]), True)

    # An inline test captures nothing and consumes the verdict completely --
    # the spelling the other file's helper uses. It must NOT be red.
    inline = _GOOD_STREAM.replace(
        "    bool sdArmed = sd_card_manager_UpdateSettingsForStreamingLog(pSDCardSettings);\n"
        "    if (!sdArmed) {",
        "    if (!sd_card_manager_UpdateSettingsForStreamingLog(pSDCardSettings)) {")
    assert inline != _GOOD_STREAM
    _ck("an inline test of the arm is accepted, not red",
        check_stream(inline)[0], [])
    renamed = _GOOD_STREAM.replace("sdArmed", "armedOk")
    assert renamed != _GOOD_STREAM
    _ck("renaming the captured verdict is followed, not flagged",
        check_stream(renamed)[0], [])

    # ---- property 5: the refusal beats the poll ----------------------------
    # THE acceptance mutation: the branch still exists, still returns, still
    # clears under the claim -- it is simply reached after the poll has already
    # spent its five seconds.
    moved = _GOOD_STREAM.replace(_STREAM_REFUSAL, "").replace(
        "    return SCPI_RES_OK;", _STREAM_REFUSAL + "    return SCPI_RES_OK;")
    assert moved != _GOOD_STREAM
    _ck("a refusal branch positioned after the poll is caught",
        any("positioned AFTER the first" in p
            for p in check_stream(moved)[0]), True)

    noreturn = _GOOD_STREAM.replace(
        "        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);\n"
        "        return SCPI_RES_ERR;\n    }\n"
        "    sd_card_manager_ReleaseClaim();",
        "        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);\n    }\n"
        "    sd_card_manager_ReleaseClaim();")
    assert noreturn != _GOOD_STREAM
    _ck("a refusal branch that falls through into the poll is caught",
        any("does not RETURN" in p for p in check_stream(noreturn)[0]), True)

    # A success-shaped test: the refusal is then in an `else` or a
    # fall-through, which this cannot place. Refused, and said to be refused.
    successtest = _GOOD_STREAM.replace("    if (!sdArmed) {",
                                       "    if (sdArmed) {")
    assert successtest != _GOOD_STREAM
    _ck("a success-shaped test is refused rather than guessed at",
        any("read as the REFUSAL branch" in p
            for p in check_stream(successtest)[0]), True)

    braceless = _GOOD_STREAM.replace(
        _STREAM_REFUSAL, "    if (!sdArmed) return SCPI_RES_ERR;\n")
    assert braceless != _GOOD_STREAM
    _ck("a brace-less refusal is refused: its body cannot be read",
        any("no braced block" in p for p in check_stream(braceless)[0]), True)

    # ---- property 5, the claim half ----------------------------------------
    leaked = _GOOD_STREAM.replace(
        "        pSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;\n"
        "        sd_card_manager_ReleaseClaim();\n",
        "        pSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;\n")
    assert leaked != _GOOD_STREAM
    _ck("a refusal that never releases the claim is caught",
        any("calls sd_card_manager_ReleaseClaim() 0 times" in p
            for p in check_stream(leaked)[0]), True)

    lateclear = _GOOD_STREAM.replace(
        "        pSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;\n"
        "        sd_card_manager_ReleaseClaim();\n",
        "        sd_card_manager_ReleaseClaim();\n"
        "        pSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;\n")
    assert lateclear != _GOOD_STREAM
    _ck("clearing `mode` after the release is caught (#955 at the new site)",
        any("AFTER sd_card_manager_ReleaseClaim()" in p
            for p in check_stream(lateclear)[0]), True)

    noclear = _GOOD_STREAM.replace(
        "        pSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;\n", "")
    assert noclear != _GOOD_STREAM
    _ck("a refusal that leaves `mode` advertising a WRITE is caught",
        any("expected exactly one" in p and "MODE_NONE" in p
            for p in check_stream(noclear)[0]), True)

    otherobj = _GOOD_STREAM.replace(
        "        pSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;",
        "        gLastSdSettings->mode = SD_CARD_MANAGER_MODE_NONE;")
    assert otherobj != _GOOD_STREAM
    _ck("clearing a different object than the one armed is caught",
        any("arms `pSDCardSettings` but its refusal clears" in p
            for p in check_stream(otherobj)[0]), True)

    noclaim = _GOOD_STREAM.replace(
        "    if (!sd_card_manager_TryClaim()) {\n"
        "        LOG_E(\"Cannot start SD logging - SD card busy\\r\\n\");\n"
        "        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);\n"
        "        return SCPI_RES_ERR;\n    }\n", "")
    assert noclaim != _GOOD_STREAM
    _ck("arming with no claim taken is caught",
        any("calls sd_card_manager_TryClaim() 0 times" in p
            for p in check_stream(noclaim)[0]), True)
    lateclaim = noclaim.replace(
        "    sd_card_manager_ReleaseClaim();\n",
        "    if (!sd_card_manager_TryClaim()) {\n"
        "        return SCPI_RES_ERR;\n    }\n"
        "    sd_card_manager_ReleaseClaim();\n", 1)
    assert lateclaim != noclaim
    _ck("a claim taken AFTER the arm is caught",
        any("arms before it calls sd_card_manager_TryClaim()" in p
            for p in check_stream(lateclaim)[0]), True)

    # ---- comments and literals are not code --------------------------------
    commented = _GOOD_STREAM.replace(
        _STREAM_REFUSAL, "/*\n" + _STREAM_REFUSAL + "*/\n")
    assert commented != _GOOD_STREAM
    _ck("a commented-out refusal branch does not count as one",
        any("no `if` after the arm tests it" in p
            for p in check_stream(commented)[0]), True)
    mention = _GOOD_STREAM.replace(
        _STREAM_REFUSAL,
        '    const char *m = "if (!sdArmed) { return SCPI_RES_ERR; }";\n'
        "    (void)m;\n")
    assert mention != _GOOD_STREAM
    _ck("a refusal branch spelled in a string literal is not one",
        any("no `if` after the arm tests it" in p
            for p in check_stream(mention)[0]), True)

    # ---- vacuity: what this cannot read must FAIL ---------------------------
    probs, n2 = check_stream("int main(void) { return 0; }")
    _ck("a source with no streaming-start function fails rather than passing",
        any("not found" in p for p in probs), True)
    _ck("...and reports nothing examined", n2, 0)

    twoarms = _GOOD_STREAM.replace(
        "    bool sdArmed = sd_card_manager_UpdateSettingsForStreamingLog(pSDCardSettings);",
        "    bool sdArmed = sd_card_manager_UpdateSettingsForStreamingLog(pSDCardSettings);\n"
        "    sdArmed = sd_card_manager_UpdateSettingsForStreamingLog(pSDCardSettings);")
    assert twoarms != _GOOD_STREAM
    probs, n3 = check_stream(twoarms)
    _ck("two arms are refused, not reasoned about",
        any("2 times" in p for p in probs), True)
    _ck("...and report nothing examined", n3, 0)

    noarm = _GOOD_STREAM.replace(
        "sd_card_manager_UpdateSettingsForStreamingLog", "sd_card_manager_Arm")
    assert noarm != _GOOD_STREAM
    _ck("the arm renamed away is refused, not silently unchecked",
        any("0 times" in p for p in check_stream(noarm)[0]), True)

    nopoll = _GOOD_STREAM.replace(_STREAM_POLL_LOOP, "")
    assert nopoll != _GOOD_STREAM
    _ck("with the poll gone the ordering is UNVERIFIED, not passed",
        any("never calls sd_card_manager_IsWriteReady()" in p
            for p in check_stream(nopoll)[0]), True)

    bad = _CHECKS.count(False)
    print("self-test: %d/%d checks passed" % (_CHECKS.count(True), len(_CHECKS)))
    return 1 if bad else 0


def _read(path, flag):
    """The text of `path`, or exit saying which flag would move it."""
    if not os.path.isfile(path):
        sys.exit("error: %r not found (run from the repo root, or pass %s)"
                 % (path, flag))
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sd", default="firmware/src/services/SCPI/SCPIStorageSD.c",
                    help="path to SCPIStorageSD.c (properties 1-3)")
    ap.add_argument("--interface",
                    default="firmware/src/services/SCPI/SCPIInterface.c",
                    help="path to SCPIInterface.c (properties 4-5)")
    ap.add_argument("--self-test", action="store_true",
                    help="run the built-in checks and exit (no source needed)")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    # BOTH, always. An entry point that can be skipped is one that can report a
    # pass having examined nothing, which is the failure this file's vacuity
    # cases exist to refuse.
    problems, examined = check(_read(args.sd, "--sd"))
    stream, stream_examined = check_stream(_read(args.interface, "--interface"))

    if problems or stream:
        print("FAIL: SD arm-refusal ordering check (%d arm site(s) in %s, %d "
              "in %s)" % (examined, os.path.basename(args.sd), stream_examined,
                          os.path.basename(args.interface)))
        for path, found in ((args.sd, problems), (args.interface, stream)):
            for p in found:
                print("  - %s: %s" % (os.path.basename(path), p))
        return 1
    print("OK: %d arm site(s) claim before arming; %s() holds the claim across "
          "%s() and clears `%s` and invokes the caller's retraction on the "
          "refusal side of it, behind a success path that returns; %s() "
          "reaches its retraction through that parameter and not at its own "
          "call site; the other sites go through %s(), which passes NULL. In "
          "%s(), %s()'s verdict is checked and its refusal clears `%s` under "
          "the claim and returns before %s()"
          % (examined, ARM_HELPER, ARM_CALL, MODE_FIELD, FORMAT_FN,
             ARM_WRAPPER, STREAM_FN, STREAM_ARM_CALL, MODE_FIELD, STREAM_POLL))
    return 0


if __name__ == "__main__":
    sys.exit(main())
