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

## The three properties

1. **The helper holds the claim across the arm, and both refusal-path writes
   fall inside it.** In `SD_ArmOrRefuseWithCleanup`: the arm
   (`sd_card_manager_UpdateSettings`) precedes every
   `sd_card_manager_ReleaseClaim`, and the `mode` clear and the `onRefused`
   invocation both fall on the refusal side of the success-path release and
   before the refusal-path release. Caller-side: every function that arms
   calls `SD_ClaimOrRefuse` exactly once, before its arm.
2. **FORmat reaches its retraction THROUGH the callback parameter.**
   `SCPI_StorageSDFormat` publishes format-pending before arming, passes a
   non-NULL retraction in the callback slot, and does NOT also call that
   function at its own call site -- the shape #964 removed.
3. **The five commands with nothing published before the arm go through the
   `SD_ArmOrRefuse` wrapper**, which passes NULL. Exactly one call site uses
   the `WithCleanup` form, and it is FORmat's, so a future caller that
   publishes state before arming has to make a deliberate choice here rather
   than inherit silence.

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

With two releases the refusal region is `(first release, last release)`, which
is what makes "on the refusal path" decidable: a write hoisted above the
success return leaves the region, and so does one moved past the release. With
one release the region is `(arm, release)`. Requiring every release to follow
the arm is what stops a release hoisted above the arm from re-admitting both
writes into a region that no longer holds anything.

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
    """Property 1, helper side: the claim is held across the arm and both
    refusal-path writes fall inside it."""
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

    # The refusal region. Two releases: after the success exit, before the
    # refusal exit. One release: after the arm, before that exit.
    if len(releases) == 2:
        lo, hi = releases[0], releases[1]
        where = ("between the success-path %s() and the refusal-path one"
                 % RELEASE_CLAIM)
    else:
        lo, hi = arm, releases[0]
        where = "between %s() and the release" % ARM_CALL

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

    # (1f) #964: the caller-published retraction, through the callback param
    return problems + _callback_problems(text, helper, lo, hi, where)


def _callback_problems(text, helper, lo, hi, where):
    """Property 1, the `onRefused` half: called exactly once, inside the
    region, and inspected before it is called."""
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

    bad = _CHECKS.count(False)
    print("self-test: %d/%d checks passed" % (_CHECKS.count(True), len(_CHECKS)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sd", default="firmware/src/services/SCPI/SCPIStorageSD.c",
                    help="path to SCPIStorageSD.c")
    ap.add_argument("--self-test", action="store_true",
                    help="run the built-in checks and exit (no source needed)")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    if not os.path.isfile(args.sd):
        sys.exit("error: %r not found (run from the repo root, or pass --sd)"
                 % args.sd)
    with open(args.sd, "r", encoding="utf-8", errors="replace") as fh:
        problems, examined = check(fh.read())

    if problems:
        print("FAIL: SD arm-refusal ordering check (%d arm site(s) examined)"
              % examined)
        for p in problems:
            print("  - %s" % p)
        return 1
    print("OK: %d arm site(s) claim before arming; %s() holds the claim across "
          "%s() and clears `%s` and invokes the caller's retraction on the "
          "refusal side of it; %s() reaches its retraction through that "
          "parameter and not at its own call site; the other sites go through "
          "%s(), which passes NULL"
          % (examined, ARM_HELPER, ARM_CALL, MODE_FIELD, FORMAT_FN,
             ARM_WRAPPER))
    return 0


if __name__ == "__main__":
    sys.exit(main())
