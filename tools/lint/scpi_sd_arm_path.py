#!/usr/bin/env python3
"""SD arm-refusal call-site shape: census, and simple linear ordering only.

## The hazard these checks came from

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
The race needs a preemption window that cannot be aimed at from a client:
single-threaded, the entry guard always wins, so the companion tests
(daqifi-python-test-suite#317) assert equivalence and the new contract, not the
ordering. #971 is that gap, and it is closed by
`tests/host/test_971_sd_arm_refusal_order.c`, NOT by this file -- see "What
moved to a host test" immediately below for why, and "The properties this
file still asserts" for what this file keeps instead.

## What moved to a host test, and why (#976)

This file used to ALSO assert that both writes fall INSIDE
`SD_ArmOrRefuseWithCleanup()`'s claim AND run only on the refusal path -- a
claim about which BRANCH a write sits in, not just which call it sits between.
Three review rounds on #976 catalogued FIFTEEN separate ways an honest
refactor of that one helper defeats a textual check of that shape; four
representative ones: a write hoisted out of the failure guard so it runs on
every path; a release moved so the region it "protects" stops meaning
anything; a ternary rewritten to an if/else that moves the branch's only
`return` one level down; a compound guard whose second term a regex swallows.
Each round bought back correctness against the ONE mutation it was written
for and left the next equivalent rewrite free to defeat it again.

That ordering is now covered by `tests/host/test_971_sd_arm_refusal_order.c`
instead: a deterministic model of `SD_ArmOrRefuseWithCleanup()`'s body, plus a
sha256 drift pin on the real function's (splice-joined, comment-stripped,
whitespace-normalised) text. An edit to the real ordering either fails the model or is
caught retyping the pin -- either way a human has to look, which a regex that
can always be phrased around cannot force. #896 already tracks that this
family of checker is a guard against honest regression, never against a
determined refactor; #976 is the case where that limit was actually reached on
this file, not just theorised about, so the ordering moved to a tool that does
not share it.

**What this file no longer establishes about the helper:** that the `mode`
clear or the `onRefused()` call happen inside the claim, that either runs only
on the refusal path, or that the claim is held across the arm at all. That is
`test_971_sd_arm_refusal_order.c`'s job now. What is left here is census,
statement shape (property 4, below), and simple LINEAR ordering -- "does X
call Y", "does X call Y before Z" -- over the whole function, none of which
needs to know which branch a line is in, and none of which was one of the
fifteen shapes catalogued above.

## The same shape at a second site, in a second file -- same limit, same gap

#942 (PR #974) found the identical defect in `SCPI_StartStreamingClaimed`
(`SCPIInterface.c`): the streaming-log arm,
`sd_card_manager_UpdateSettingsForStreamingLog`, with its return DISCARDED.
The #589 suspend pre-check ~50 lines above closes the common case; what it
cannot close is the LOST RACE -- a WiFi FW update, a WiFi-streaming start on
the other transport, or a bus-jam quarantine landing between that check and
the arm. Fixing it added an analogous positional check here: verdict consumed,
refusal branch positioned before the readiness poll, `mode` cleared then
released inside that branch, claim taken once before the arm -- the same
branch-gated reasoning as the helper's, at a second site, because Qodo raised
the same objection #971 raises above and the race is equally unreachable from
a client.

That positional half is now DELETED by this same change, for the same reason
as the helper's: it is branch-gated reasoning about ONE function's control
flow, the exact shape three rounds spent failing to defend. Unlike the helper,
**no host-test model exists yet for `SCPI_StartStreamingClaimed`** -- filed as
**#998** rather than left silently uncovered. Until that lands, the refusal-
vs-poll ordering #942 fixed (refusal beats the poll, `mode` cleared before
release) has NO automated check anywhere in this tree. The claim-before-arm
half of that same fix is NOT branch-gated -- it is a plain "A before B" over
the whole function, one of the fifteen shapes above never touched it -- so it
stays checked here (property 1, below), unlike the rest of what #942 fixed.

## The properties this file still asserts

1. **At every arm site, in EITHER file, the enclosing function arms exactly
   once and takes the manager's claim exactly once, before that arm.** The arm
   COUNT is half of it and was missing here until Qodo raised it on #976: both
   helpers release the claim on both of their paths (#955), so a second arm in
   one function runs unowned however the first turned out, and "every arm is
   after the one claim" passes it. `_stream_arm_problems` had enforced the
   count at the seventh site all along; this is its twin arriving late. In `SCPIStorageSD.c` this
   is `SD_ClaimOrRefuse()`; `SCPI_StartStreamingClaimed()`
   (`SCPIInterface.c`) has no such wrapper and takes
   `sd_card_manager_TryClaim()` directly, so the two are checked separately
   but assert the same thing. A plain "A before B" over the WHOLE function --
   not a claim about which branch either call sits in, so it does not share
   the fate of the helper-internal reasoning removed above. This is the
   surviving half of what #971 originally called "the claim precedes the
   arm"; the helper-internal half (which write falls on which branch) is the
   property that moved above.
2. **FORmat reaches its retraction THROUGH the callback parameter, and
   publishes between the claim and the arm.** `SCPI_StorageSDFormat` takes the
   claim, THEN publishes format-pending, THEN arms; passes a non-NULL
   retraction in the callback slot; and does NOT also call that function at its
   own call site -- the shape #964 removed. All three legs of #829's ordering
   are compared, `claim < publish < arm`; until the #976 pre-merge audit the
   first leg was asserted in this file's MESSAGE and nowhere in its code, so
   hoisting the publish above the claim passed cleanly.

   Like property 4, this is the ARGUMENT SLOT's spelling, not its value. The
   check is "the callback argument is not the literal `NULL`"; it does not
   establish that the identifier denotes a callable, so a local variable
   holding null passes it (Qodo, #976). That is the same class as property 4's
   comma-operator discard and gets the same answer: adding a case for one
   spelling invites the next, and what actually closes it is the host-test
   model's NULL-retraction case in
   `tests/host/test_971_sd_arm_refusal_order.c`, which exercises the helper
   with a null callback and asserts it still clears and releases. Read a green
   run as "the slot is not literally NULL", never as "a retraction runs".
3. **The five commands with nothing published before the arm go through the
   `SD_ArmOrRefuse` wrapper**, which passes NULL. Exactly one call site uses
   the `WithCleanup` form, and it is FORmat's, so a future caller that
   publishes state before arming has to make a deliberate choice here rather
   than inherit silence.
4. **The streaming-log arm's verdict is consumed.** In
   `SCPI_StartStreamingClaimed` (`SCPIInterface.c`): the one call to
   `sd_card_manager_UpdateSettingsForStreamingLog` is captured into a variable
   or tested inline in an `if` condition -- not left as a bare expression
   statement, and not cast to `(void)`. This establishes only that the verdict
   is CONSUMED, never that it is later BRANCHED ON: a captured verdict nobody
   ever tests again is no longer flagged here (see the self-test for the
   mutation this now accepts, and "The same shape at a second site" above for
   what covers -- and does not yet cover -- the rest of that shape).

   (Property 5, below, is what every count in 1-4 now rests on.)

   And "consumed" is a SHAPE, not dataflow. This matches the spelling of the
   call against the spellings that discard it; it does not trace where the
   value goes. `if ((arm(cfg), false))` puts the call inside an `if` condition
   and throws the verdict away, and this reads it as consumed. That is not a
   hole to be patched -- it is the same class as the fifteen rewrites that
   removed this file's branch-gated half, and patching it invites the next
   spelling. What closes it is a tool with control flow: the host-test model
   for the helper (#971), and #998 for this second site. Read a green run as
   "the obvious discards are absent", never as "the verdict is used".

5. **Every occurrence of every name 1-4 are computed from is accounted for,
   and the census finds exactly the calls accounting found, at the same
   positions.** Properties 1-4 are COUNTS and ORDERINGS -- arms per
   function, the claim before the arm, the one publish between them, the
   one WithCleanup caller -- and each is only as good as the scan that
   produced it. Every audit round after round 5 found another valid
   spelling that dropped live code out of that scan while the counts still
   read correctly (a helper with two `__attribute__` prefixes next to an
   `#if 0` original; a second arm written `(F)(...)`). So:

   * the census reads the source the way the compiler does
     (`compiler_text()`: backslash-newline splices joined first, then
     comments and literals blanked) -- the same characters occurrence
     accounting reads, so a splice-made comment marker, a splice-closed
     comment or a split identifier is seen identically by both;
   * `cdef.account_occurrences` requires each occurrence of `SD_ACCOUNTED` /
     `STREAM_ACCOUNTED`, found with no grammar, to be a definition, a
     prototype or an EVALUATED call; anything else (an alias, `&F`,
     `p = F;`, a declaration, `sizeof F(x)`, `p->F(x)`, a spelling nobody
     has thought of) is reported at its physical line;
   * and, per name, every call the census counts must be a call accounting
     classified AT THE SAME POSITION, and vice versa. Positions, not
     per-function counts, are the point: a claim the census reads before
     the arm and the compiler never makes (`my$F(...)` is another function
     to GCC), plus the real claim made after the arm in a spelling the
     census cannot read (`(F)(...)`), keeps every count equal while the
     claim moves -- that pair passed clean until the #976 review.

   Out of reach, by construction: a name that is not in the text -- macro
   token pasting (`CAT(SD_ArmOr, Refuse)(...)`), or an alias defined in a
   header -- and anything that needs the preprocessor evaluated (a claim
   inside `#if 0` still counts as a claim) or reachability (`if (0)`). See
   `cdef.line_comment_end` and the `cdef` module docstring.

## An inline test is accepted (#942's follow-up did not spell this out)

The directive was "the return must be CAPTURED into a variable, not left as a
bare expression statement". `if (!sd_card_manager_UpdateSettingsForStreamingLog(cfg)) { ... }`
captures nothing and consumes the verdict just as completely -- it is the
spelling `SD_ArmOrRefuse`'s five callers use in the other file, and the
spelling `SD_ArmOrRefuseWithCleanup` itself uses. Demanding the variable would
red a correct restructure. What is refused is the verdict going NOWHERE: a
bare statement, an explicit `(void)` cast (its own message -- a deliberate
discard is still a discard, and naming it says which one happened), or a
consumer this checker cannot recognise at all.

## Two files, two entry points

`check()` reads `SCPIStorageSD.c` (property 1 at its six sites there, plus 2
and 3) and `check_stream()` reads `SCPIInterface.c` (property 1 at its
seventh site, plus 4); each also asserts property 5 for its own names;
`main()` requires BOTH and defaults the second to
`--interface`. Separate entry points rather than one call taking two texts,
because an optional second text is a vacuity hazard -- a run that silently
examines one site and reports a pass is the failure half this file's self-test
exists to refuse. Each fails if it cannot find its own function.

## What a green run does NOT mean

Textual, no control flow, no reachability, and -- since #976 -- no reasoning
about which branch of a function a line sits in. The same stated limits as
`scpi_claim_path.py`, tracked as #896, which this file does not close.

* Property 2 establishes that FORmat's own body names the retraction it
  passes in exactly once -- as that argument -- and that no preprocessor
  directive in the file names it at all, so neither a plain call, a `(F)()`
  call, a pointer taken to it nor an alias can retract at the call site. It
  does not see a retraction reached through another NAME -- a function
  FORmat calls that calls it, or a pointer to it initialised outside FORmat
  -- and it does not establish that no OTHER function retracts FORmat's
  state after a release: only FORmat's own body is read.
* Nothing here says the claim is a real exclusion, or that `UpdateSettings`
  actually arms. That is the manager's business and this file never opens it.
* Property 4 establishes the streaming-log arm's verdict is consumed. It does
  NOT establish it is ever tested again, that a refusal exits before the
  readiness poll, or that `mode` is cleared under the claim -- see "The same
  shape at a second site" above; none of that is checked by anything today.
* Whether the claim's RELEASE runs on every path, on either file, is not
  examined by anything here -- it never was, and even less is asked of this
  file after #976 than before it.

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
# THE definition rule, in one place. This file used to carry three matchers of
# its own (`_DEF`, `function_body`, `signature_params`) and `hash_function.py`
# carried a fourth, with a fifth grep in `tests/host/Makefile`. Six audit
# findings across three rounds were all the same thing: two of those five
# disagreeing about what a definition looks like. `cdef` is that rule and its
# own self-test; see its module docstring for the six.
from cdef import (AmbiguousDefinition, ANY_DEF, one_definition)  # noqa: E402
# ...and the span primitives (`match_brace`, `function_spans`, `enclosing`)
# live there too now, rather than in a copy here that could drift from the
# one occurrence accounting uses to decide what is a call site -- and so does
# the compiler's view itself. The census used to scan
# `scpi_wiki_sync.strip_c_comments(source)`, RAW text with its comments
# collapsed, while occurrence accounting read the splice-joined text the
# compiler reads. Two views of one file is how a splice-closed comment hid a
# call-site retraction from the census, and how two misreadings that cancel
# in a count got through (#976 review). There is one view now:
# `compiler_text()`, below.
from cdef import (account_occurrences, classify_occurrences,  # noqa: E402
                  compiler_view, directive_lines, enclosing, function_spans,
                  identifier_re, match_brace)

# The helper that owns the refusal path, its NULL-passing wrapper, and the
# claim taker each arm site must go through first. The manager primitives
# this file used to reason about POSITIONALLY inside the helper's own body
# (TryClaim/ReleaseClaim/UpdateSettings) are gone along with that reasoning --
# see "What moved to a host test" in the module docstring.
ARM_HELPER = "SD_ArmOrRefuseWithCleanup"
ARM_WRAPPER = "SD_ArmOrRefuse"
CLAIM_TAKER = "SD_ClaimOrRefuse"
# FORmat: the one caller that publishes state before arming, and the call that
# publishes it. Both names are spelled out rather than discovered -- unlike the
# retraction, which IS discovered (from the argument FORmat passes), because
# there the discovery is what keeps a rename from silently disarming property
# 2. A rename of either name below reds the gate loudly instead, which is the
# safe direction: it costs one line here and cannot be mistaken for a pass.
FORMAT_FN = "SCPI_StorageSDFormat"
FORMAT_PUBLISH = "sd_card_manager_SetFormatPending"
# The refusal-path clear, named here only for the messages the caller-side
# census and the streaming-site discard message print -- this file no longer
# checks WHERE either helper clears it (see the module docstring).
MODE_FIELD = "mode"
MODE_NONE = "SD_CARD_MANAGER_MODE_NONE"

# ---- the second site: the streaming-log arm in SCPIInterface.c (#942/#974) --
# `static`, one caller (SCPI_StartStreaming), and the arm is a ~130-line slice
# of it. Named rather than discovered for the same reason FORMAT_FN is: a
# rename reds this gate loudly, which is the safe direction.
STREAM_FN = "SCPI_StartStreamingClaimed"
STREAM_ARM_CALL = "sd_card_manager_UpdateSettingsForStreamingLog"
# The poll the refusal has to beat, named here only for the discard/void
# messages' own explanation of the consequence of NOT consuming the verdict --
# this file no longer checks the poll's POSITION relative to the refusal (see
# the module docstring).
STREAM_POLL = "sd_card_manager_IsWriteReady"
# This site takes the manager's claim directly; there is no SD_ClaimOrRefuse
# in SCPIInterface.c. Property 1's caller-side shape (exactly one claim,
# taken before the arm) is a plain linear ordering, not branch-gated, so it
# survives here the same way it does at the six SCPIStorageSD.c sites.
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
# `(?:\n[ \t]*)?` allows the GNU style that puts the return type on its own
# line -- `static bool\nName(void)\n{`. Without it the pattern was anchored
# within ONE line, so a definition written that way was invisible to the
# definition scanner: its body became no span, every call inside it was
# attributed to nothing, and the site dropped silently out of the census while
# the floor count still passed (#976 pre-merge audit). Ordinary formatting, not
# an adversarial construct. ONE break only, so the prefix cannot run away
# across unrelated lines.
_DEF = ANY_DEF


def function_body(text, name):
    """The brace-delimited body of C function `name`, or None if not found.

    `text` must already have its comments removed or blanked -- `check()`
    passes `compiler_text()`, where literals are blanked too. Literals are
    blanked again before brace counting, so a brace inside a format string
    cannot unbalance the scan when a caller passes text that still has them.
    """
    # `one_definition` REFUSES two definitions rather than taking the first.
    # Taking the first is how an `#if 0`-disabled original plus a live
    # replacement -- calling the manager directly, with no claim and no arm --
    # passed this lint clean (#976 pre-merge audit, round 4). The refusal
    # propagates to `check()`, which turns it into a problem.
    sig = one_definition(text, name)
    if not sig:
        return None
    start = sig.end() - 1
    end = match_brace(_blank(text), start)
    return None if end is None else text[start:end]


# `function_spans(text)` -> [(name, body_start, body_end)] is `cdef`'s now
# (imported above). It attributes a call OFFSET to the function it sits in,
# which is how the arm-site census below tells FORmat's `WithCleanup` call
# from the one inside the wrapper -- and how a DEFINITION is told from a call
# at all: a definition's own name lies before its body starts, so it falls in
# no span and is never counted as a call site.
enclosing_function = enclosing


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


def _balanced_params(captured):
    """`captured` truncated at the `)` that actually closes the parameter list.

    The definition pattern captures greedily between the first `(` and the
    LAST `)` before the body brace, which is what makes a function-pointer
    parameter -- `bool (*onRefused)(scpi_t *)` -- come out whole. The same
    greed swallows a POSTFIX `__attribute__((...))`: for
    `static bool F(int x) __attribute__((unused)) {` the capture came out as
    `int x) __attribute__((unused)`, so `callback_param` was reading a
    parameter list that is not one, and silently indexing into garbage rather
    than failing (#976 audit, round 6 -- reported as "the definition is
    omitted", which is not what happens; it is found, with the wrong params).

    So walk the capture and stop where depth would go negative. That point IS
    the real closing paren, whatever follows it.
    """
    depth = 0
    for i, ch in enumerate(captured):
        if ch == "(":
            depth += 1
        elif ch == ")":
            if depth == 0:
                return captured[:i]
            depth -= 1
    return captured


def signature_params(text, name):
    """[parameter declaration, ...] of C function `name`, or None."""
    sig = one_definition(text, name, capture_params=True)
    if not sig:
        return None
    # GROUP 3 is the parameter list; groups 1 and 2 are cdef's own
    # paren-seen markers for a (possibly two-level) parenthesized
    # declarator (`cdef.def_pattern`, #976 round 6).
    inner = _balanced_params(sig.group(3)).strip()
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
# Verdict-consumption primitives
#
# What is left after #976: how an arm's return is CONSUMED at its own call
# site -- captured into a variable, tested inline as an `if` condition,
# discarded as a bare statement, or cast to `(void)` -- not which branch a
# write sits in afterward. `_if_statements` is needed only to tell an inline
# test (the arm call itself standing as the condition) from a call that
# merely appears near one; nothing here walks outward through enclosing
# blocks or reasons about which side of a branch is "success".
# --------------------------------------------------------------------------


def _statement_prefix(blanked, pos):
    """The blanked text from the start of `pos`'s statement up to `pos`."""
    start = max(blanked.rfind(ch, 0, pos) for ch in (";", "{", "}"))
    return blanked[start + 1:pos]


# `<type> v = f(...)` and `v = f(...)`. `==`, `!=`, `<=`, `>=`, `+=` cannot
# match: each needs a non-identifier character exactly where the `=` has to be.
_ASSIGN_TAIL = re.compile(r"([A-Za-z_]\w*)\s*=\s*$")
_VOID_TAIL = re.compile(r"\(\s*void\s*\)\s*$")


def _if_statements(body):
    """[(cond_start, cond_end)] for every `if (...)` condition in body.

    Only the condition's extent is needed: the sole surviving consumer,
    `_arm_verdict`'s "inline" case, asks whether the arm call's own position
    falls inside one of these spans to tell an inline test
    (`if (!arm(cfg)) { ... }`) from a call that merely appears nearby. Used to
    also return the gated block's own extent for branch-gated reasoning about
    which side of an `if` a write falls on; that reasoning left with #976 (see
    the module docstring), so the block extent is gone too rather than kept
    unread.
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
        out.append((i + 1, close))
    return out


def _call_end(blanked, start):
    """Index of the `)` closing the call whose name begins at `start`, or None.

    Same balanced walk `call_arguments` does; lifted out because two callers
    now need the call's EXTENT, not its arguments.
    """
    i = blanked.find("(", start)
    if i < 0:
        return None
    depth = 0
    for j in range(i, len(blanked)):
        if blanked[j] == "(":
            depth += 1
        elif blanked[j] == ")":
            depth -= 1
            if depth == 0:
                return j
    return None


def _statement_suffix(blanked, pos):
    """The blanked text from `pos` to the end of its statement."""
    ends = [k for k in (blanked.find(ch, pos) for ch in (";", "{", "}"))
            if k != -1]
    return blanked[pos:min(ends)] if ends else blanked[pos:]


def _arm_verdict(body, arm, arm_call, ifs):
    """-> (kind, var, pattern) for how the arm's return is consumed.

    kind: "captured" | "captured_transformed" | "inline" | "bare" | "void" |
    "other". `pattern` is a regex matching the expression that carries the
    verdict (the variable, or the arm call itself), and is None when there is
    nothing to match -- which is a refusal, not a pass: a consumption form
    this checker cannot recognise is not a form it can report on.
    """
    blanked = _blank(body)
    prefix = _statement_prefix(blanked, arm)
    m = _ASSIGN_TAIL.search(prefix)
    if m:
        # THE WHOLE INITIALISER HAS TO BE THE CALL. Only the prefix was read,
        # so `bool sdArmed = arm(cfg) == false;` was taken as "the verdict is
        # in sdArmed" while the variable held its NEGATION -- every successful
        # arm then entered the refusal branch and every refusal skipped it,
        # with the checker green (audit finding 1). Refused rather than
        # normalised: `== false` is readable, but the next transformation
        # would not be, and guessing is how the first hole got in.
        end = _call_end(blanked, arm)
        tail = "" if end is None else _statement_suffix(blanked, end + 1)
        if tail.strip():
            return "captured_transformed", m.group(1), None
        return "captured", m.group(1), re.escape(m.group(1))
    if _VOID_TAIL.search(prefix):
        return "void", None, None
    if not prefix.strip():
        return "bare", None, None
    if any(cs <= arm < ce for cs, ce in ifs):
        # MATCH THIS CALL AND NOTHING APPENDED TO IT. The pattern was
        # `<name>\s*\(.*\)`, whose `.*` runs from the first `(` to the LAST
        # `)` in the condition -- so `arm(cfg) && (onRefused == NULL)`
        # fullmatched as if it were the bare verdict, and the compound guard
        # this file documents as refused sailed through, retracting on a
        # SUCCESSFUL arm (audit finding 0). Built from the call's own balanced
        # text instead, with whitespace made flexible.
        end = _call_end(blanked, arm)
        if end is None:
            return "other", None, None
        call_src = body[arm:end + 1]
        return "inline", None, r"\s*".join(
            re.escape(t) for t in call_src.split())
    return "other", None, None


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
    mentions = len(identifier_re(retraction).findall(body))
    if _calls(body, retraction):
        problems.append(
            "%s() calls %s() at its own call site as well as passing it to "
            "%s(). That is the pre-#964 shape: at the call site the helper has "
            "already released, so the store is unowned and can retract the "
            "NEXT owner's format state. Pass it in and let the refusal path "
            "run it under the claim." % (FORMAT_FN, retraction, ARM_HELPER))
    elif mentions != 1:
        # ...and every OTHER spelling of the same thing. `_calls` reads only
        # `name(`; a parenthesized call `(name)()`, a pointer taken to it or
        # an alias defined inside FORmat retracts at the call site just as
        # well. The argument is the ONE mention expected in FORmat's body
        # (comments and literals are already blanked in `body`), so any
        # other is refused rather than read (#976 review). An alias defined
        # OUTSIDE FORmat is `_retraction_problems`' job.
        problems.append(
            "%s() names %s %d times in its body; expected exactly once, as "
            "the argument it passes to %s(). Any other mention -- a "
            "parenthesized call, a pointer taken to it, an alias -- can "
            "retract at the call site after the helper has released, which "
            "is the pre-#964 shape; refusing rather than reading which it is."
            % (FORMAT_FN, retraction, mentions, ARM_HELPER))

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
    else:
        # ...and the FIRST leg of that same sentence, which this checker
        # asserted in its MESSAGE and nowhere in its code until the #976
        # pre-merge audit said so. `publish < arm` and `claim < arm` were both
        # checked; `claim < publish` was not, so hoisting the publish above
        # SD_ClaimOrRefuse passed cleanly -- a format advertised by a caller
        # that does not own the manager yet, which is the whole of what #829
        # forbids (SCPIStorageSD.c documents it at the call site). A plain
        # linear ordering over the whole function, so it is the same category
        # as property 1 rather than the branch-gated reasoning this PR removed.
        takers = _call_positions(body, CLAIM_TAKER)
        if len(takers) == 1 and pubs[0] < takers[0]:
            problems.append(
                "%s() publishes format-pending BEFORE it calls %s(). #829 "
                "requires the claim, then the publish, then the arm: a publish "
                "made before the claim advertises a format on behalf of an "
                "owner this function is not yet, and the other SCPI transport "
                "can be that owner in the gap." % (FORMAT_FN, CLAIM_TAKER))
    return problems, retraction


def _census_problems(text, spans):
    """Property 3, plus property 1 (the claim precedes the arm) for the
    six `SCPIStorageSD.c` sites -- `_stream_arm_problems` asserts property 1
    again for the seventh, in `SCPIInterface.c`, via a different claim taker.

    -> (problems, [(function, arm offset, helper_form)]).
    """
    problems = []
    sites = []
    wrapper_helper_calls = []
    recursive = []
    for name, positions in ((ARM_WRAPPER, _call_positions(text, ARM_WRAPPER)),
                            (ARM_HELPER, _call_positions(text, ARM_HELPER))):
        for pos in positions:
            fn = enclosing_function(spans, pos)
            if fn is None:
                continue                 # the definition's own name
            if fn == name:
                # RECURSION, and excusing it silently is how the "arms exactly
                # once" property below was defeated: a wrapper that calls
                # ITSELF reaches the helper twice at run time while showing
                # only one helper call to a textual scan (#976 pre-merge
                # audit, round 2). Neither of these functions is recursive
                # today, and neither has any reason to be, so a self-call is
                # reported rather than waved through.
                recursive.append((fn, pos))
                continue
            if name == ARM_HELPER and fn == ARM_WRAPPER:
                # The wrapper's own NULL-passing call is expected, and exactly
                # ONE of it is. Excluding EVERY such call -- which is what this
                # did -- meant a second one was invisible to the census below,
                # so an ordinary retry-on-refusal inside the wrapper armed
                # twice and the "arms exactly once" guard never saw it. The
                # second call runs with the mode already cleared and no claim
                # held, which is the #589 suspension refusal bypassed (#976
                # pre-merge audit).
                wrapper_helper_calls.append(pos)
                continue
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

    if recursive:
        problems.append(
            "%s calls itself. Neither arm function is recursive, and a "
            "self-call is how the arm count below is defeated: the helper is "
            "reached twice at run time while a textual scan sees one call "
            "site. If recursion is genuinely wanted here, this checker has to "
            "be taught to count reachable arms rather than written ones."
            % ", ".join(sorted({"%s()" % f for f, _ in recursive})))

    if len(wrapper_helper_calls) > 1:
        problems.append(
            "%s() calls %s() %d times; expected exactly once. A second arm "
            "inside the wrapper runs with the mode already cleared and no "
            "claim held, so it does not meet the suspension refusal the first "
            "one does -- and the arm census cannot see it, because the "
            "wrapper's own call is the one site it is meant to excuse (#971)."
            % (ARM_WRAPPER, ARM_HELPER, len(wrapper_helper_calls)))

    # The wrapper must pass NULL, or the five inherit something else.
    wrapper = function_body(text, ARM_WRAPPER)
    found, why = callback_param(text, ARM_HELPER)
    if wrapper is None:
        problems.append(
            "%s() not found, so what the five plain arm sites pass in the "
            "retraction slot is UNVERIFIED." % ARM_WRAPPER)
    elif found is not None:
        _cb, index = found
        params = signature_params(text, ARM_HELPER)
        # EVERY call, not just the first. Reading `[0]` alone left a second
        # call's retraction argument unexamined, which is the same blind spot
        # the count check above closes, one level down.
        all_args = call_arguments(wrapper, ARM_HELPER) or []
        args = all_args[0] if all_args else None
        for n_call, one in enumerate(all_args[1:], start=2):
            if one is None or len(one) != len(params):
                problems.append(
                    "%s()'s call #%d to %s() does not pass its full argument "
                    "list, so its retraction slot could not be read."
                    % (ARM_WRAPPER, n_call, ARM_HELPER))
            elif one[index] != "NULL":
                problems.append(
                    "%s()'s call #%d to %s() passes `%s` rather than NULL in "
                    "the retraction slot."
                    % (ARM_WRAPPER, n_call, ARM_HELPER, one[index]))
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

    # Property 1: the claim precedes the arm, in the arm's own function. This
    # is the checkable half of #971's "TryClaim-side entry".
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
        if len(arms) != 1:
            # ONE arm per function, counted -- not a claim about which branch
            # either arm is in. Both helpers RELEASE the claim on both of
            # their paths (#955), so a second arm in the same function runs
            # unowned however the first one turned out: a retry, or two
            # sequential arms, and the checker would have passed it because
            # every arm was merely AFTER the single claim. Qodo raised this on
            # PR #976 against the narrowed file; `_stream_arm_problems` has
            # enforced the same count at the seventh site all along, so this
            # is its twin arriving late rather than a new kind of check.
            problems.append(
                "%s() arms %d times; expected exactly one. %s() and %s() both "
                "release the claim on BOTH of their paths, so a second arm in "
                "one function runs with no claim held -- whatever the first "
                "one returned. Take the claim again, or split the function."
                % (fn, len(arms), ARM_WRAPPER, ARM_HELPER))
        elif len(takers) != 1:
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
    """Properties 1 and 4, in `SCPIInterface.c`: the claim precedes the arm,
    and the streaming-log arm's verdict is consumed. -> (problems, sites
    examined).

    This used to also place the refusal branch against the readiness poll and
    clear `mode` under the claim (the old property 5) -- the same branch-gated
    positional reasoning that #976 spent three review rounds failing to defend
    for the helper in `SCPIStorageSD.c`, at a SECOND site. That half is gone;
    see "What moved to a host test" in this module's docstring for why, and
    for the fact that (unlike the helper) no replacement host-test model
    exists yet for this site. Property 1's claim-before-arm half stays: it is
    a plain "A before B" over the whole function, not one of the shapes three
    rounds catalogued, so it does not share their fate."""
    problems = []
    body = function_body(text, STREAM_FN)
    if body is None:
        problems.append(
            "%s() not found, so the streaming-log arm this checker exists to "
            "examine was NOT read at all. Refusing rather than reporting a "
            "pass on a function it could not read (#942)." % STREAM_FN)
        return problems, 0

    arms = _call_positions(body, STREAM_ARM_CALL)
    if len(arms) != 1:
        problems.append(
            "%s() calls %s() %d times; this checker needs exactly one arm to "
            "know whose verdict it is reading. None means the arm moved or "
            "was renamed and nothing here is being checked at all; several "
            "means it cannot say which call's verdict a consumer downstream "
            "belongs to (#942)."
            % (STREAM_FN, STREAM_ARM_CALL, len(arms)))
        return problems, 0
    arm = arms[0]

    # ---- property 1: the claim precedes the arm ------------------------------
    takers = _call_positions(body, STREAM_CLAIM_TAKER)
    if len(takers) != 1:
        problems.append(
            "%s() calls %s() %d times before arming; expected exactly one. "
            "The arm is where ownership hands over from the claim to `%s`, so "
            "an arm with no claim taken -- or with a claim this checker "
            "cannot place -- has nothing to hand over (#829/#836)."
            % (STREAM_FN, STREAM_CLAIM_TAKER, len(takers), MODE_FIELD))
    elif takers[0] > arm:
        problems.append(
            "%s() arms before it calls %s(): every write to the shared "
            "operands, `%s` included, must follow the claim (#829/#836)."
            % (STREAM_FN, STREAM_CLAIM_TAKER, MODE_FIELD))

    # ---- property 4: the verdict is consumed --------------------------------
    ifs = _if_statements(body)
    kind, _var, pattern = _arm_verdict(body, arm, STREAM_ARM_CALL, ifs)
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
    if kind == "captured_transformed":
        problems.append(
            "%s() assigns %s()'s return through a TRANSFORMATION -- the "
            "initialiser does not end at the call. Whatever the variable then "
            "holds, it is not the verdict, so every branch placed on it may "
            "mean the opposite of what it reads (audit finding 1: `= arm(...) "
            "== false` sent every SUCCESSFUL arm into the refusal branch). "
            "Assign the call's result plainly and do the comparison in the "
            "`if` (#942)." % (STREAM_FN, STREAM_ARM_CALL))
        return problems, 1
    if pattern is None:
        problems.append(
            "%s() consumes %s()'s return in a form this checker cannot read: "
            "neither assigned to a variable nor standing as an `if` condition. "
            "Refusing rather than reporting a pass on a shape it did not "
            "understand (#942)." % (STREAM_FN, STREAM_ARM_CALL))
        return problems, 1

    # Consumed (captured or tested inline) and nothing more is asked: this
    # file no longer checks whether a captured verdict is ever branched on,
    # whether the refusal beats the readiness poll, or whether `mode` is
    # cleared under the claim -- see the module docstring.
    return problems, 1


# Every name each entry point's properties are computed from. Each one's
# occurrences are ACCOUNTED FOR (see `_accounting_problems`): a spelling of
# one of these that no recogniser explains fails the gate instead of silently
# dropping out of a count.
SD_ACCOUNTED = (ARM_HELPER, ARM_WRAPPER, CLAIM_TAKER, FORMAT_FN,
                FORMAT_PUBLISH)
STREAM_ACCOUNTED = (STREAM_FN, STREAM_ARM_CALL, STREAM_CLAIM_TAKER)


def compiler_text(source_text):
    """`source_text` as the compiler reads it, comments and literals
    blanked, LENGTH preserved: `cdef.compiler_view(source_text).matchmask`.

    Backslash-newline splices are joined FIRST (translation phase 2), then
    every comment and literal is blanked, so a splice-made `//`, a comment
    closed by a spliced `*/` and an identifier split across a line are what
    the compiler says they are. Every census scan in this file runs on this
    text and nothing else, and occurrence accounting reads the same
    characters. Offsets into it are offsets into the JOINED text;
    `compiler_view(source_text).omap` maps them back to the file.

    This used to be `scpi_wiki_sync.strip_c_comments(source_text)` -- RAW
    text, comments collapsed -- so the census and accounting read two
    different files, and a `/* ... *` + backslash + newline + `/` comment hid
    a FORmat call-site retraction from the census entirely (#976 review).
    """
    return compiler_view(source_text).matchmask


def _accounting_problems(source_text, text, spans, names):
    """Occurrence accounting for `names`, and the census's agreement with
    it, POSITION BY POSITION.

    1. `cdef.account_occurrences`: every occurrence of each name must be a
       definition, a prototype or an evaluated call; anything else (an
       alias, `&F`, a declaration, `sizeof F(x)`, `p->F(x)`, a construct
       nobody has thought of yet) is reported at its PHYSICAL line.
    2. Every call the census counts inside a function body (`\\bF\\s*\\(` on
       `text`, which is `compiler_text(source_text)`) must be a call
       accounting classified AT THE SAME OFFSET, and every call accounting
       classified must be one the census counts. Without this, "classified
       as a call" would be an allowlist the census never saw: `(F)(x)` is a
       call to accounting and invisible to `\\bF\\s*\\(`, and `my$F(x)` is
       the reverse -- another function to GCC, a boundary to `\\b`.

    Positions, not per-function counts. Counts were the first version, and
    one of each misreading in the same function -- a phantom claim the
    census reads before the arm, and the real claim after it in a spelling
    only accounting reads -- kept every count equal while the claim moved
    past the arm. Properties 1 and 2 are ORDERINGS computed from the
    census's positions, so both passed (#976 review).
    """
    view = compiler_view(source_text)
    problems = []
    for name in names:
        problems.extend(account_occurrences(source_text, name))
        found = {o.offset: o.function
                 for o in classify_occurrences(source_text, name)
                 if o.kind == "call"}
        counted = {}
        for p in _call_positions(text, name):
            fn = enclosing_function(spans, p)
            if fn is not None:
                counted[view.omap[p]] = fn
        parts = ["the census counts a call in %s() at line %d that "
                 "accounting does not classify as one"
                 % (counted[o], view.line_of_orig(o))
                 for o in sorted(set(counted) - set(found))]
        parts += ["accounting finds a call in %s() at line %d that the "
                  "census does not count" % (found[o], view.line_of_orig(o))
                  for o in sorted(set(found) - set(counted))]
        if parts:
            problems.append(
                "%s: occurrence accounting and the census disagree about "
                "WHICH text is a call -- %s. Every count and every ordering in "
                "properties 1-4 is computed from the census's positions, so a "
                "call either one misreads, or two misreadings that cancel in "
                "a count, is refused rather than trusted (#976)."
                % (name, "; ".join(parts)))
    return problems


def _retraction_problems(source_text, retraction):
    """Every preprocessor directive in the file that names FORmat's
    retraction.

    An alias defined OUTSIDE FORmat -- `#define UNDO <retraction>` at file
    scope, then `UNDO();` at FORmat's call site -- retracts at the call site
    through a name FORmat's own body never spells, so `_format_problems`'s
    exactly-once count cannot see it. The directive itself can be seen.
    """
    return ["%s: line %d: FORmat's retraction is named in a preprocessor "
            "directive -- an alias or a macro body that can reach it from "
            "FORmat's own call site without spelling it there, which is the "
            "pre-#964 shape. Refusing rather than expanding it (#976)."
            % (retraction, line)
            for line in directive_lines(source_text, retraction)]


def check(source_text):
    """-> (problems, examined). Pure, so --self-test can drive it.

    Reads `compiler_text(source_text)` -- the file the way the compiler
    reads it -- and nothing else. No longer reads
    `SD_ArmOrRefuseWithCleanup()`'s own body at all: the ordering that used
    to require it (property 1's helper-internal half) is gone from this file
    -- see "What moved to a host test" in the module docstring.
    `_format_problems` and `_census_problems` each independently refuse
    rather than pass if the helper cannot be found or called, so no separate
    vacuity gate is needed here for that case."""
    text = compiler_text(source_text)
    problems = []
    try:
        spans = function_spans(text)
        fmt_problems, retraction = _format_problems(text)
        problems.extend(fmt_problems)
        census, sites = _census_problems(text, spans)
        problems.extend(census)
        problems.extend(_accounting_problems(source_text, text, spans,
                                             SD_ACCOUNTED))
        if retraction:
            problems.extend(_retraction_problems(source_text, retraction))
    except AmbiguousDefinition as exc:
        # Reported, never resolved. Which definition the compiler builds is
        # preprocessor state this checker does not evaluate, so a pass here
        # would be a pass on code that may never be built.
        return ["%s -- refusing to check either. If this is a board-variant "
                "or #if 0 pair, the checker has to be taught which is live."
                % exc], 0
    return problems, len(sites)


def check_stream(source_text):
    """-> (problems, examined) for `SCPIInterface.c`. Pure, like check(),
    and like it reads `compiler_text(source_text)` and nothing else."""
    text = compiler_text(source_text)
    try:
        problems, examined = _stream_arm_problems(text)
        problems = problems + _accounting_problems(
            source_text, text, function_spans(text), STREAM_ACCOUNTED)
        return problems, examined
    except AmbiguousDefinition as exc:
        return ["%s -- refusing to check either." % exc], 0


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
# lines and the arm is a ~130-line slice of it; everything around it -- the
# frequency parse, the interface publication, the #589 pre-check, the
# five-second poll's own error reporting -- is left out. `_STREAM_REFUSAL` is
# its own constant so mutations can delete or reshape it (see `discarded`,
# `voided`, `untested` below); `_STREAM_POLL_LOOP` is kept verbatim only so
# the fixture still resembles the real function -- no surviving test moves it,
# since the refusal-vs-poll ordering it used to anchor left with property 5.
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

def _unevaluated_claims(call):
    """(label, statement) pairs, each putting `call` inside an operand GCC
    never evaluates (measured: 0 calls each) and then opening the `if (0) {`
    whose body the claim's own `{` used to open. These are the spellings the
    #976 round-8 review found the first round-8 reader still misread: a
    bracket or `;` on a directive line inside the operand, GCC's prefix
    keywords ahead of a unary `-`/`+`, and digraph brackets."""
    return (
        ("an operand whose `)` stands on a directive line",
         "(void)sizeof (0 +\n#define SD_RP_X )\n%s); if (0) {" % call),
        ("an operand with a directive's `;` ahead of it",
         "(void)sizeof\n#define SD_SEMI_X ;\n(0 + %s); if (0) {" % call),
        ("a subscript operand whose `]` stands on a directive line",
         "(void)sizeof context[0 +\n#define SD_RB_X ]\n%s]; if (0) {" % call),
        ("`sizeof __extension__ -`",
         "(void)sizeof __extension__ -%s; if (0) {" % call),
        ("`sizeof __real__ +`", "(void)sizeof __real__ +%s; if (0) {" % call),
        ("a digraph subscript", "(void)sizeof context<:%s:>; if (0) {" % call),
        ("a digraph compound literal",
         "(void)sizeof (bool<:1:>)<%%%s%%>; if (0) {" % call))


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

        # #976 pre-merge audit: two ORDINARY C shapes that the checker used to
        # pass silently, which is the bar this file sets for itself -- a
        # property it CLAIMS to assert, satisfiable by everyday code that
        # violates it.
        #
        # 1. A retry-on-refusal inside the wrapper armed TWICE. Every helper
        #    call inside the wrapper was excused, not just the expected one,
        #    so the "arms exactly once" census never saw the second.
        retry = _GOOD.replace(
            "    return SD_ArmOrRefuseWithCleanup(context, cmd, cfg, NULL);",
            "    if (!SD_ArmOrRefuseWithCleanup(context, cmd, cfg, NULL)) {\n"
            "        return SD_ArmOrRefuseWithCleanup(context, cmd, cfg, NULL);\n"
            "    }\n"
            "    return true;", 1)
        _ck("a second arm inside the wrapper is refused",
            any("expected exactly once" in x for x in check(retry)[0]), True)

        # 2. A definition with its return type on its own line was invisible
        #    to the definition scanner, so its body was no span, every call
        #    inside it was attributed to nothing, and the site dropped out of
        #    the census while the floor count still passed.
        gnu = _GOOD.replace("static bool SD_ArmOrRefuse(",
                            "static bool\nSD_ArmOrRefuse(", 1)
        _ck("a split-line definition is still a definition",
            check(gnu)[1], n)
        _ck("and it is still read as compliant", check(gnu)[0], [])

        # Round 2 of the audit, three more shapes -- each one defeating a
        # guard the round-1 fixes had just added, which is why the matchers
        # are now ONE rule rather than three that agree by discipline.
        #
        # 3. `__attribute__((...))` between the return type and the name was
        #    captured AS the name. Confirmed live: SCPIStorageSD.c already
        #    carries `bool __attribute__((weak)) DRV_SDSPI_GetCID(...)`.
        attr = _GOOD.replace("static bool SD_ArmOrRefuse(",
                             "static bool __attribute__((weak)) "
                             "SD_ArmOrRefuse(", 1)
        _ck("an __attribute__ does not become the function name",
            [nm for nm, _, _ in function_spans(compiler_text(attr))
             if nm.startswith("__")], [])
        _ck("and the annotated file still reads as compliant",
            check(attr)[0], [])

        # 4. A wrapper that calls ITSELF reaches the helper twice at run time
        #    while showing one call site to a textual scan.
        recur = _GOOD.replace(
            "    return SD_ArmOrRefuseWithCleanup(context, cmd, cfg, NULL);",
            "    if (!SD_ArmOrRefuseWithCleanup(context, cmd, cfg, NULL)) {\n"
            "        return SD_ArmOrRefuse(context, cmd, cfg);\n"
            "    }\n"
            "    return true;", 1)
        _ck("a self-call is reported, not excused as recursion",
            any("calls itself" in x for x in check(recur)[0]), True)

        # 5. The HELPER split the same way. The round-2 rows covered only the
        #    WRAPPER, and `signature_params` -- a FOURTH matcher in this file
        #    -- still required the return type and the name to share a line,
        #    so `callback_param` reported the helper missing and the lint
        #    FAILED on formatting that changes no behaviour. A false alarm
        #    rather than a silent pass, and still a broken gate.
        helper_split = _GOOD.replace(
            "static bool SD_ArmOrRefuseWithCleanup(",
            "static bool\nSD_ArmOrRefuseWithCleanup(", 1)
        # 6. Round 4: `function_body`/`signature_params` took the FIRST match
        #    with no ambiguity check, so an `#if 0`-disabled original plus a
        #    live replacement that called the manager DIRECTLY -- no claim, no
        #    arm, no retraction -- passed this lint clean. `cdef` refuses, and
        #    `check()` turns the refusal into a problem rather than a crash.
        dead_pair = _GOOD.replace(
            "static bool SD_ArmOrRefuse(",
            "#if 0\nstatic bool SD_ArmOrRefuse(", 1)
        dead_pair += ("#endif\nstatic bool SD_ArmOrRefuse(scpi_t * context, "
                      "const char *cmd, void *cfg)\n{\n    return true;\n}\n")
        _ck("two definitions are refused, not silently resolved",
            any("refusing to choose" in x for x in check(dead_pair)[0]), True)

        # Round 6. A POSTFIX `__attribute__((...))` used to be swallowed by
        # the greedy parameter capture, so `callback_param` indexed into a
        # "parameter list" that was not one. The definition was never lost --
        # the finding described it as omitted; it was found, with garbage
        # params, which is worse because nothing failed.
        _ck("a postfix __attribute__ does not become a parameter",
            signature_params(
                "static bool F(int x) __attribute__((unused))\n{\n}\n", "F"),
            ["int x"])
        _ck("...and a function-POINTER parameter still comes out whole",
            signature_params(
                "static bool G(scpi_t *c, bool (*cb)(scpi_t *), int n)\n{\n}\n",
                "G"),
            ["scpi_t *c", "bool (*cb)(scpi_t *)", "int n"])

        # Round 6 also reported that a column-zero `if (F(x)) {` is read as a
        # definition of F. It is NOT, and these rows pin why rather than
        # leaving it true by luck: a control statement wraps its call in its
        # OWN parentheses, so the `)` the pattern needs before the brace is
        # the control's, not the call's, and the match fails. Asserted for
        # five shapes because "it happens not to match" is not a property.
        for _shape in ("if (F(x)) {\n}\n",
                       "if (F(x))\n{\n}\n",
                       "while (F(x)) {\n}\n",
                       "for (i = 0; F(i); i++) {\n}\n",
                       "switch (F(x)) {\ndefault: break;\n}\n"):
            # GROUP 3 is the name; groups 1 and 2 are cdef's own paren-seen
            # markers for a (possibly two-level) parenthesized declarator
            # (`cdef.ANY_DEF`, #976 round 6).
            _ck("control flow calling F() is not a definition of F: %r"
                % _shape.split("\n")[0],
                [m.group(3) for m in _DEF.finditer(_shape)], [])

        _ck("the helper's parameters are found when its type is on its own line",
            callback_param(compiler_text(helper_split), ARM_HELPER)[0],
            ("onRefused", 3))
        _ck("and a split helper definition is still read as compliant",
            check(helper_split)[0], [])

        # The parsing this rests on, asserted directly rather than only
        # through a verdict: a definition is not a call site, and a call in a
        # condition is not a definition.
        _ck("the callback parameter is discovered from the signature",
            callback_param(compiler_text(_GOOD), ARM_HELPER)[0],
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
            "SCPI_ErrorPush" in function_body(compiler_text(_GOOD),
                                              ARM_HELPER), True)

        # #971 acceptance 1 used to live here: the retraction moved past the
        # release. That is now `test_971_sd_arm_refusal_order.c`'s job (the
        # ordering INSIDE SD_ArmOrRefuseWithCleanup() is no longer this
        # file's business at all) -- see "What moved to a host test" in the
        # module docstring.

        # ---- the pre-#964 FORmat shape --------------------------------------
        # Retract at the call site, after the helper has released, rather
        # than through the callback parameter.
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

        # The #955 `mode`-clear ordering, the claim-held-across-the-arm
        # reasoning, the "region is not a path" fixes (Qodo, PR #976), and
        # the callback's own on-refusal-path placement ALL used to be tested
        # here. All of that was branch-gated positional reasoning about
        # `SD_ArmOrRefuseWithCleanup()`'s own body -- the shape three review
        # rounds on #976 kept finding new ways to defeat. It is gone from
        # this file; `tests/host/test_971_sd_arm_refusal_order.c` (a
        # deterministic model plus a sha256 drift pin on that function's
        # text) covers it now. See "What moved to a host test" in the module
        # docstring.

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

        # Property 1: the arm without the claim ahead of it.
        noclaim = _GOOD.replace(
            "    if (!SD_ClaimOrRefuse(context, \"SPACe\")) {\n"
            "        return SCPI_RES_ERR;\n    }\n", "")
        assert noclaim != _GOOD
        probs, _ = check(noclaim)
        _ck("an arm with no claim taken in its own function is caught",
            any("calls SD_ClaimOrRefuse() 0 times" in p for p in probs), True)

        # The SAME defect, but the claim call is still THERE in the text --
        # deleted only by a `//` comment ending in a backslash, which C's
        # phase 2 splices onto the next physical line before phase 3 even
        # recognises the comment. `check()` ran `scpi_wiki_sync`'s
        # `strip_c_comments` then, and until #976 round 6 that stripper
        # stopped a `//` comment at the first bare newline, so the
        # spliced-away claim call was still handed to the census as LIVE
        # code. (The census reads `compiler_text()` now, where every splice
        # is joined before any comment is recognised.) A mutation of
        # this shape passed clean before the fix; a review pass on this same
        # fix reproduced the defect CLASS against the real
        # `SCPIStorageSD.c` too, with a different anchor (the CRC claim, not
        # SPACe's). Positive control is `noclaim` immediately above: same
        # missing claim, different means.
        # Putting the `{`/`}` on the SAME line as the `if` keeps this
        # compilable AND keeps the splice brace-neutral -- both braces are
        # swallowed into the comment together, so nothing is unbalanced;
        # splicing the comment above a multi-line `if (...) {` instead
        # swallows only the OPENING brace, corrupts the count, and reports a
        # different (misleading) problem instead of the one this row pins.
        splicedclaim = _GOOD.replace(
            "    if (!SD_ClaimOrRefuse(context, \"SPACe\")) {\n"
            "        return SCPI_RES_ERR;\n    }\n",
            "    // the compiler deletes the next physical line \\\n"
            "    if (!SD_ClaimOrRefuse(context, \"SPACe\")) { return SCPI_RES_ERR; }\n",
            1)
        assert splicedclaim != _GOOD
        probs, _ = check(splicedclaim)
        _ck("an arm whose claim call is spliced into a comment is still "
            "caught, not silently read as present",
            any("calls SD_ClaimOrRefuse() 0 times" in p for p in probs), True)

        # A SECOND arm in the same function. Both helpers release the claim
        # on both paths, so the retry runs unowned -- and before this row the
        # checker passed it, because every arm was merely AFTER the one claim.
        twice = _GOOD.replace(
            "    if (!SD_ArmOrRefuse(context, \"SPACe\", pCfg)) {\n"
            "        return SCPI_RES_ERR;\n    }",
            "    if (!SD_ArmOrRefuse(context, \"SPACe\", pCfg)) {\n"
            "        return SCPI_RES_ERR;\n    }\n"
            "    if (!SD_ArmOrRefuse(context, \"SPACe\", pCfg)) {\n"
            "        return SCPI_RES_ERR;\n    }")
        assert twice != _GOOD
        probs, _ = check(twice)
        _ck("a second arm in one function is caught, not grouped under the "
            "one claim",
            any("arms 2 times" in p for p in probs), True)

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

        # The FIRST leg of the same ordering, which this file asserted only
        # in prose until #976's audit hoisted the publish above the claim and
        # watched it pass.
        earlypub = _GOOD.replace(
            "    if (!SD_ClaimOrRefuse(context, \"FORmat\")) {\n"
            "        return SCPI_RES_ERR;\n    }\n"
            "    sd_card_manager_SetFormatPending();",
            "    sd_card_manager_SetFormatPending();\n"
            "    if (!SD_ClaimOrRefuse(context, \"FORmat\")) {\n"
            "        return SCPI_RES_ERR;\n    }")
        assert earlypub != _GOOD
        probs, _ = check(earlypub)
        _ck("publishing BEFORE the claim is caught",
            any("BEFORE it calls SD_ClaimOrRefuse" in p for p in probs), True)

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

        # The "comments are not code" pair that used to live here (a
        # commented-out or string-mentioned onRefused() call) tested the
        # callback's on-refusal-path PLACEMENT, which moved to the host test
        # along with the rest of the helper-internal reasoning above.

        # ---- vacuity: a file this cannot read must FAIL ---------------------
        # check() no longer has a helper-not-found gate of its own (that was
        # `_helper_problems`'s job); this input still fails, via
        # `_format_problems` (FORmat not found) and `_census_problems` (no
        # arm sites at all) independently, each of which is a separate
        # refusal-not-a-pass gate in its own right.
        probs, n2 = check("int main(void) { return 0; }")
        _ck("a source this checker cannot read at all fails, not passes",
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
            any("could not be located" in p for p in probs), True)

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

        # ---- occurrence accounting (#976, after round 6) --------------------
        # Every row below was CLEAN before accounting existed -- each is a
        # valid C spelling that took live code out of what this file reads
        # while it reported no problem. The fix is not a recogniser per
        # spelling: every occurrence of every name the properties are
        # computed from must be explained, and the census must find exactly
        # the calls accounting found, at the same positions. See
        # `_accounting_problems`.
        unaccounted = "no recogniser accounts for"
        disagree = "disagree about WHICH text is a call"
        helper = _GOOD[_GOOD.index("static bool SD_ArmOrRefuseWithCleanup("):
                       _GOOD.index("static bool SD_ArmOrRefuse(scpi_t")]

        # 1. An `#if 0` original plus a live replacement with TWO attributes.
        #    `cdef._ATTR` absorbs one, so only the DEAD copy is a definition
        #    to every matcher: unambiguous, its signature read, its callers
        #    censused -- and the compiled helper never looked at.
        two_attr = _GOOD.replace(helper, "#if 0\n" + helper + "#endif\n" + helper.replace(
            "static bool SD_ArmOrRefuseWithCleanup(",
            "static bool __attribute__((noinline)) __attribute__((unused)) "
            "SD_ArmOrRefuseWithCleanup(", 1), 1)
        assert two_attr != _GOOD
        _ck("an #if 0 helper plus a live DOUBLE-__attribute__ replacement is "
            "refused, not read as the dead one",
            any(unaccounted in p for p in check(two_attr)[0]), True)

        # 2. The same with THREE wrapping parens -- one past what the
        #    definition matcher supports, and so call-shaped to nothing.
        three = _GOOD.replace(helper, "#if 0\n" + helper + "#endif\n" + helper.replace(
            "static bool SD_ArmOrRefuseWithCleanup(",
            "static bool (((SD_ArmOrRefuseWithCleanup)))(", 1), 1)
        assert three != _GOOD
        _ck("an #if 0 helper plus a live (((F))) replacement is refused",
            any(unaccounted in p for p in check(three)[0]), True)

        # 3. A SECOND arm, spelled with a parenthesized callee. `(F)(x)` is
        #    a call; the census's `\bF\s*\(` cannot see it, so the "arms
        #    exactly once" count read one.
        paren = _GOOD.replace(
            "    return SCPI_RES_OK;\n}\n\nscpi_result_t SCPI_StorageSDFormat",
            "    (SD_ArmOrRefuseWithCleanup)(context, \"SPACe\", pCfg, NULL);\n"
            "    return SCPI_RES_OK;\n}\n\nscpi_result_t SCPI_StorageSDFormat", 1)
        assert paren != _GOOD
        _ck("a parenthesized-callee second arm is not silently uncounted",
            any(disagree in p and "accounting finds a call in "
                "SCPI_StorageSDSpaceGet() at line" in p
                for p in check(paren)[0]), True)

        # 4. An alias. Replacing the one plain arm is caught by the site
        #    FLOOR already (and by accounting); adding a SECOND arm through
        #    the alias is caught by accounting ALONE -- the census sees one.
        alias = _GOOD.replace(
            "    if (!SD_ArmOrRefuse(context, \"SPACe\", pCfg)) {",
            "#define ARM_ALIAS SD_ArmOrRefuse\n"
            "    if (!ARM_ALIAS(context, \"SPACe\", pCfg)) {", 1)
        assert alias != _GOOD
        probs, _ = check(alias)
        _ck("an arm replaced by an alias drops below the plain-site floor",
            any("found only 0 call(s) to SD_ArmOrRefuse()" in p
                for p in probs), True)
        _ck("...and the alias itself is an unaccounted occurrence",
            any(unaccounted in p and "preprocessor directive" in p
                for p in probs), True)
        alias2 = _GOOD.replace(
            "    return SCPI_RES_OK;\n}\n\nscpi_result_t SCPI_StorageSDFormat",
            "#define ARM_AGAIN SD_ArmOrRefuse\n"
            "    if (!ARM_AGAIN(context, \"SPACe\", pCfg)) {\n"
            "        return SCPI_RES_ERR;\n    }\n"
            "    return SCPI_RES_OK;\n}\n\nscpi_result_t SCPI_StorageSDFormat", 1)
        assert alias2 != _GOOD
        _ck("a SECOND arm through an alias is refused, not counted as none",
            any(unaccounted in p and "preprocessor directive" in p
                for p in check(alias2)[0]), True)

        # 5. A second claim through a function pointer -- bare, and `&`.
        for spell in ("SD_ClaimOrRefuse", "&SD_ClaimOrRefuse"):
            ptr = _GOOD.replace(
                "    pCfg->mode = SD_CARD_MANAGER_MODE_GET_SPACE;\n",
                "    bool (*again)(scpi_t *, const char *) = %s;\n"
                "    (void)again(context, \"SPACe\");\n"
                "    pCfg->mode = SD_CARD_MANAGER_MODE_GET_SPACE;\n" % spell, 1)
            assert ptr != _GOOD
            _ck("a claim reached through `= %s` is an unaccounted occurrence"
                % spell, any(unaccounted in p for p in check(ptr)[0]), True)

        # 6. A name in a comment or a string literal is NOT an occurrence:
        #    no problem, and nothing counted.
        mentioned = _GOOD.replace(
            "    pCfg->mode = SD_CARD_MANAGER_MODE_GET_SPACE;\n",
            "    /* SD_ArmOrRefuse(context, \"x\", pCfg); */\n"
            "    LOG_E(\"SD_ClaimOrRefuse(\");\n"
            "    pCfg->mode = SD_CARD_MANAGER_MODE_GET_SPACE;\n", 1)
        assert mentioned != _GOOD
        _ck("names inside a comment and a string are not occurrences",
            check(mentioned), ([], 2))

        # 7. Diagnostics carry the PHYSICAL line of the ORIGINAL source. The
        #    census's text is splice-JOINED (and a multi-line comment used
        #    to be collapsed to one space), so its offsets are not the
        #    file's; every diagnostic maps back through the view's `omap`.
        lined = _GOOD.replace(
            "scpi_result_t SCPI_StorageSDSpaceGet(scpi_t * context) {\n",
            "/* three\n * physical\n * lines */\n"
            "scpi_result_t SCPI_StorageSDSpaceGet(scpi_t * context) {\n"
            "    void *p = SD_ClaimOrRefuse;\n", 1)
        assert lined != _GOOD
        want = "line %d" % (lined[:lined.index("void *p")].count("\n") + 1)
        _ck("an unaccounted occurrence is reported at its physical %s" % want,
            any(unaccounted in p and want in p for p in check(lined)[0]),
            True)

        # 8. #1066, live against this file until accounting existed: a `//`
        #    CREATED by a splice (`/` + `\` + newline + `/`) deletes the SPACe
        #    claim guard. The census read `strip_c_comments` output then,
        #    which looks for a literal `//`, so it still counted the claim.
        #    It reads `compiler_text()` now, where the claim is simply gone
        #    -- and the claim count says so directly.
        made = _GOOD.replace(
            "    if (!SD_ClaimOrRefuse(context, \"SPACe\")) {\n"
            "        return SCPI_RES_ERR;\n    }\n",
            "    /\\\n/ if (!SD_ClaimOrRefuse(context, \"SPACe\")) "
            "{ return SCPI_RES_ERR; }\n", 1)
        assert made != _GOOD
        _ck("a claim swallowed by a splice-CREATED comment is refused (#1066)",
            any("calls SD_ClaimOrRefuse() 0 times" in p
                for p in check(made)[0]), True)

        # 9. The same deletion through GCC's backslash-SPACE-newline, which
        #    XC32 joins and an ISO-only stripper does not; the compiler's
        #    view joins it too (`cdef._SPLICE`).
        gcc = _GOOD.replace(
            "    if (!SD_ClaimOrRefuse(context, \"SPACe\")) {\n"
            "        return SCPI_RES_ERR;\n    }\n",
            "    // deleted by GCC \\ \n"
            "    if (!SD_ClaimOrRefuse(context, \"SPACe\")) "
            "{ return SCPI_RES_ERR; }\n", 1)
        assert gcc != _GOOD
        _ck("a claim swallowed by a backslash-SPACE-continued comment is "
            "refused", any("calls SD_ClaimOrRefuse() 0 times" in p
                           for p in check(gcc)[0]), True)

        # 10. A second arm whose NAME is split by a backslash-newline: one
        #     token to the compiler -- and, read the compiler's way, one to
        #     the census, which now simply counts the second arm.
        split = _GOOD.replace(
            "    return SCPI_RES_OK;\n}\n\nscpi_result_t SCPI_StorageSDFormat",
            "    if (!SD_ArmOr\\\nRefuse(context, \"SPACe\", pCfg)) {\n"
            "        return SCPI_RES_ERR;\n    }\n"
            "    return SCPI_RES_OK;\n}\n\nscpi_result_t SCPI_StorageSDFormat", 1)
        assert split != _GOOD
        _ck("a second arm with a splice-split name is not silently uncounted",
            any("arms 2 times" in p for p in check(split)[0]), True)

        # 11. The census OVER-counting: `my$SD_ArmOrRefuse(...)` calls a
        #     DIFFERENT function to GCC (`$` is an identifier character), but
        #     `\b` sees a boundary before `SD_` and counts an arm. Accounting
        #     reads no occurrence there, so the disagreement reds -- had it
        #     shared the census's boundary, the two would have agreed on a
        #     phantom arm and a real one replaced this way would pass.
        dollar = _GOOD.replace(
            "    if (!SD_ArmOrRefuse(context, \"SPACe\", pCfg)) {",
            "    if (!my$SD_ArmOrRefuse(context, \"SPACe\", pCfg)) {", 1)
        assert dollar != _GOOD
        _ck("an arm replaced by `my$SD_ArmOrRefuse` (another function to "
            "GCC) is refused, not counted",
            any(disagree in p for p in check(dollar)[0]), True)

        # 12. #976 review: two misreadings that CANCEL in a count. A claim
        #     the census reads and the compiler never makes (`my$F` is
        #     another function to GCC) BEFORE the arm, plus the real claim
        #     AFTER it in a spelling only accounting reads (`(F)(...)`).
        #     Every per-function count stayed equal and the claim moved past
        #     the arm; the count-based agreement check passed it clean. Rows
        #     3 and 11 each red alone -- this is the pair.
        pair = _GOOD.replace(
            "static bool SD_ClaimOrRefuse(scpi_t *context, const char *cmd)\n{",
            "bool my$SD_ClaimOrRefuse(scpi_t *context, const char *cmd);\n"
            "static bool SD_ClaimOrRefuse(scpi_t *context, const char *cmd)\n{",
            1)
        pair = pair.replace(
            "    if (!SD_ClaimOrRefuse(context, \"SPACe\")) {",
            "    if (!my$SD_ClaimOrRefuse(context, \"SPACe\")) {", 1)
        pair = pair.replace(
            "    if (!SD_ArmOrRefuse(context, \"SPACe\", pCfg)) {\n"
            "        return SCPI_RES_ERR;\n    }\n",
            "    if (!SD_ArmOrRefuse(context, \"SPACe\", pCfg)) {\n"
            "        return SCPI_RES_ERR;\n    }\n"
            "    if (!(SD_ClaimOrRefuse)(context, \"SPACe\")) {\n"
            "        return SCPI_RES_ERR;\n    }\n", 1)
        assert pair.count("my$") == 2 and "(SD_ClaimOrRefuse)(" in pair
        _ck("a claim moved past the arm by two misreadings that cancel in a "
            "count is refused",
            any(disagree in p and "the census counts a call" in p
                and "accounting finds a call" in p
                for p in check(pair)[0]), True)

        # 13. The same move made with splices: a splice-CREATED `//` hides
        #     the claim ahead of the arm, and a claim whose NAME a splice
        #     splits is made after it. Read the compiler's way, the census
        #     simply sees a claim taken after the arm.
        spair = _GOOD.replace(
            "    if (!SD_ClaimOrRefuse(context, \"SPACe\")) {\n"
            "        return SCPI_RES_ERR;\n    }\n",
            "    /\\\n/ if (!SD_ClaimOrRefuse(context, \"SPACe\")) "
            "{ return SCPI_RES_ERR; }\n", 1)
        spair = spair.replace(
            "    if (!SD_ArmOrRefuse(context, \"SPACe\", pCfg)) {\n"
            "        return SCPI_RES_ERR;\n    }\n",
            "    if (!SD_ArmOrRefuse(context, \"SPACe\", pCfg)) {\n"
            "        return SCPI_RES_ERR;\n    }\n"
            "    if (!SD_ClaimOr\\\nRefuse(context, \"SPACe\")) "
            "{ return SCPI_RES_ERR; }\n", 1)
        assert "SD_ClaimOr\\\nRefuse" in spair and "/\\\n/ if" in spair
        _ck("a claim moved past the arm with splices is refused",
            any("arms before it calls SD_ClaimOrRefuse()" in p
                for p in check(spair)[0]), True)

        # 14-17. FORmat's retraction at its own call site, in the spellings
        #     `_calls` alone cannot read (#976 review): hidden by a comment
        #     closed by a SPLICED `*/` (raw text ran on to the next literal
        #     `*/`), a parenthesized call, a pointer, and an alias defined
        #     OUTSIDE FORmat.
        fmt_refusal = ("                                   "
                       "sd_card_manager_ClearFormatStatus)) {\n"
                       "        return SCPI_RES_ERR;")
        for label, inject, want in (
                ("hidden by a splice-closed comment",
                 "        /* late *\\\n/ sd_card_manager_ClearFormatStatus(); "
                 "/* end */\n",
                 "at its own call site"),
                ("as a parenthesized call",
                 "        (sd_card_manager_ClearFormatStatus)();\n",
                 "names sd_card_manager_ClearFormatStatus 2 times"),
                ("through a pointer",
                 "        void (*undo)(void) = "
                 "sd_card_manager_ClearFormatStatus;\n        undo();\n",
                 "names sd_card_manager_ClearFormatStatus 2 times")):
            mutated = _GOOD.replace(
                fmt_refusal,
                fmt_refusal.replace("        return SCPI_RES_ERR;",
                                    inject + "        return SCPI_RES_ERR;"),
                1)
            assert mutated != _GOOD
            _ck("a call-site retraction %s is refused" % label,
                any(want in p for p in check(mutated)[0]), True)
        aliased = _GOOD.replace(
            "scpi_result_t SCPI_StorageSDFormat(scpi_t * context) {\n",
            "#define UNDO_FORMAT sd_card_manager_ClearFormatStatus\n"
            "scpi_result_t SCPI_StorageSDFormat(scpi_t * context) {\n", 1)
        aliased = aliased.replace(
            fmt_refusal,
            fmt_refusal.replace("        return SCPI_RES_ERR;",
                                "        UNDO_FORMAT();\n"
                                "        return SCPI_RES_ERR;"), 1)
        assert "UNDO_FORMAT();" in aliased
        _ck("a call-site retraction through an alias defined OUTSIDE FORmat "
            "is refused",
            any("named in a preprocessor directive" in p
                for p in check(aliased)[0]), True)

        # 18-22. The shapes `cdef`'s own self-test pins, here against the
        #     whole checker -- each one once passed this file clean.
        claim_block = ("    if (!SD_ClaimOrRefuse(context, \"SPACe\")) {\n"
                       "        return SCPI_RES_ERR;\n    }\n")
        arm_block = ("    if (!SD_ArmOrRefuse(context, \"SPACe\", pCfg)) {\n"
                     "        return SCPI_RES_ERR;\n    }\n")
        arm_call = "SD_ArmOrRefuse(context, \"SPACe\", pCfg)"
        for label, src in (
                ("a declaration standing where the claim was",
                 _GOOD.replace(claim_block,
                               "    __attribute__((unused)) bool "
                               "SD_ClaimOrRefuse(scpi_t *context, "
                               "const char *cmd);\n", 1)),
                ("a member call standing where the claim was",
                 _GOOD.replace("    if (!SD_ClaimOrRefuse(context, \"SPACe\")) {",
                               "    if (!hooks.SD_ClaimOrRefuse(context, "
                               "\"SPACe\")) {", 1)),
                ("an unevaluated sizeof standing where the claim was",
                 _GOOD.replace(claim_block,
                               "    (void)sizeof(SD_ClaimOrRefuse(context, "
                               "\"SPACe\"));\n", 1)),
                ("a `%:define` macro body standing for two arms",
                 _GOOD.replace(arm_block,
                               "%:define ARM_SPACE() " + arm_call + "\n"
                               + arm_block.replace(arm_call, "ARM_SPACE()", 1)
                               + "    (void)ARM_SPACE();\n", 1))):
            assert src != _GOOD
            _ck("%s is refused" % label,
                any(unaccounted in p for p in check(src)[0]), True)
        apos = _GOOD.replace(arm_block, arm_block
                             + "#if 0\nit's the old path\n#endif\n"
                             "    (void)" + arm_call + ";\n"
                             "#if 0\nthat's it\n#endif\n", 1)
        assert apos != _GOOD
        _ck("a second arm between two `#if 0` blocks holding an apostrophe "
            "is SEEN, not masked away",
            any("arms 2 times" in p for p in check(apos)[0]), True)

        # 23-25. #976 audit round 8: phase 2 applied TWICE. A `//` comment
        #     ending in TWO backslashes has only the second spliced (phase 2
        #     is one pass over the physical source), so the comment ends at
        #     the blank line below it and the next line is code GCC builds
        #     (measured). `compiler_text()` masked the JOINED text as if it
        #     were raw, spliced the leftover backslash again, and swallowed
        #     that line: a second arm, or a call-site retraction, placed
        #     there passed clean. The Windows path is the spelling a person
        #     writes by accident.
        for label, comment in (("`// x \\\\`", "    // x \\\\\n"),
                               ("`// see C:\\temp\\\\`",
                                "    // see C:\\temp\\\\\n")):
            hidden_arm = _GOOD.replace(
                arm_block, arm_block + comment + "\n    if (!" + arm_call
                + ") { return SCPI_RES_ERR; }\n", 1)
            assert hidden_arm != _GOOD
            _ck("a second arm after %s and a blank line is SEEN" % label,
                any("arms 2 times" in p for p in check(hidden_arm)[0]), True)
        hidden_retraction = _GOOD.replace(
            fmt_refusal, fmt_refusal.replace(
                "        return SCPI_RES_ERR;",
                "        // x \\\\\n\n"
                "        sd_card_manager_ClearFormatStatus();\n"
                "        return SCPI_RES_ERR;"), 1)
        assert hidden_retraction != _GOOD
        _ck("a call-site retraction after `// x \\\\` and a blank line is SEEN",
            any("at its own call site" in p
                for p in check(hidden_retraction)[0]), True)
        # (The audit's own spelling -- an early `return true;` there, inside
        # the HELPER -- is not this file's to catch: it no longer reads the
        # helper's control flow at all (see "What moved to a host test"),
        # and a plain early return reads clean here too. The sha256 pin
        # catches it; `hash_function.py --self-test` pins that.)

        # 26-28. #976 audit round 8: the SPACe claim replaced by an
        #     occurrence that is NOT an evaluated call -- `sizeof`'s operand
        #     reached through `!` or through a compound literal, and a
        #     parenthesized block-scope DECLARATOR -- each read as the claim.
        for label, rep in (
                ("an unevaluated `sizeof !` operand",
                 "    (void)sizeof !SD_ClaimOrRefuse(context, \"SPACe\"); "
                 "if (0) {"),
                ("an unevaluated operand behind a compound literal",
                 "    (void)sizeof((bool[]){SD_ClaimOrRefuse(context, "
                 "\"SPACe\")}); if (0) {"),
                ("a parenthesized block-scope declarator",
                 "    extern bool (SD_ClaimOrRefuse(scpi_t *, const char *)); "
                 "if (0) {")):
            src = _GOOD.replace(
                "    if (!SD_ClaimOrRefuse(context, \"SPACe\")) {", rep, 1)
            assert src != _GOOD
            _ck("the SPACe claim replaced by %s is refused" % label,
                any(unaccounted in p for p in check(src)[0]), True)

        # 29-35. #976 round-8 review: the same claim inside an unevaluated
        #     operand the first round-8 reader still misread -- the first
        #     two shapes are a REGRESSION it introduced (the original walk
        #     refused them). See `_unevaluated_claims`.
        for label, rep in _unevaluated_claims(
                "SD_ClaimOrRefuse(context, \"SPACe\")"):
            src = _GOOD.replace(
                "    if (!SD_ClaimOrRefuse(context, \"SPACe\")) {",
                "    " + rep, 1)
            assert src != _GOOD
            _ck("the SPACe claim inside %s is refused" % label,
                any(unaccounted in p for p in check(src)[0]), True)
    finally:
        KNOWN_PLAIN_ARM_SITES = saved

    # ======================================================================
    # properties 1 and 4, in SCPIInterface.c: claim precedes arm, and the
    # streaming-log arm's verdict is consumed (#942)
    # ======================================================================
    probs, n = check_stream(_GOOD_STREAM)
    _ck("a compliant streaming-log arm is clean", probs, [])
    _ck("...and reports the one site examined", n, 1)

    # ---- property 1: the claim precedes the arm, at this site too ----------
    # A plain "A before B" over the whole function -- not one of the fifteen
    # branch-gated shapes above, so it was not deleted with property 5.
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

    # THE narrowing: a verdict captured into a variable and never branched on
    # again used to be caught here (audit against the OLD property 5, "a
    # verdict read into a variable and never branched on is the discard of
    # #942 with an extra line"). Property 4 only ever claimed CONSUMED, never
    # BRANCHED ON, and the code now matches that claim exactly -- this input
    # is clean.
    untested = _GOOD_STREAM.replace(_STREAM_REFUSAL, "")
    assert untested != _GOOD_STREAM
    _ck("a verdict captured but never branched on is no longer flagged here",
        check_stream(untested)[0], [])

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

    # A transformed capture. Only the assignment PREFIX was read, so the
    # variable held the NEGATION of the verdict and every branch on it meant
    # the opposite of what it read (audit finding 1). This is still property
    # 4's business: a transformed capture is not a recognisable verdict.
    inverted = _GOOD_STREAM.replace(
        "bool sdArmed = sd_card_manager_UpdateSettingsForStreamingLog(pSDCardSettings);",
        "bool sdArmed = sd_card_manager_UpdateSettingsForStreamingLog(pSDCardSettings) == false;")
    assert inverted != _GOOD_STREAM
    _ck("an initialiser that does not end at the arm call is refused",
        any("TRANSFORMATION" in p for p in check_stream(inverted)[0]), True)

    # Everything that used to follow here -- the refusal branch's position
    # against the readiness poll, the release-before-clear ordering under the
    # claim, the compound-guard/nested-return/brace-less/comment-mention
    # mutations that defeated the old positional version of THIS site's check
    # -- was the same branch-gated reasoning #976 removed from the helper in
    # `SCPIStorageSD.c`, at a second site. It is gone from here too, with NO
    # replacement host-test model yet (unlike the helper's
    # `test_971_sd_arm_refusal_order.c`) -- see "The same shape at a second
    # site" in the module docstring.

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

    # ---- occurrence accounting at the seventh site --------------------------
    # A second streaming-log arm with a parenthesized callee: `_call_positions`
    # saw one arm, so the "exactly one" count and property 4's verdict read
    # were computed from a file with a call in it they never saw.
    paren_stream = _GOOD_STREAM.replace(
        "    sd_card_manager_ReleaseClaim();\n    int readyWait",
        "    (void)(sd_card_manager_UpdateSettingsForStreamingLog)"
        "(pSDCardSettings);\n"
        "    sd_card_manager_ReleaseClaim();\n    int readyWait", 1)
    assert paren_stream != _GOOD_STREAM
    _ck("a parenthesized-callee second streaming arm is not silently "
        "uncounted",
        any("disagree about WHICH text is a call" in p
            for p in check_stream(paren_stream)[0]), True)
    # ...and the claim replaced by a DECLARATION of it, which both views
    # used to count as the claim (#976 review).
    decl_claim = _GOOD_STREAM.replace(
        "    if (!sd_card_manager_TryClaim()) {",
        "    __attribute__((unused)) bool sd_card_manager_TryClaim(void);\n"
        "    if (0) {", 1)
    assert decl_claim != _GOOD_STREAM
    _ck("a declaration standing where the streaming claim was is refused",
        any("no recogniser accounts for" in p
            for p in check_stream(decl_claim)[0]), True)
    # ...and the claim taken through a pointer instead of a call.
    ptr_claim = _GOOD_STREAM.replace(
        "    if (!sd_card_manager_TryClaim()) {",
        "    bool (*take)(void) = sd_card_manager_TryClaim;\n"
        "    if (!sd_card_manager_TryClaim() || !take()) {", 1)
    assert ptr_claim != _GOOD_STREAM
    _ck("a second claim reached through a function pointer is refused",
        any("no recogniser accounts for" in p
            for p in check_stream(ptr_claim)[0]), True)
    # ...and #976 audit round 8's three, at this site: the claim replaced by
    # an unevaluated `sizeof` operand reached through `!` or a compound
    # literal, or by a parenthesized block-scope declarator of it.
    for label, rep in (
            ("an unevaluated `sizeof !` operand",
             "    (void)sizeof !sd_card_manager_TryClaim(); if (0) {"),
            ("an unevaluated operand behind a compound literal",
             "    (void)sizeof((bool[]){sd_card_manager_TryClaim()}); "
             "if (0) {"),
            ("a parenthesized block-scope declarator",
             "    extern bool (sd_card_manager_TryClaim(void)); if (0) {")):
        src = _GOOD_STREAM.replace("    if (!sd_card_manager_TryClaim()) {",
                                   rep, 1)
        assert src != _GOOD_STREAM
        _ck("the streaming claim replaced by %s is refused" % label,
            any("no recogniser accounts for" in p
                for p in check_stream(src)[0]), True)
    # ...and the round-8 review's seven, at this site too.
    for label, rep in _unevaluated_claims("sd_card_manager_TryClaim()"):
        src = _GOOD_STREAM.replace("    if (!sd_card_manager_TryClaim()) {",
                                   "    " + rep, 1)
        assert src != _GOOD_STREAM
        _ck("the streaming claim inside %s is refused" % label,
            any("no recogniser accounts for" in p
                for p in check_stream(src)[0]), True)

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
                    help="path to SCPIStorageSD.c (property 1's six sites, "
                         "plus 2 and 3)")
    ap.add_argument("--interface",
                    default="firmware/src/services/SCPI/SCPIInterface.c",
                    help="path to SCPIInterface.c (property 1's seventh "
                         "site, plus 4)")
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
        print("FAIL: SD arm-refusal call-site check (%d arm site(s) in %s, "
              "%d in %s)" % (examined, os.path.basename(args.sd),
                             stream_examined, os.path.basename(args.interface)))
        for path, found in ((args.sd, problems), (args.interface, stream)):
            for p in found:
                print("  - %s: %s" % (os.path.basename(path), p))
        return 1
    print("OK: %d arm site(s) in %s take the claim (%s()) before arming; "
          "%s() reaches its retraction through %s()'s callback parameter and "
          "not at its own call site, and publishes before it arms; the other "
          "sites go through %s(), which passes NULL. In %s(), the claim "
          "(%s()) precedes the arm and %s()'s verdict is consumed (captured "
          "into a variable or tested inline, not discarded or cast to "
          "`(void)`). Every occurrence of the %d names those properties are "
          "counted from is accounted for, and the census -- reading the "
          "source the way the compiler does -- finds exactly the calls "
          "accounting found, at the same positions. Names that are not in "
          "the text (macro token pasting, a header alias) and anything that "
          "needs the preprocessor evaluated (a call inside `#if 0` still "
          "counts) are out of reach. %s()'s own internal ordering is checked by "
          "tests/host/test_971_sd_arm_refusal_order.c instead; the position "
          "of %s()'s refusal against the readiness poll, and its `mode` "
          "clear under the claim, are not checked by anything right now -- "
          "see the module docstring."
          % (examined, os.path.basename(args.sd), CLAIM_TAKER, FORMAT_FN,
             ARM_HELPER, ARM_WRAPPER, STREAM_FN, STREAM_CLAIM_TAKER,
             STREAM_ARM_CALL, len(SD_ACCOUNTED) + len(STREAM_ACCOUNTED),
             ARM_HELPER, STREAM_FN))
    return 0


if __name__ == "__main__":
    sys.exit(main())
