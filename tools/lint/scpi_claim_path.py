#!/usr/bin/env python3
"""Every SYSTem:MEMory:* setter must take the streaming config-change claim.

## What this protects

The seven `SYSTem:MEMory:*` setters (`SD:BUFfer`, `WIFI:BUFfer`, `USB:BUFfer`,
`SAMPle:POOL`, `ENCoder:BUFfer`, `AUTO`, `RESet`) must not mutate the memory
config while a streaming session is starting or running. #857 gave them that
protection through a SINGLE shared helper, `SCPI_MemRunClaimed`, which takes
`Streaming_BeginConfigChange()` before dispatching to each command's own
`...Claimed` body.

Four edits would silently remove the protection, and none is loud:

1. **Route one command off the helper** -- give it back a plain body that does
   its own stream-state test, or none. The other six keep working, so nothing
   on the bench necessarily notices.
2. **Gut the helper** -- remove the `Streaming_BeginConfigChange()` call
   inside it; or keep the call and throw the verdict away
   (`(void)Streaming_BeginConfigChange();`, one `(void)` away from an
   unused-variable warning); or keep the assignment and delete only the
   `if (claim != STREAM_CFG_CLAIM_OK) return ...;` arm, so the verdict is
   STORED and never read. All seven commands still "go through the claim
   path" and none of them refuses anything.
3. **Reorder the helper** to take the claim, release it, and dispatch the
   command body afterwards (#864). Both calls are still there and every setter
   still routes through them, so 1 and 2 both pass -- while the command that
   was supposed to be protected runs with nothing held.
4. **Gut the primitive** -- leave `Streaming_BeginConfigChange()` returning
   `STREAM_CFG_CLAIM_OK` without setting its busy flag, set that flag outside
   the critical section that makes the test-and-set atomic, or set it there
   without the flag being read there AT ALL (#864). Checks 1-3 all read
   `SCPIInterface.c`, so every one of them passes.

   Read that last clause narrowly, because it is narrow: it separates a set
   from a set-beside-a-read. A `Begin` that grants unconditionally while the
   flag is mentioned in the section for any other reason -- another arm, a
   `(void)` cast, a log line -- passes. See "What a green run does NOT mean".

This checker fails on all four. 4 lives in `streaming.c`, which is why the CI
gate triggers on `firmware/src/services/streaming.*` as well -- a trigger that
until #864 fired on a file the checker asserted nothing about.

5. **Open the interlock** (#977) -- the claim above is not the only one guarding
   streaming setup. `Streaming_BeginSessionStart` is a SECOND claim, taken by
   `SYSTem:STReam:START`, `SYST:STR:THRoughput` and `SYST:STR:WIFI:FINd?`, and
   BOTH families reach `PrepareStreamingBuffers`, which re-carves the single
   streaming pool and installs the new pointers into seven subsystems. Until
   #977 neither `Begin` looked at the other's flag, so a `SYST:MEM:AUTO` could
   re-partition while a finder was midway through installing the previous
   partition's pointers. Each `Begin` now also refuses while the OTHER flag is
   set -- two conditions in two functions, which is precisely the arrangement
   that produced the gap, so this checker gates both of them:

   * `Streaming_BeginConfigChange` READS the session-start flag inside its
     single critical section, and
   * `Streaming_BeginSessionStart` READS the config-change flag inside its own,
   * plus the session-start claim being a real test-and-set in its own right
     (set inside one critical section that also reads it) and released by
     `Streaming_EndSessionStart` -- a half of this file's subject matter that
     nothing anywhere checked before #977.

   Delete EITHER direction and the tree goes red; the two arms are asserted
   independently, so half the property cannot stand in for the whole. Checks
   1-4 all pass on a tree with the interlock removed -- the pre-#977 tree
   passes every one of them.

   Inside, not merely present: a cross-test hoisted ABOVE `taskENTER_CRITICAL`
   is a TOCTOU (the other transport takes its claim between the read and the
   take), and is refused.

## Positional reasoning, and where it stops

Properties 3 and 4 are about position, and position is only meaningful against
ONE opener and ONE closer. "Between the first opener and the last closer" is NOT
"inside a region" -- the gap BETWEEN two regions satisfies it. Both were first
written that way here and both were defeatable (round 1 of PR #894, found
independently by the codex leg and Qodo `/agentic_review`).

Anything other than exactly one of each is therefore REFUSED rather than passed
on a looser bound. Two of each is the unsound case. An UNBALANCED count -- one
opener with a second closer on an early-return path -- is correct C, and is
refused only because deciding it needs control flow: the extra closer may sit
before or after the item depending on which branch runs. The refusal prints the
counts so the message distinguishes the two. #896 tracks it.

Being exact about property 4's last clause: it establishes that the flag is
READ inside the same critical section that sets it. It does not establish that
the read is what gates the grant -- regex cannot show that. It is the
difference between a plain set and a test-and-set, which is the mutation that
was getting through.

## What a green run does NOT mean

There is no control flow here and no reachability, and that bounds EVERY
property, not only the dead-call case above. Known and filed as #896:

* Property 2's verdict requirement shows the verdict is READ, not BRANCHED
  ON. Passing it to another function counts, so
  `LOG_I("claim=%u", Streaming_BeginConfigChange());` above an unconditional
  dispatch passes.
* Property 4's read requirement is satisfied by ANY mention of the flag in
  the section, gating or not: a read in the non-granting arm, a read after the
  set, a `(void)flag;`, or a log line's format argument. Instrument
  `Streaming_BeginConfigChange` once and the arm is disarmed for good.
* Property 5's cross-read requirement inherits that limit exactly. It shows
  each `Begin` MENTIONS the other claim's flag inside its critical section --
  not that the mention REFUSES anything. `(void)gSessionStartBusy;` there
  satisfies it. What it does catch is the honest regression: the arm being
  deleted, or moved out of the section, which is how the gap existed in the
  first place.
* Property 5 says nothing about the two claims being the only two. A third
  path to `PrepareStreamingBuffers` that takes neither claim is invisible here,
  because this file reads the primitives, not the callers.
* Property 4 says nothing about what the grant is CONDITIONED on. Delete
  `Streaming_BeginConfigChange`'s `if (pStreamCfg->IsEnabled ||
  pStreamCfg->Running)` arm and it hands the claim out mid-session, with the
  flag still set, in the section, and read.
* Property 3 cannot see branches. A helper that releases the claim only on the
  body's success path passes and leaks `gCfgChangeBusy`, which then refuses
  every later `SYST:STR:START`.
* A dead branch satisfies 3 and 4 as readily as 1 -- an `End` whose only clear
  sits inside `if (0)`, or a `Begin` that sets then immediately clears.

Closing these means real reachability analysis, i.e. a different tool. The
honest description of this one is: it catches an honest regression, not a
determined or half-finished refactor.

## Why it is a source check and not a bench test

`test_857_mem_guard_claim.py`'s race arm is the only arm that separates a real
claim from a plain stream-state guard, and it can only hammer `SYST:MEM:AUTO`:
the race needs the claim held long enough to land inside, and AUTO is the only
one of the seven that holds it across work -- `SCPI_MemAutoBalanceClaimed` runs
`PrepareStreamingBuffers` (~1 s quiesce), while the other six are a parse, a
range check and one scalar store. Rotating the hammer to those six would miss a
window that does not exist and report a false negative, so the coverage gap
cannot be closed from the bench (test-suite #246).

It is closed here instead, where the property actually lives, at no bench cost.

What it deliberately does NOT do (#864(3)): reachability. A dead call --
`if (0) return SCPI_MemRunClaimed(...);` above a direct unclaimed call --
satisfies the routing check. Detecting that means real reachability analysis
rather than regex, and it requires deliberate sabotage that a reviewer sees.
Resisting a hostile author is a different goal from catching an honest
regression, and only the second one is worth this tool's complexity budget.

Run:
    python3 tools/lint/scpi_claim_path.py
    python3 tools/lint/scpi_claim_path.py --self-test
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# Reused rather than re-implemented: SCPIInterface.c has commented-out
# `.pattern` entries, so a checker that does not strip comments counts
# commands that are not shipped. `_REGISTRATION` / `_joined` handle adjacent
# string-literal concatenation, which is legal C and appears in that table.
from scpi_wiki_sync import (               # noqa: E402
    strip_c_comments, _joined,
)

# A table row, e.g. `{.pattern = "A", .callback = X,}`. Rows contain no nested
# braces, so a non-greedy brace pair is an exact row match.
#
# NOT scpi_wiki_sync's `_REGISTRATION`: that one requires `.pattern` to be
# IMMEDIATELY followed by `.callback`. Designated initializers are legal in any
# order, so `{.callback = X, .pattern = "..."}` is a valid registration it does
# not match -- and a setter this checker never examines is exactly the silent
# pass it exists to prevent (pre-merge audit on PR #863). Each field is found
# independently within the row instead.
_ROW = re.compile(r"\{([^{}]*)\}")
_ROW_PATTERN = re.compile(r'\.pattern\s*=\s*((?:"[^"]*"\s*)+)')
_ROW_CALLBACK = re.compile(r"\.callback\s*=\s*([A-Za-z_]\w*)")
_ROW_PATTERN_FIELD = re.compile(r"\.pattern\s*=")
_ROW_NULL = re.compile(r"\.pattern\s*=\s*NULL\b")

CLAIM_HELPER = "SCPI_MemRunClaimed"
CLAIM_BEGIN = "Streaming_BeginConfigChange"
CLAIM_END = "Streaming_EndConfigChange"
# The READ side of the claim, in the streaming source. Used to discover the
# flag name rather than hard-coding it, so a rename is followed instead of
# silently disarming the assertions below (#864).
CLAIM_READER = "Streaming_ConfigChangeInProgress"
# #977: the OTHER claim in the same file, which this one must interlock with.
START_BEGIN = "Streaming_BeginSessionStart"
START_END = "Streaming_EndSessionStart"
TASK_ENTER = "taskENTER_CRITICAL"
TASK_EXIT = "taskEXIT_CRITICAL"
# The namespace, as NODES rather than one spelling. libscpi gives each node
# exactly two legal spellings -- the full node, or the node truncated at its
# first lowercase letter (CLAUDE.md, "SCPI Abbreviation Rule"; the authority is
# matchPattern in libscpi/src/utils.c). Each node is independently full or
# short, so `SYSTem:MEMory:` has FOUR legal spellings and a registration may
# use any of them.
#
# Matching only the full spelling meant an abbreviated row -- a legal command
# libscpi accepts -- was not recognised as a memory setter and was never
# examined, while the summary still reported having checked them all
# (pre-merge audit on PR #863). All 14 current registrations use the full
# form, so this is a convention the checker was relying on rather than a
# constraint it could count on.
MEM_NODES = ("SYSTem", "MEMory")


def _node_spellings(node):
    """The legal spellings of one SCPI node: full, and truncated at the first
    lowercase letter. A node with no lowercase has exactly ONE spelling."""
    short = node
    for i, ch in enumerate(node):
        if ch.islower():
            short = node[:i]
            break
    return (node,) if short == node else (node, short)


def _mem_prefixes():
    """Every legal spelling of the `SYSTem:MEMory:` prefix."""
    out = [""]
    for node in MEM_NODES:
        out = [p + sp + ":" for p in out for sp in _node_spellings(node)]
    return tuple(sorted(set(out)))


MEM_PREFIXES = _mem_prefixes()

# The seven that existed when this checker was written. Used ONLY as a floor:
# a new setter must also claim (it is discovered from the table, not from this
# list), but dropping below seven means the table moved or the regex broke, and
# a checker that silently examines nothing is the failure this guards against.
KNOWN_MEM_SETTERS = 7

_STR_OR_CHAR = re.compile(r'"(?:\\.|[^"\\])*"' r"|'(?:\\.|[^'\\])*'", re.S)


def _blank(text):
    """`text` with string/char literals replaced by spaces, offsets preserved.

    Every call-form and assignment scan below runs on this, so a literal that
    merely SPELLS a call cannot satisfy one (pre-merge audit on PR #863).
    """
    return _STR_OR_CHAR.sub(lambda m: " " * len(m.group(0)), text)


def function_body(text, name):
    """The brace-delimited body of C function `name`, or None if not found.

    `text` must already have comments stripped. String and character literals
    are blanked before brace counting so a brace inside a literal -- e.g. a
    format string -- cannot unbalance the scan.
    """
    # Anchored to a line that STARTS with a return type, because the bare
    # form `\bNAME\s*\([^;{]*\)\s*\{` also matches a CALL inside a
    # condition: in `if (foo(a)) {` the group can absorb `a)` and take the
    # outer `)` before the brace, so a call would be read as a definition and
    # the wrong body returned. A prototype is still excluded -- it ends in `;`,
    # which `[^;{]*` cannot cross.
    sig = re.search(r"(?m)^[A-Za-z_][\w \t\*]*\b%s\s*\([^;{]*\)\s*\{"
                    % re.escape(name), text)
    if not sig:
        return None
    start = text.index("{", sig.start())
    masked = _STR_OR_CHAR.sub(lambda m: " " * len(m.group(0)), text)
    depth = 0
    for i in range(start, len(masked)):
        if masked[i] == "{":
            depth += 1
        elif masked[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return None


def _calls(body, name):
    """True iff `body` CALLS `name`, rather than merely mentioning it.

    A raw substring test passes a body that only names the helper -- e.g.
    `const char *marker = "SCPI_MemRunClaimed";` beside a direct call to the
    unclaimed inner body. That reports OK on a setter with no claim at all
    (pre-merge audit on PR #863). String literals are blanked first so a
    mention inside one cannot satisfy the call form either.
    """
    return re.search(r"\b%s\s*\(" % re.escape(name), _blank(body)) is not None


def _call_positions(body, name):
    """Offsets of every CALL to `name` in `body`, in order.

    Same call form as `_calls`, but positional: three of the properties below
    are about ORDER, not presence, and presence alone passes a helper that
    takes the claim and releases it around nothing (#864).
    """
    return [m.start() for m in
            re.finditer(r"\b%s\s*\(" % re.escape(name), _blank(body))]


def dispatch_param(text, name):
    """Name of the function-POINTER parameter of C function `name`, or None.

    Discovered from the signature rather than hard-coded, for the same reason
    as CLAIM_READER: the checker should follow a rename, not stop asserting.
    """
    sig = re.search(r"(?m)^[A-Za-z_][\w \t\*]*\b%s\s*\(([^;{]*)\)\s*\{"
                    % re.escape(name), text)
    if not sig:
        return None
    m = re.search(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*\(", sig.group(1))
    return m.group(1) if m else None


# What counts as clearing the flag. `false` and `0x0` are here because the flag
# is only required to be `static volatile` -- retyping it `bool` is an ordinary
# refactor, and a checker that then reported "never clears" would be a false
# red on correct code.
_ZERO_RHS = re.compile(r"^(?:0[uUlL]*|0[xX]0+[uUlL]*|false)$")

# A file-scope declaration of `%s`. Both qualifier orders are legal C and the
# tree uses the first; accepting only it redded the gate on correct code (opus
# hunter, PR #894 round 2). `[\w\s\*]` cannot cross `=`, which is what keeps
# another declaration's INITIALISER from being offered as a candidate name.
_STATIC_VOLATILE = (r"(?:static\s+volatile|volatile\s+static)\s+"
                    r"[A-Za-z_][\w\s\*]*?\b%s\s*(?:=|;|\[)")


def _assignments(body, name):
    r"""[(offset, assigns_zero)] for every plain `name = <rhs>;` in `body`.

    `|=` and `&=` count: `flag |= 1u` sets and `flag &= 0` clears, and reading
    the first as "never sets" was worse than refusing -- the message actively
    misdescribed correct code (opus hunter, PR #894 round 2).

    `(?!=)` is what keeps comparisons out, and it is load-bearing: `flag == 0`
    is a read, and crediting it as a write would let a Begin that only TESTS
    the flag pass as one that takes it. A lookbehind excluding `!<>+-*/%&|^`
    was written beside it and removed as dead -- `\s*` consumes only
    whitespace, so `!=`, `>=` and `+=` cannot reach the operator in the first
    place, and no arm could tell the two versions apart.

    Only `=`, `|=` and `&=` are recognised. `&= ~MASK` -- the idiomatic
    bit-clear -- is NOT a clear here, because the only `&=` right-hand side
    this can evaluate is a literal zero. NOT handled, and known: a
    multi-declarator declaration
    (`static volatile uint32_t gA = 0u, gFlag = 0u;`) hides the second name
    from the declarator search, and a macro-wrapped write
    (`CLAIM_CLEAR(gFlag);`) is invisible here. Both would red the gate on
    correct code; neither occurs in this tree. See #896.
    """
    out = []
    for m in re.finditer(
            r"\b%s\s*(\|=|&=|=(?!=))\s*([^;]+);" % re.escape(name),
            _blank(body)):
        out.append((m.start(), _write_kind(m.group(1), m.group(2))))
    return out


def _unparen(rhs):
    """`rhs` with balanced outer parentheses peeled off."""
    r = rhs.strip()
    while len(r) > 1 and r[0] == "(" and r[-1] == ")":
        depth = 0
        for i, ch in enumerate(r):
            depth += (ch == "(") - (ch == ")")
            if depth == 0 and i < len(r) - 1:
                return r          # the leading `(` closes early: not wrapping
        r = r[1:-1].strip()
    return r


def _write_kind(op, rhs):
    """"set" | "clear" | None, for a binary flag.

    The operator matters, not only the value. Classifying `|=` and `&=` by
    their right-hand side alone credited `flag &= 1u` as taking the claim and
    `flag |= 0u` as releasing it, when both LEAVE THE FLAG AS IT WAS -- a
    checker certifying a Begin that never acquires (Qodo /agentic_review, PR
    #894 round 3). Only definite transitions count:

        =  0      clear        =  nonzero   set
        &= 0      clear        &= nonzero   neither (preserving)
        |= 0      neither      |= nonzero   set

    Parentheses are peeled first, or `flag = (0u)` reads as a set (same
    review). An RHS this cannot evaluate -- a macro, a variable -- is not zero,
    so it counts as a set and never as a clear: the safe direction, since a
    missed clear is a loud red and a wrongly-credited clear is a silent pass.
    """
    zero = bool(_ZERO_RHS.match(_unparen(rhs)))
    if op == "=":
        return "clear" if zero else "set"
    if op == "&=":
        return "clear" if zero else None
    return None if zero else "set"          # `|=`


def mem_setters(registrations):
    """Registered SYSTem:MEMory:* patterns that MUTATE config -> callbacks.

    Queries are excluded by the trailing `?`: they read and cannot corrupt a
    session's partition, so they are deliberately outside the claim.
    """
    return [(p, cb) for p, cb in registrations
            if p.startswith(MEM_PREFIXES) and not p.endswith("?")]


def parse_registrations(text):
    """-> (registrations, unparsed) from an already-comment-stripped table.

    Fields are read independently within each row, so either designated-
    initializer order works. `unparsed` counts rows that HAVE a `.pattern`
    field but from which a (pattern, callback) pair could not be recovered --
    reported rather than skipped, because a row this cannot read is a command
    it cannot check.
    """
    regs, unparsed = [], 0
    for row in _ROW.findall(text):
        if not _ROW_PATTERN_FIELD.search(row):
            continue
        if _ROW_NULL.search(row):
            continue                      # libscpi's end-of-table sentinel
        pat, cb = _ROW_PATTERN.search(row), _ROW_CALLBACK.search(row)
        if pat and cb:
            regs.append((_joined(pat.group(1)), cb.group(1)))
        else:
            unparsed += 1
    return regs, unparsed


def check(source_text):
    """-> (problems, examined_count). Pure, so --self-test can drive it."""
    text = strip_c_comments(source_text)
    registrations, unparsed = parse_registrations(text)
    setters = mem_setters(registrations)
    problems = []

    if unparsed:
        problems.append(
            "%d command-table row(s) have a .pattern field this checker could "
            "not read. A row it cannot parse is a command it cannot check, so "
            "this fails rather than reporting on the rest." % unparsed)

    if not setters:
        problems.append(
            "no SYSTem:MEMory:* setters found in the command table -- the "
            "table moved or the registration regex broke. Refusing to report "
            "a pass on a file this checker could not read.")
        return problems, 0

    if len(setters) < KNOWN_MEM_SETTERS:
        problems.append(
            "found only %d SYSTem:MEMory:* setters, expected at least %d. "
            "If one was genuinely removed, lower KNOWN_MEM_SETTERS in this "
            "file and say why in the commit."
            % (len(setters), KNOWN_MEM_SETTERS))

    # (1) every setter routes through the shared helper
    for pattern, callback in sorted(setters):
        body = function_body(text, callback)
        if body is None:
            problems.append(
                "%s -> %s(): could not find that function to check it"
                % (pattern, callback))
        elif not _calls(body, CLAIM_HELPER):
            problems.append(
                "%s -> %s() does not go through %s(), so it can mutate the "
                "memory config while a session is arming or running (#857)."
                % (pattern, callback, CLAIM_HELPER))

    # (2) the shared helper still actually claims. Without this, (1) passes
    # while every command claims nothing -- all seven routed through a helper
    # that no longer holds the exclusion.
    helper = function_body(text, CLAIM_HELPER)
    if helper is None:
        problems.append(
            "%s() not found -- every setter above was checked for a call to a "
            "function that does not exist." % CLAIM_HELPER)
    else:
        for needed, why in ((CLAIM_BEGIN, "take"), (CLAIM_END, "release")):
            if not _calls(helper, needed):
                problems.append(
                    "%s() does not call %s(), so it does not %s the claim -- "
                    "routing through it protects nothing."
                    % (CLAIM_HELPER, needed, why))
        verdict = (_verdict_used(helper, CLAIM_BEGIN)
                   if _calls(helper, CLAIM_BEGIN) else None)
        if verdict:
            problems.append(
                "%s() %s %s(): a claim that is taken but whose verdict is "
                "never looked at refuses nothing, so every setter runs "
                "mid-session while checks above still pass. Every shape here "
                "is a one-line refactor away -- delete the rejection arm, cast "
                "the call away, or silence the now-unused variable with "
                "`(void)claim;` (#864, PR #894 rounds 2-4)."
                % (CLAIM_HELPER, verdict, CLAIM_BEGIN))
        problems.extend(_helper_order_problems(text, helper))

    return problems, len(setters)


# Positional reasoning is only sound over ONE region. Given several, "between
# the first opener and the last closer" is not "inside a region" -- the gap
# BETWEEN two regions satisfies it. Both the claim pair and the critical
# section were written that way and both were defeatable (Qodo /agentic_review
# and the codex leg on PR #894, independently). Rather than grow a matcher,
# more than one region is treated as beyond what this checker can reason about
# and REFUSED, which is the same stance it takes on a table row it cannot read.
_ONE_REGION = (
    "%(who)s contains %(counts)s. Positional containment is only decidable "
    "against exactly ONE opener and ONE closer, so this refuses rather than "
    "reporting a pass it did not establish (#864). TWO of each is the unsound "
    "case -- %(defeat)s. An UNBALANCED count (one opener, an extra closer on "
    "an early-return path) is correct C and is refused only because deciding "
    "it needs control flow this checker does not have: the extra closer may "
    "sit before or after the %(item)s depending on the branch. Both are "
    "refused, and the counts above say which you have. See #896.")


def _stmt_prefix(blanked, pos):
    """The text between the previous statement boundary and `pos`, stripped."""
    cut = max(blanked.rfind(";", 0, pos), blanked.rfind("{", 0, pos),
              blanked.rfind("}", 0, pos))
    return blanked[cut + 1:pos].strip()


# A statement prefix that means "the value goes nowhere". Enumerated rather
# than described as "empty", because a standalone discarded call does NOT
# always have an empty prefix -- a label and a `for` init clause were both
# credited as uses when this said "empty means the call stands alone"
# (opus verifier, PR #894 round 4). `(void)` is not special: any bare cast
# throws the value away just as thoroughly.
_CAST_ONLY = re.compile(r"\(\s*[A-Za-z_][\w\s\*]*\)")
_LABEL_ONLY = re.compile(r"[A-Za-z_]\w*\s*:")
_FOR_INIT = re.compile(r"\bfor\s*\($")


def _discards(prefix):
    """True iff a value produced at this statement prefix goes nowhere."""
    return (prefix == ""
            or _CAST_ONLY.fullmatch(prefix) is not None
            or _LABEL_ONLY.fullmatch(prefix) is not None
            or _FOR_INIT.search(prefix) is not None)


def _verdict_used(body, name):
    """-> None if every call to `name` has its verdict looked at, else a reason.

    Two ways to drop it, and the second is the one that got through first:

    * DISCARD -- the value goes nowhere: a bare statement, a bare cast
      (`(void)`, and equally `(int)`), a labelled statement, or a `for` init
      clause.
    * STORE AND IGNORE -- `claim = Begin();` and then nothing INSPECTS `claim`.
      Treating the assignment as consumption meant deleting only the
      `if (claim != STREAM_CFG_CLAIM_OK) return ...;` arm still passed (Qodo,
      round 3); then `(void)claim;` -- the very thing this check's own message
      described -- still passed, because a discarding cast is textually a read
      (opus verifier, round 4). A read whose own prefix discards does not
      count as an inspection.

    A call used inline -- in an `if`, in a `return` -- is a use.

    KNOWN LIMIT: passing the verdict to another function counts as a use, so
    `LOG_I("claim=%u", Streaming_BeginConfigChange());` followed by an
    unconditional dispatch passes. Separating "inspected" from "branched on"
    is control flow, which this checker does not have. #896.
    """
    b = _blank(body)
    for m in re.finditer(r"\b%s\s*\(" % re.escape(name), b):
        prefix = _stmt_prefix(b, m.start())
        if _discards(prefix):
            return "discards the result of"
        target = re.search(r"([A-Za-z_]\w*)\s*=$", prefix)
        if target:
            var = target.group(1)
            after = b.find(";", m.start())
            if after < 0:
                after = m.start()
            inspected = any(
                x.start() > after and not _discards(_stmt_prefix(b, x.start()))
                for x in re.finditer(r"\b%s\b" % re.escape(var), b))
            if not inspected:
                return "stores but never inspects the verdict of"
    return None


def _helper_order_problems(text, helper):
    """#864(2): the dispatch must happen BETWEEN the take and the release.

    Presence of both calls is what (2) above establishes, and presence is not
    the property. Rewritten as take -> release -> dispatch, the helper still
    calls both, every setter still routes through it, and the command runs
    with no claim held -- a plausible refactor accident rather than sabotage,
    which is why it gets a guard and #864(3)'s dead-call case does not.
    """
    problems = []
    param = dispatch_param(text, CLAIM_HELPER)
    begins = _call_positions(helper, CLAIM_BEGIN)
    ends = _call_positions(helper, CLAIM_END)
    if param is None:
        problems.append(
            "%s() has no function-pointer parameter, so where it dispatches "
            "the command body could not be located and the claim's position "
            "around that dispatch is UNVERIFIED. Refusing to report a pass on "
            "a property this checker could not establish." % CLAIM_HELPER)
        return problems
    if not begins or not ends:
        return problems           # already reported as a missing call, above
    if len(begins) != 1 or len(ends) != 1:
        problems.append(_ONE_REGION % {
            "who": "%s()" % CLAIM_HELPER,
            "item": "dispatch",
            "counts": "%d %s() and %d %s()"
                      % (len(begins), CLAIM_BEGIN, len(ends), CLAIM_END),
            "defeat": "a helper with two claim pairs can dispatch between "
                      "them, inside first-Begin..last-End and outside every "
                      "claim",
        })
        return problems
    begin_pos, end_pos = begins[0], ends[0]
    dispatches = _call_positions(helper, param)
    if not dispatches:
        problems.append(
            "%s() has no visible call to its %s() parameter, so either it "
            "takes and releases the claim around no work at all, or the "
            "dispatch goes through a local copy this checker cannot follow. "
            "Either way the claim's position around it is UNVERIFIED (#864)."
            % (CLAIM_HELPER, param))
    elif end_pos < begin_pos:
        problems.append(
            "%s() calls %s() before %s(): the claim is released before it is "
            "taken, so nothing is held across the dispatch (#864)."
            % (CLAIM_HELPER, CLAIM_END, CLAIM_BEGIN))
    elif not all(begin_pos < d < end_pos for d in dispatches):
        problems.append(
            "%s() dispatches %s() outside the claim -- every call must fall "
            "between %s() and %s(), or the command it wraps runs unclaimed "
            "(#864)." % (CLAIM_HELPER, param, CLAIM_BEGIN, CLAIM_END))
    return problems


# --------------------------------------------------------------------------
# #864(1) residual: the claim PRIMITIVE, in the streaming source.
#
# The CI gate already re-runs when streaming.* changes (PR #863 added the
# trigger). Triggering was only ever the necessary half: everything above
# reads SCPIInterface.c and establishes that the helper CALLS Begin/End, so a
# Begin that returned STREAM_CFG_CLAIM_OK without taking an exclusion passed
# the whole gate while the summary line still said the setters take the claim.
# A gate that runs on a file it asserts nothing about is the same defect this
# checker exists to catch, one level up -- so the trigger now has something to
# do when it fires.
# --------------------------------------------------------------------------
def claim_flag(text):
    """-> (flag_name, None) | (None, reason). Comment-stripped text.

    Discovered by intersecting the identifiers the READER mentions with the
    file's `static volatile` declarations, rather than hard-coded: a hard-coded
    name turns into a silent pass the moment someone renames the variable,
    which is precisely the failure mode being guarded against.
    """
    reader = function_body(text, CLAIM_READER)
    if reader is None:
        return None, (
            "%s() not found in the streaming source, so the flag the claim "
            "turns on could not be identified and %s()/%s() were NOT checked."
            % (CLAIM_READER, CLAIM_BEGIN, CLAIM_END))
    cands = set()
    for m in re.finditer(r"\b([A-Za-z_]\w*)\b", _blank(reader)):
        name = m.group(1)
        # The name must be the DECLARATOR, not an initialiser: `[\w\s\*]`
        # cannot cross the `=`, so `static volatile bool gNeedSharedScan =
        # false;` does not offer `false` as a candidate. Before this anchor it
        # did, and a reader gutted to `return false;` was then reported against
        # the name "false" -- the right verdict carried by a message that named
        # the wrong thing, which is the shape of finding this file exists to
        # refuse in others.
        #
        # This is the ONLY filter. A keyword blocklist was written here first
        # and then removed: with the anchor in place no arm could tell the two
        # apart, so it was defensive code that could not fail -- and a check
        # that cannot fail is what this checker is about.
        if re.search(_STATIC_VOLATILE % re.escape(name), text):
            cands.add(name)
    if len(cands) != 1:
        return None, (
            "%s() reads %d file-scope `static volatile` variable(s) -- this "
            "checker needs exactly one to follow the claim flag through %s() "
            "and %s(). Found: %s."
            % (CLAIM_READER, len(cands), CLAIM_BEGIN, CLAIM_END,
               ", ".join(sorted(cands)) or "none"))
    return cands.pop(), None


def _reads_in(body, name, lo, hi):
    """True iff the claim flag is READ in (lo, hi), not only written.

    A call to CLAIM_READER counts. Testing through the public accessor instead
    of touching the raw global is an ordinary DRY refactor and is if anything
    the better shape; rejecting it told the author their test-and-set was "a
    plain set", which was simply false (opus verifier, PR #894 round 2).

    KNOWN LIMIT, and it is not small: ANY mention counts, including a
    diagnostic one. A `LOG_D(..., "%u", flag)` inside the section satisfies
    this permanently, so a later deletion of the real test would pass. See
    #896 -- distinguishing a gating read from an incidental one needs control
    flow, which this checker does not have.
    """
    written = {pos for pos, _ in _assignments(body, name)}
    reads = [m.start() for m in re.finditer(r"\b%s\b" % re.escape(name),
                                            _blank(body))
             if m.start() not in written]
    reads += _call_positions(body, CLAIM_READER)
    return any(lo < pos < hi for pos in reads)


def _reads_name_in(body, name, lo, hi):
    """True iff `name` is MENTIONED (not assigned) at a position in (lo, hi).

    The strict twin of `_reads_in`: it does NOT credit a call to CLAIM_READER.
    Used where the accessor would be the WRONG spelling. CLAIM_READER answers
    "is a CONFIG change in flight", so crediting it as a read of the
    SESSION-START flag would pass a Begin that never looks at its own claim --
    and crediting it inside Streaming_BeginConfigChange would let that function
    satisfy the interlock by reading its own flag through the accessor.

    Same known limit as `_reads_in` and for the same reason (#896): ANY mention
    counts, a diagnostic one included, because separating a gating read from an
    incidental one needs control flow this checker does not have.
    """
    written = {pos for pos, _ in _assignments(body, name)}
    return any(lo < m.start() < hi
               for m in re.finditer(r"\b%s\b" % re.escape(name), _blank(body))
               if m.start() not in written)


def _single_section(who, body):
    """-> (lo, hi, None) | (None, None, problem) for a claim-taking function.

    Factored out because #977 needs the same "exactly one critical section"
    reasoning for Streaming_BeginSessionStart that CLAIM_BEGIN already gets, and
    a second hand-written copy is how two rules drift apart -- which is the
    defect #977 itself was filed for, one level up.
    """
    enters = _call_positions(body, TASK_ENTER)
    exits = _call_positions(body, TASK_EXIT)
    if not enters or not exits:
        return None, None, (
            "%s has no %s()/%s() around its test-and-set. Granting a claim is a "
            "read-modify-write (test the flag, then set it), which is NOT atomic "
            "on PIC32MZ, so both SCPI transports can be granted it at once "
            "(docs/MCU_REFERENCE.md, Atomicity & Concurrency Rules)."
            % (who, TASK_ENTER, TASK_EXIT))
    if len(enters) != 1 or len(exits) != 1:
        return None, None, _ONE_REGION % {
            "who": who,
            "item": "assignment",
            "counts": "%d %s() and %d %s()"
                      % (len(enters), TASK_ENTER, len(exits), TASK_EXIT),
            "defeat": "a flag assigned between two disjoint sections is inside "
                      "first-enter..last-exit and inside neither section",
        }
    return enters[0], exits[0], None


# --------------------------------------------------------------------------
# #977: the INTERLOCK between the two claims.
#
# Everything above establishes that the config-change claim is a real
# test-and-set and that all seven SYSTem:MEMory:* setters route through it. It
# says nothing about the OTHER claim in the same file -- the session-start claim
# (#850) that SYST:STR:START, SYST:STR:THRoughput and SYST:STR:WIFI:FINd? hold
# -- and before #977 the two did not exclude each other at all. Both families
# reach SCPIInterface.c's PrepareStreamingBuffers, which re-carves the single
# streaming pool and installs the new pointers into seven subsystems, so a
# SYST:MEM:AUTO could re-partition underneath a finder that was midway through
# installing the previous partition's.
#
# The fix is two conditions -- each Begin also refuses while the OTHER flag is
# set -- and two conditions in two functions are exactly the arrangement that
# produced the gap. That is what this section gates: delete either one and the
# tree goes red, instead of going quiet.
# --------------------------------------------------------------------------
def session_start_flag(text, cfg_flag):
    """-> (flag_name, None) | (None, reason). Comment-stripped text.

    Discovered, not hard-coded, for the same reason `claim_flag` is: a
    hard-coded name becomes a silent pass the moment someone renames the
    variable. There is no public reader for this claim to discover it through
    (and there deliberately is not -- see Streaming_ConfigChangeInProgress's
    warning in streaming.c), so the anchor is START_BEGIN's own body: the
    `static volatile` file-scope names it mentions, minus the config flag it
    mentions BECAUSE of the interlock.

    The config flag is subtracted by TWO independent handles -- the name
    `claim_flag` found through the reader, and whatever CLAIM_END releases --
    because the first one is not always available. A reader gutted to `return
    false;` leaves `cfg_flag` None, and without the second handle the config
    flag would then look like a second candidate here and disarm this whole
    section on a mutation aimed at the other one.

    Exactly one is required. Two would make every positional verdict below
    ambiguous, and this file's stance on an ambiguous input is to refuse rather
    than pick one and report a pass it did not establish. Note the subtraction
    is by NAME, not by "is it assigned here": discovering the flag as "the one
    START_BEGIN writes" would have been tidier and is wrong, because it makes a
    Begin that never writes its flag -- the exact regression this checks for --
    undiscoverable instead of red.
    """
    begin = function_body(text, START_BEGIN)
    if begin is None:
        return None, (
            "%s() not found in the streaming source. It is the session-start "
            "claim SYSTem:STReam:START, SYST:STR:THRoughput and "
            "SYST:STR:WIFI:FINd? take, and #977 requires it to interlock with "
            "%s() -- neither could be checked." % (START_BEGIN, CLAIM_BEGIN))
    released_by_cfg = set()
    cfg_end = function_body(text, CLAIM_END)
    if cfg_end is not None:
        for m in re.finditer(r"\b([A-Za-z_]\w*)\b", _blank(cfg_end)):
            if _assignments(cfg_end, m.group(1)):
                released_by_cfg.add(m.group(1))
    cands = set()
    for m in re.finditer(r"\b([A-Za-z_]\w*)\b", _blank(begin)):
        name = m.group(1)
        if cfg_flag is not None and name == cfg_flag:
            continue          # the interlock read, not this claim's own flag
        if name in released_by_cfg:
            continue          # the config claim's flag, by its other handle
        if re.search(_STATIC_VOLATILE % re.escape(name), text):
            cands.add(name)
    if len(cands) != 1:
        return None, (
            "%s() mentions %d file-scope `static volatile` variable(s) other "
            "than the config-change flag -- this checker needs exactly one to "
            "follow the session-start claim through %s() and %s(). Found: %s."
            % (START_BEGIN, len(cands), START_BEGIN, START_END,
               ", ".join(sorted(cands)) or "none"))
    return cands.pop(), None


def check_interlock(text, cfg_flag, cfg_begin):
    """-> problems for the session-start claim and the #977 interlock.

    `cfg_flag` may be None (the config flag was not identifiable); the
    session-start half is still checked, and the two interlock arms that need
    the config flag are reported as unverified rather than skipped silently.
    """
    problems = []
    start_flag, why = session_start_flag(text, cfg_flag)
    if start_flag is None:
        return [why]

    begin = function_body(text, START_BEGIN)
    end = function_body(text, START_END)
    if end is None:
        problems.append(
            "%s() not found in the streaming source, so the session-start claim "
            "is never released: one SYSTem:STReam:START and every later "
            "streaming command is refused until reboot (#977)." % START_END)
    elif not any(kind == "clear" for _, kind in _assignments(end, start_flag)):
        problems.append(
            "%s() never clears %s, so a session-start claim once taken is "
            "never released. Since #977 that refuses the SYSTem:MEMory:* "
            "family too, not only later starts." % (START_END, start_flag))

    lo, hi, why = _single_section("%s()" % START_BEGIN, begin)
    if why is not None:
        return problems + [why]

    sets = [pos for pos, kind in _assignments(begin, start_flag)
            if kind == "set"]
    if not sets:
        problems.append(
            "%s() never sets %s to a non-zero value, so it can report a claim "
            "it did not take -- two starts, or a start and a SYSTem:MEMory:* "
            "setter, would both be granted (#977)." % (START_BEGIN, start_flag))
    elif not all(lo < pos < hi for pos in sets):
        problems.append(
            "%s() sets %s outside its critical section -- the test-and-set must "
            "be bracketed by %s()/%s() to be atomic on PIC32MZ."
            % (START_BEGIN, start_flag, TASK_ENTER, TASK_EXIT))
    elif not _reads_name_in(begin, start_flag, lo, hi):
        problems.append(
            "%s() sets %s inside its critical section but never READS it there, "
            "so it is a plain set, not a test-and-set: two session starts on the "
            "two SCPI transports would both be granted the claim (#850). This "
            "establishes that the flag is read in the same section as the set -- "
            "NOT that the read gates the grant, which regex cannot show."
            % (START_BEGIN, start_flag))

    # The interlock itself. Each Begin must READ the other flag inside its own
    # single critical section -- inside, because a cross-test read before
    # taskENTER_CRITICAL is a TOCTOU: the other transport can take its claim
    # between the read and the take, which is the whole failure #977 closes.
    if cfg_flag is None:
        problems.append(
            "the #977 interlock could NOT be checked in either direction: the "
            "config-change flag was not identifiable (see the reason above), so "
            "neither %s() nor %s() could be shown to read it."
            % (CLAIM_BEGIN, START_BEGIN))
        return problems

    # START side: reading the config flag through CLAIM_READER is a legitimate
    # spelling of the same test (it is that flag's public accessor), so the
    # permissive `_reads_in` is correct here and only here.
    if not _reads_in(begin, cfg_flag, lo, hi):
        problems.append(
            "%s() never reads %s inside its critical section, so the #977 "
            "interlock is open in the START direction: a session start is "
            "granted while a SYSTem:MEMory:* or cap-input setter holds the "
            "config-change claim, and both reach PrepareStreamingBuffers, which "
            "re-carves the one streaming pool." % (START_BEGIN, cfg_flag))

    if cfg_begin is None:
        problems.append(
            "%s() not found, so the #977 interlock could not be checked in the "
            "config-change direction." % CLAIM_BEGIN)
        return problems
    clo, chi, why = _single_section("%s()" % CLAIM_BEGIN, cfg_begin)
    if why is not None:
        # Already reported by the config-change half above; do not duplicate
        # the message, only record that this arm could not run.
        problems.append(
            "the #977 interlock could not be checked in the config-change "
            "direction: %s() does not present a single critical section (see "
            "the refusal above)." % CLAIM_BEGIN)
    elif not _reads_name_in(cfg_begin, start_flag, clo, chi):
        problems.append(
            "%s() never reads %s inside its critical section, so the #977 "
            "interlock is open in the config-change direction: a SYST:MEM:AUTO "
            "is granted while a session start is preparing, and re-partitions "
            "the pool that start is installing pointers from."
            % (CLAIM_BEGIN, start_flag))
    return problems


def check_streaming(streaming_text):
    """-> (problems, flag_name_or_None) for the claim primitive itself."""
    text = strip_c_comments(streaming_text)
    flag, why = claim_flag(text)
    if flag is None:
        # #977: the session-start claim and the interlock are still checked --
        # an undiscoverable config flag must not silently disarm them too.
        return [why] + check_interlock(text, None,
                                       function_body(text, CLAIM_BEGIN)), None

    problems = []
    begin = function_body(text, CLAIM_BEGIN)
    end = function_body(text, CLAIM_END)
    for fn, body in ((CLAIM_BEGIN, begin), (CLAIM_END, end)):
        if body is None:
            problems.append(
                "%s() not found in the streaming source -- the SCPI helper "
                "calls it, so this checker cannot confirm the call does "
                "anything." % fn)
    if begin is None or end is None:
        return problems + check_interlock(text, flag, begin), flag

    sets = [pos for pos, kind in _assignments(begin, flag) if kind == "set"]
    if not sets:
        problems.append(
            "%s() never sets %s to a non-zero value, so it can report a claim "
            "it did not take. Every SYSTem:MEMory:* setter would then run "
            "unclaimed while the routing check above still passes (#864)."
            % (CLAIM_BEGIN, flag))
    else:
        enters = _call_positions(begin, TASK_ENTER)
        exits = _call_positions(begin, TASK_EXIT)
        if not enters or not exits:
            problems.append(
                "%s() sets %s with no %s()/%s() around it. Granting the claim "
                "is a read-modify-write (test the flag, then set it), which is "
                "NOT atomic on PIC32MZ, so both SCPI transports can be granted "
                "it at once (docs/MCU_REFERENCE.md, Atomicity & Concurrency Rules)."
                % (CLAIM_BEGIN, flag, TASK_ENTER, TASK_EXIT))
        elif len(enters) != 1 or len(exits) != 1:
            problems.append(_ONE_REGION % {
                "who": "%s()" % CLAIM_BEGIN,
                "item": "assignment",
                "counts": "%d %s() and %d %s()"
                          % (len(enters), TASK_ENTER, len(exits), TASK_EXIT),
                "defeat": "a %s assigned between two disjoint sections is "
                          "inside first-enter..last-exit and inside neither "
                          "section" % flag,
            })
        elif not all(enters[0] < pos < exits[0] for pos in sets):
            problems.append(
                "%s() sets %s outside its critical section -- the test-and-set "
                "must be bracketed by %s()/%s() to be atomic on PIC32MZ."
                % (CLAIM_BEGIN, flag, TASK_ENTER, TASK_EXIT))
        elif not _reads_in(begin, flag, enters[0], exits[0]):
            problems.append(
                "%s() sets %s inside its critical section but never READS it "
                "there, so it is a plain set, not a test-and-set: drop the "
                "`else if (%s != 0)` arm and two transports are both granted "
                "the claim while every other check here still passes (#864, "
                "codex leg on PR #894). This establishes that the flag is read "
                "in the same section as the set -- NOT that the read is what "
                "gates the grant, which regex cannot show."
                % (CLAIM_BEGIN, flag, flag))

    if not any(kind == "clear" for _, kind in _assignments(end, flag)):
        problems.append(
            "%s() never clears %s, so a claim once taken is never released and "
            "every later SYSTem:MEMory:* setter is refused for the rest of the "
            "session (#864)." % (CLAIM_END, flag))

    problems += check_interlock(text, flag, begin)

    return problems, flag


# --------------------------------------------------------------------------
# self-test: pure, no source tree needed
# --------------------------------------------------------------------------
_GOOD = '''
static scpi_result_t SCPI_MemRunClaimed(scpi_t *c, scpi_result_t (*b)(scpi_t *),
                                        const char *what) {
    StreamingCfgClaim claim = Streaming_BeginConfigChange();
    if (claim != STREAM_CFG_CLAIM_OK) { return SCPI_RejectCfgClaim(c, 1, what); }
    scpi_result_t r = b(c);
    Streaming_EndConfigChange();
    return r;
}
static scpi_result_t SCPI_SetMemSdBuf(scpi_t * context) {
    return SCPI_MemRunClaimed(context, SCPI_SetMemSdBufClaimed, "a{b}c");
}
static scpi_result_t SCPI_SetMemWifiBuf(scpi_t * context) {
    return SCPI_MemRunClaimed(context, SCPI_SetMemWifiBufClaimed, "x");
}
static scpi_result_t SCPI_GetMemFree(scpi_t * context) { return SCPI_RES_OK; }
static const scpi_command_t scpi_commands[] = {
    {.pattern = "SYSTem:MEMory:SD:BUFfer", .callback = SCPI_SetMemSdBuf,},
    {.pattern = "SYSTem:MEMory:WIFI:BUFfer", .callback = SCPI_SetMemWifiBuf,},
    {.pattern = "SYSTem:MEMory:FREE?", .callback = SCPI_GetMemFree,},
};
'''

# The claim primitive, as it must look for the SCPI-side routing to mean
# anything. `gSomethingElse` is the declarator anchor's mutation target: its
# `= false` is the only identifier-shaped initialiser in this fixture, so it
# is the only one a loosened anchor could offer as a flag name. It changes no
# verdict here. Widen `[\w\s\*]` so it can cross `=` (to `[\w\s\*=]`, `.`, or
# `[^;]`) and FOUR arms go red -- the two `init_reader` arms and the two
# pre-existing `gutted_reader` arms below; delete this line and all four go
# green on that same broken anchor (60/60). The line therefore outlives any
# one of them: do NOT retire it alongside one. Retiring either pair AND
# deleting this line takes a caught loosening (56/58) to a silent pass
# (58/58).
#
# It buys that and no more: a class widened all the way to `[\s\S]` is caught
# with or without this line, and loosening the anchor's `\b` or its
# `(?:=|;|\[)` tail is caught by nothing. (#899; limits tracked in #896.)
#
# #977 added the session-start half. The fixture carries BOTH claims and the
# interlock between them, because that is now what "compliant" means: a
# _GOOD_STREAM with only the config claim would fail the checker it is supposed
# to be the clean baseline for.
_GOOD_STREAM = '''
static volatile bool gSomethingElse = false;
static volatile uint32_t gCfgChangeBusy = 0u;
static volatile uint32_t gSessionStartBusy = 0u;

StreamingCfgClaim Streaming_BeginConfigChange(void) {
    StreamingRuntimeConfig* pCfg = BoardRunTimeConfig_Get(BOARDRUNTIME_STREAM);
    StreamingCfgClaim result;
    bool startHeld = false;
    taskENTER_CRITICAL();
    if (pCfg->IsEnabled || pCfg->Running) {
        result = STREAM_CFG_CLAIM_STREAMING;
    } else if (gCfgChangeBusy != 0u) {
        result = STREAM_CFG_CLAIM_BUSY;
    } else if (gSessionStartBusy != 0u) {
        result = STREAM_CFG_CLAIM_BUSY;
        startHeld = true;
    } else {
        gCfgChangeBusy = 1u;
        result = STREAM_CFG_CLAIM_OK;
    }
    taskEXIT_CRITICAL();
    if (startHeld) { LOG_E("refused"); }
    return result;
}

void Streaming_EndConfigChange(void) {
    gCfgChangeBusy = 0u;
}

bool Streaming_ConfigChangeInProgress(void) {
    return (gCfgChangeBusy != 0u);
}

StreamingStartClaim Streaming_BeginSessionStart(void) {
    StreamingStartClaim result;
    bool cfgHeld = false;
    taskENTER_CRITICAL();
    if (gSessionStartBusy != 0u) {
        result = STREAM_START_CLAIM_BUSY;
    } else if (gCfgChangeBusy != 0u) {
        result = STREAM_START_CLAIM_BUSY;
        cfgHeld = true;
    } else {
        gSessionStartBusy = 1u;
        result = STREAM_START_CLAIM_OK;
    }
    taskEXIT_CRITICAL();
    if (cfgHeld) { LOG_E("refused"); }
    return result;
}

void Streaming_EndSessionStart(void) {
    gSessionStartBusy = 0u;
}
'''

_CHECKS = []


def _ck(name, got, want):
    ok = got == want
    _CHECKS.append(ok)
    if not ok:
        print("  self-test FAIL: %s: got %r want %r" % (name, got, want))


def self_test():
    # Floor lowered so the two-setter fixture is a valid sample.
    global KNOWN_MEM_SETTERS
    saved, KNOWN_MEM_SETTERS = KNOWN_MEM_SETTERS, 2
    try:
        probs, n = check(_GOOD)
        _ck("a compliant table is clean", probs, [])
        _ck("queries are not counted as setters", n, 2)

        # A brace inside a string literal must not truncate the body scan.
        _ck("literal braces do not unbalance the scan",
            CLAIM_HELPER in function_body(_GOOD, "SCPI_SetMemSdBuf"), True)

        # A CALL in a condition must not be mistaken for a definition. Without
        # the line anchor the signature regex backtracks across the inner `)`
        # and matches `if (helper_probe(x)) {`, returning that block as the
        # function body -- which would then be searched for the claim call.
        called = _GOOD + """
static scpi_result_t decoy(scpi_t * c) {
    if (helper_probe(c)) { return SCPI_RES_ERR; }
    return SCPI_RES_OK;
}
"""
        _ck("a call inside a condition is not read as a definition",
            function_body(called, "helper_probe"), None)

        # (1) one command routed off the helper
        off = _GOOD.replace(
            'return SCPI_MemRunClaimed(context, SCPI_SetMemWifiBufClaimed, "x");',
            'return SCPI_SetMemWifiBufClaimed(context);')
        probs, _ = check(off)
        _ck("a setter routed off the helper is caught",
            any("does not go through" in p for p in probs), True)

        # (2) the helper gutted -- every setter still routes through it
        gutted = _GOOD.replace(
            "StreamingCfgClaim claim = Streaming_BeginConfigChange();",
            "StreamingCfgClaim claim = STREAM_CFG_CLAIM_OK;")
        probs, _ = check(gutted)
        _ck("a gutted helper is caught (routing alone proves nothing)",
            any(CLAIM_BEGIN in p for p in probs), True)
        _ck("...and the gutted case is NOT reported as a routing problem",
            any("does not go through" in p for p in probs), False)

        # --- pre-merge audit on PR #863: two ways this reported a false OK ---

        # (1) reversed designated-initializer order. Legal C, and the old
        # parser required .pattern to be immediately followed by .callback, so
        # a setter written this way was never examined -- while the summary
        # line still claimed to have checked them all.
        rev = _GOOD.replace(
            '{.pattern = "SYSTem:MEMory:WIFI:BUFfer", .callback = SCPI_SetMemWifiBuf,},',
            '{.callback = SCPI_SetMemFoo, .pattern = "SYSTem:MEMory:FOO",},'
        ).replace(
            'static scpi_result_t SCPI_SetMemWifiBuf(scpi_t * context) {\n'
            '    return SCPI_MemRunClaimed(context, SCPI_SetMemWifiBufClaimed, "x");\n}',
            'static scpi_result_t SCPI_SetMemFoo(scpi_t * context) {\n'
            '    return SCPI_SetMemFooClaimed(context);\n}')
        probs, n = check(rev)
        _ck("a reversed-order registration is still examined", n, 2)
        _ck("...and an unclaimed one is caught",
            any("SYSTem:MEMory:FOO" in p for p in probs), True)

        # (2) a body that only MENTIONS the helper must not count as calling it.
        mention = _GOOD.replace(
            'return SCPI_MemRunClaimed(context, SCPI_SetMemSdBufClaimed, "a{b}c");',
            'const char *marker = "SCPI_MemRunClaimed"; (void)marker;\n'
            '    return SCPI_SetMemSdBufClaimed(context);')
        probs, _ = check(mention)
        _ck("a string mention is not accepted as a call",
            any("does not go through" in p for p in probs), True)

        # (3) an ABBREVIATED registration is a legal command and must be
        # examined. libscpi accepts each node full or truncated at its first
        # lowercase letter, independently, so `SYSTem:MEMory:` has four legal
        # spellings; matching only the full one meant an abbreviated setter was
        # never checked while the summary claimed otherwise.
        _ck("SYSTem spells full and short", _node_spellings("SYSTem"),
            ("SYSTem", "SYST"))
        _ck("an all-caps node has exactly ONE spelling",
            _node_spellings("APPLY"), ("APPLY",))
        _ck("all four prefix spellings are accepted", set(MEM_PREFIXES),
            {"SYSTem:MEMory:", "SYST:MEMory:", "SYSTem:MEM:", "SYST:MEM:"})

        abbrev = _GOOD.replace(
            '{.pattern = "SYSTem:MEMory:WIFI:BUFfer", .callback = SCPI_SetMemWifiBuf,},',
            '{.pattern = "SYST:MEM:FOO", .callback = SCPI_SetMemFoo,},'
        ).replace(
            'static scpi_result_t SCPI_SetMemWifiBuf(scpi_t * context) {\n'
            '    return SCPI_MemRunClaimed(context, SCPI_SetMemWifiBufClaimed, "x");\n}',
            'static scpi_result_t SCPI_SetMemFoo(scpi_t * context) {\n'
            '    return SCPI_SetMemFooClaimed(context);\n}')
        probs, n = check(abbrev)
        _ck("an abbreviated registration is still examined", n, 2)
        _ck("...and an unclaimed abbreviated setter is caught",
            any("SYST:MEM:FOO" in p for p in probs), True)

        # queries are still excluded whichever spelling they use
        _ck("an abbreviated QUERY is not counted as a setter",
            mem_setters([("SYST:MEM:FREE?", "SCPI_GetMemFree")]), [])

        # a row with a .pattern it cannot read is REPORTED, not skipped
        bad_row = _GOOD.replace(
            '{.pattern = "SYSTem:MEMory:SD:BUFfer", .callback = SCPI_SetMemSdBuf,},',
            '{.pattern = "SYSTem:MEMory:SD:BUFfer", .callback = ,},')
        probs, _ = check(bad_row)
        _ck("an unreadable table row fails rather than being skipped",
            any("could not read" in p for p in probs), True)

        # a commented-out registration must not be counted
        commented = _GOOD.replace(
            '    {.pattern = "SYSTem:MEMory:SD:BUFfer", .callback = SCPI_SetMemSdBuf,},',
            '//  {.pattern = "SYSTem:MEMory:SD:BUFfer", .callback = SCPI_SetMemSdBuf,},')
        _, n2 = check(commented)
        _ck("a commented-out registration is not counted", n2, 1)

        # ---- #864(2): the claim must be HELD ACROSS the dispatch ---------
        # Presence of Begin and End is what the checks above establish, and a
        # helper rewritten take -> release -> dispatch satisfies every one of
        # them while holding the claim across nothing.
        reordered = _GOOD.replace(
            "    scpi_result_t r = b(c);\n    Streaming_EndConfigChange();",
            "    Streaming_EndConfigChange();\n    scpi_result_t r = b(c);")
        assert reordered != _GOOD
        probs, _ = check(reordered)
        _ck("a dispatch AFTER the release is caught",
            any("outside the claim" in p for p in probs), True)

        nodispatch = _GOOD.replace("    scpi_result_t r = b(c);",
                                   "    scpi_result_t r = SCPI_RES_OK;")
        probs, _ = check(nodispatch)
        _ck("a helper that claims around no work at all is caught",
            any("no work at all" in p for p in probs), True)

        # ...and the property is UNVERIFIABLE rather than satisfied when the
        # dispatch target cannot be located. Refusing is the point: a silent
        # skip here is exactly the false OK this file exists to prevent.
        noparam = nodispatch.replace(
            "scpi_result_t (*b)(scpi_t *),\n", "")
        probs, _ = check(noparam)
        _ck("an unlocatable dispatch is refused, not skipped",
            any("UNVERIFIED" in p for p in probs), True)

        # a rename must be FOLLOWED, not reported
        renamed = _GOOD.replace("(*b)(scpi_t *)", "(*run)(scpi_t *)").replace(
            "scpi_result_t r = b(c);", "scpi_result_t r = run(c);")
        probs, _ = check(renamed)
        _ck("renaming the dispatch parameter is followed, not flagged",
            probs, [])

        # ...and the released-before-taken shape gets its own message, so the
        # log says which way round it went rather than only that it is wrong.
        swapped = _GOOD.replace(
            "    StreamingCfgClaim claim = Streaming_BeginConfigChange();",
            "    Streaming_EndConfigChange();").replace(
            "    Streaming_EndConfigChange();\n    return r;",
            "    StreamingCfgClaim claim = Streaming_BeginConfigChange();\n    return r;")
        assert swapped != _GOOD
        probs, _ = check(swapped)
        _ck("a claim released before it is taken is named as such",
            any("released before it is taken" in p for p in probs), True)

        # Round 1 of PR #894's review, found independently by the codex leg
        # and by Qodo /agentic_review: "between the FIRST Begin and the LAST
        # End" is not "inside a claim" -- the gap between two claim pairs
        # satisfies it while holding nothing.
        twopair = _GOOD.replace(
            "    scpi_result_t r = b(c);\n    Streaming_EndConfigChange();\n    return r;",
            "    Streaming_EndConfigChange();\n"
            "    scpi_result_t r = b(c);\n"
            "    claim = Streaming_BeginConfigChange();\n"
            "    Streaming_EndConfigChange();\n    return r;")
        assert twopair != _GOOD
        probs, _ = check(twopair)
        _ck("a dispatch between two claim pairs is refused, not passed",
            any("2 Streaming_BeginConfigChange() and 2 "
                "Streaming_EndConfigChange()" in p for p in probs), True)
        # Round 4: the counts are built at the call site, so the arm above
        # passes a gutted _ONE_REGION template. Round 2 rewrote that template
        # BECAUSE it was false and removed the only assertion on its wording
        # in the same edit -- the "a fix becomes the next finding" shape.
        _ck("...and the refusal says WHY, not just the counts",
            any("Positional containment is only decidable" in p
                for p in probs), True)

        # Round 2 (opus hunter): CALLING Begin is not USING it. A helper that
        # throws the verdict away refuses nothing, and `(void)` on an
        # otherwise-unused result is an ordinary way to silence a warning.
        discarded = _GOOD.replace(
            "    StreamingCfgClaim claim = Streaming_BeginConfigChange();\n"
            "    if (claim != STREAM_CFG_CLAIM_OK) { return SCPI_RejectCfgClaim(c, 1, what); }",
            "    (void)Streaming_BeginConfigChange();")
        assert discarded != _GOOD
        probs, _ = check(discarded)
        _ck("a helper that DISCARDS the claim verdict is caught",
            any("discards the result" in p for p in probs), True)
        # Round 3 (Qodo): STORING the verdict is not INSPECTING it. Delete
        # only the rejection arm and the assignment remains, so round 2's
        # check still passed while the helper dispatched after a BUSY verdict.
        stored = _GOOD.replace(
            "    if (claim != STREAM_CFG_CLAIM_OK) { return SCPI_RejectCfgClaim(c, 1, what); }\n",
            "")
        assert stored != _GOOD
        probs, _ = check(stored)
        _ck("a verdict stored but never read is caught",
            any("never inspects the verdict" in p for p in probs), True)

        # Round 4 (opus verifier): a "read" whose own prefix throws the value
        # away is not an inspection -- and this check's OWN message used to
        # advertise catching `(void)claim;`, which it did not.
        voided = stored.replace(
            "    scpi_result_t r = b(c);",
            "    (void)claim;\n    scpi_result_t r = b(c);")
        assert voided != stored
        _ck("`(void)claim;` is not an inspection",
            any("never inspects the verdict" in p for p in check(voided)[0]), True)
        for label, mutated in (
                ("a non-void cast still discards",
                 _GOOD.replace("StreamingCfgClaim claim = Streaming_BeginConfigChange();",
                               "(int)Streaming_BeginConfigChange();")),
                ("a labelled statement still discards",
                 _GOOD.replace("StreamingCfgClaim claim = Streaming_BeginConfigChange();",
                               "retry: Streaming_BeginConfigChange();")),
                ("a for-init clause still discards",
                 _GOOD.replace("StreamingCfgClaim claim = Streaming_BeginConfigChange();",
                               "for (Streaming_BeginConfigChange(); 0; ) { }"))):
            assert mutated != _GOOD, label
            _ck(label, any("discards the result" in p
                           for p in check(mutated)[0]), True)

        inline = _GOOD.replace(
            "    StreamingCfgClaim claim = Streaming_BeginConfigChange();\n"
            "    if (claim != STREAM_CFG_CLAIM_OK) {",
            "    if (Streaming_BeginConfigChange() != STREAM_CFG_CLAIM_OK) {")
        assert inline != _GOOD
        _ck("...and a helper that tests it inline is not accused",
            check(inline)[0], [])

        # ---- #864(1) residual: the claim PRIMITIVE, in streaming.c --------
        sprobs, sflag = check_streaming(_GOOD_STREAM)
        _ck("a compliant claim primitive is clean", sprobs, [])
        _ck("the flag is discovered from the reader, not hard-coded",
            sflag, "gCfgChangeBusy")
        # The declarator anchor in `claim_flag`. Deciding it needs a reader
        # that MENTIONS the initialiser's token: candidates come from the
        # READER's identifiers, and `_GOOD_STREAM`'s reader never says
        # `false`, so it is never offered there and the anchor never runs on
        # it. Written against `_GOOD_STREAM` this arm computed
        # `check_streaming(_GOOD_STREAM)[1]` a second time -- dominated by the
        # arm above it, and green with the anchor loosened to `[\w\s\*=]`
        # (#899). This variant's reader mentions `false`, so
        # `gSomethingElse`'s initialiser is a candidate the anchor must reject:
        # loosening the anchor makes `claim_flag` see TWO flags and refuse.
        init_reader = _GOOD_STREAM.replace(
            "    return (gCfgChangeBusy != 0u);",
            "    if (gCfgChangeBusy == 0u) { return false; }\n"
            "    return true;")
        assert init_reader != _GOOD_STREAM
        _ck("an initialiser is not mistaken for a declarator",
            claim_flag(strip_c_comments(init_reader)),
            ("gCfgChangeBusy", None))
        _ck("...and that reader is otherwise a compliant primitive",
            check_streaming(init_reader)[0], [])

        # THE case the CI trigger existed for and could not detect: a Begin
        # that hands out STREAM_CFG_CLAIM_OK without taking anything. Every
        # SCPI-side check still passes on it.
        _ck("a Begin that never sets the flag is caught",
            any("never sets" in p for p in
                check_streaming(_GOOD_STREAM.replace(
                    "        gCfgChangeBusy = 1u;\n", ""))[0]), True)
        _ck("a Begin that only COMPARES the flag is not credited with a write",
            any("never sets" in p for p in
                check_streaming(_GOOD_STREAM.replace(
                    "        gCfgChangeBusy = 1u;",
                    "        (void)(gCfgChangeBusy == 1u);"))[0]), True)
        _ck("an End that never clears the flag is caught",
            any("never clears" in p for p in
                check_streaming(_GOOD_STREAM.replace(
                    "    gCfgChangeBusy = 0u;\n}", "}"))[0]), True)

        # The test-and-set is a read-modify-write, not atomic on PIC32MZ.
        outside = _GOOD_STREAM.replace(
            "    taskENTER_CRITICAL();",
            "    gCfgChangeBusy = 1u;\n    taskENTER_CRITICAL();").replace(
            "        gCfgChangeBusy = 1u;\n        result = STREAM_CFG_CLAIM_OK;",
            "        result = STREAM_CFG_CLAIM_OK;")
        assert outside != _GOOD_STREAM
        _ck("a set outside the critical section is caught",
            any("outside its critical section" in p
                for p in check_streaming(outside)[0]), True)
        _ck("...and a set with no critical section at all is caught",
            any("read-modify-write" in p for p in check_streaming(
                outside.replace("    taskENTER_CRITICAL();\n", "").replace(
                    "    taskEXIT_CRITICAL();\n", ""))[0]), True)

        # Same round-1 class on the streaming side, and its twin: a set
        # between two disjoint critical sections is inside neither.
        split_cs = _GOOD_STREAM.replace(
            "        gCfgChangeBusy = 1u;\n        result = STREAM_CFG_CLAIM_OK;\n"
            "    }\n    taskEXIT_CRITICAL();",
            "        result = STREAM_CFG_CLAIM_OK;\n"
            "    }\n    taskEXIT_CRITICAL();\n"
            "    gCfgChangeBusy = 1u;\n"
            "    taskENTER_CRITICAL();\n    (void)pCfg;\n    taskEXIT_CRITICAL();")
        assert split_cs != _GOOD_STREAM
        _ck("a set between two disjoint critical sections is refused",
            any("2 taskENTER_CRITICAL() and 2 taskEXIT_CRITICAL()" in p
                for p in check_streaming(split_cs)[0]), True)

        # A plain SET is not a test-and-set. Dropping the busy arm leaves a
        # Begin that grants unconditionally -- every other check still passes.
        plainset = _GOOD_STREAM.replace(
            "    } else if (gCfgChangeBusy != 0u) {\n"
            "        result = STREAM_CFG_CLAIM_BUSY;\n", "    ")
        assert plainset != _GOOD_STREAM
        _ck("a Begin that sets but never READS the flag is caught",
            any("never READS it there" in p
                for p in check_streaming(plainset)[0]), True)

        # Round 2 (opus hunter): three forms of CORRECT C that used to red the
        # gate. A false red is how a gate gets deleted, and the `|=` message
        # was the worst of them -- it said "never sets" about code that sets.
        qual = _GOOD_STREAM.replace("static volatile uint32_t gCfgChangeBusy",
                                    "volatile static uint32_t gCfgChangeBusy")
        assert qual != _GOOD_STREAM
        _ck("`volatile static` (legal qualifier order) is accepted",
            check_streaming(qual)[0], [])
        # Round 3 (Qodo): the OPERATOR matters, not only the value. `&= 1u`
        # and `|= 0u` leave the flag exactly as it was; crediting them by RHS
        # alone certified a Begin that never acquires and an End that never
        # releases.
        _ck("`&= 1u` is not taking the claim (it preserves)",
            any("never sets" in p for p in check_streaming(_GOOD_STREAM.replace(
                "        gCfgChangeBusy = 1u;",
                "        gCfgChangeBusy &= 1u;"))[0]), True)
        _ck("`|= 0u` is not releasing it (it is a no-op)",
            any("never clears" in p for p in check_streaming(_GOOD_STREAM.replace(
                "    gCfgChangeBusy = 0u;\n}",
                "    gCfgChangeBusy |= 0u;\n}"))[0]), True)
        # ...and a parenthesised zero is still zero, both ways round.
        _ck("`= (0u)` is not a set",
            any("never sets" in p for p in check_streaming(_GOOD_STREAM.replace(
                "        gCfgChangeBusy = 1u;",
                "        gCfgChangeBusy = (0u);"))[0]), True)
        parenclr = _GOOD_STREAM.replace("    gCfgChangeBusy = 0u;\n}",
                                        "    gCfgChangeBusy = (0u);\n}")
        assert parenclr != _GOOD_STREAM
        _ck("`= (0u)` IS a clear", check_streaming(parenclr)[0], [])
        _ck("a wrapping paren is peeled, an early-closing one is not",
            (_unparen("(0u)"), _unparen("(a) + (0u)")), ("0u", "(a) + (0u)"))

        orset = _GOOD_STREAM.replace("        gCfgChangeBusy = 1u;",
                                     "        gCfgChangeBusy |= 1u;")
        assert orset != _GOOD_STREAM
        _ck("`|= 1u` counts as taking the claim", check_streaming(orset)[0], [])
        andclr = _GOOD_STREAM.replace("    gCfgChangeBusy = 0u;\n}",
                                      "    gCfgChangeBusy &= 0u;\n}")
        assert andclr != _GOOD_STREAM
        _ck("`&= 0u` counts as releasing it", check_streaming(andclr)[0], [])

        # V1 (opus verifier): testing through the public accessor is the
        # same test-and-set, and is if anything the better shape. Rejecting it
        # told the author their test-and-set was "a plain set" -- false.
        via_accessor = _GOOD_STREAM.replace(
            "    } else if (gCfgChangeBusy != 0u) {",
            "    } else if (Streaming_ConfigChangeInProgress()) {")
        assert via_accessor != _GOOD_STREAM
        _ck("a test-and-set through the accessor is accepted as a read",
            check_streaming(via_accessor)[0], [])

        _ck("renaming the claim flag is followed, not flagged",
            check_streaming(_GOOD_STREAM.replace(
                "gCfgChangeBusy", "gClaimHeld"))[0], [])

        # vacuity, streaming side: a reader gutted to a constant leaves the
        # flag undiscoverable, and that must FAIL rather than quietly check
        # nothing.
        gutted_reader = check_streaming(_GOOD_STREAM.replace(
            "    return (gCfgChangeBusy != 0u);", "    return false;"))
        _ck("an unreadable claim flag fails rather than passing",
            any("needs exactly one" in p for p in gutted_reader[0]), True)
        _ck("...and reports no flag", gutted_reader[1], None)
        _ck("a streaming source with no claim primitive at all fails",
            len(check_streaming("int main(void) { return 0; }")[0]) >= 1, True)

        # A `bool` flag cleared with `false` is correct code and must stay
        # clean -- the checker requires only `static volatile`, so the type is
        # not its business and a false red here would be its own defect.
        as_bool = (_GOOD_STREAM
                   .replace("static volatile uint32_t gCfgChangeBusy = 0u;",
                            "static volatile bool gCfgChangeBusy = false;")
                   .replace("        gCfgChangeBusy = 1u;",
                            "        gCfgChangeBusy = true;")
                   .replace("    gCfgChangeBusy = 0u;\n}",
                            "    gCfgChangeBusy = false;\n}")
                   .replace("    return (gCfgChangeBusy != 0u);",
                            "    return gCfgChangeBusy;"))
        assert as_bool != _GOOD_STREAM
        _ck("a bool-typed claim flag cleared with `false` is clean",
            check_streaming(as_bool)[0], [])

        # ---- #977: the INTERLOCK, and the session-start claim itself -------
        # Two conditions in two functions is the arrangement that produced the
        # gap, so each arm is mutated on its own: deleting EITHER direction
        # must go red, or the tree can be returned to the pre-#977 state one
        # half at a time with the gate still green.
        cfg_open = _GOOD_STREAM.replace(
            "    } else if (gSessionStartBusy != 0u) {\n"
            "        result = STREAM_CFG_CLAIM_BUSY;\n"
            "        startHeld = true;\n", "    ")
        assert cfg_open != _GOOD_STREAM
        _ck("the interlock deleted on the CONFIG side is caught",
            any("interlock is open in the config-change direction" in p
                for p in check_streaming(cfg_open)[0]), True)

        start_open = _GOOD_STREAM.replace(
            "    } else if (gCfgChangeBusy != 0u) {\n"
            "        result = STREAM_START_CLAIM_BUSY;\n"
            "        cfgHeld = true;\n", "    ")
        assert start_open != _GOOD_STREAM
        _ck("the interlock deleted on the START side is caught",
            any("interlock is open in the START direction" in p
                for p in check_streaming(start_open)[0]), True)

        # ...and the two arms are INDEPENDENT: neither mutation reports the
        # other's message. Without this, one arm could be gating both and the
        # pair above would pass with half the property implemented.
        _ck("...and the config-side mutation does not claim the START side",
            any("interlock is open in the START direction" in p
                for p in check_streaming(cfg_open)[0]), False)
        _ck("...and the START-side mutation does not claim the config side",
            any("interlock is open in the config-change direction" in p
                for p in check_streaming(start_open)[0]), False)

        # A cross-test HOISTED OUT of the critical section is the TOCTOU the
        # interlock exists to prevent: the other transport can take its claim
        # between the read and the take. Textually the flag is still read in
        # the function, so a checker that only asked "is it mentioned" passes.
        hoisted = _GOOD_STREAM.replace(
            "    StreamingStartClaim result;\n"
            "    bool cfgHeld = false;\n"
            "    taskENTER_CRITICAL();",
            "    StreamingStartClaim result;\n"
            "    bool cfgHeld = (gCfgChangeBusy != 0u);\n"
            "    taskENTER_CRITICAL();").replace(
            "    } else if (gCfgChangeBusy != 0u) {\n"
            "        result = STREAM_START_CLAIM_BUSY;\n"
            "        cfgHeld = true;\n", "    ")
        assert hoisted != _GOOD_STREAM
        _ck("a cross-test hoisted out of the critical section is caught",
            any("interlock is open in the START direction" in p
                for p in check_streaming(hoisted)[0]), True)

        # The START claim must be a test-and-set in its own right. Every arm
        # below was previously unchecked ANYWHERE -- the gate ran on
        # streaming.c and asserted nothing about this half of the file.
        _ck("a session-start Begin that never sets its flag is caught",
            any("never sets gSessionStartBusy" in p
                for p in check_streaming(_GOOD_STREAM.replace(
                    "        gSessionStartBusy = 1u;\n", ""))[0]), True)
        _ck("a session-start End that never clears is caught",
            any("never clears gSessionStartBusy" in p
                for p in check_streaming(_GOOD_STREAM.replace(
                    "    gSessionStartBusy = 0u;\n}", "}"))[0]), True)
        start_plainset = _GOOD_STREAM.replace(
            "    if (gSessionStartBusy != 0u) {\n"
            "        result = STREAM_START_CLAIM_BUSY;\n"
            "    } else if", "    if (0) {\n    } else if")
        assert start_plainset != _GOOD_STREAM
        _ck("a session-start Begin that sets but never READS its flag is caught",
            any("never READS it there" in p
                for p in check_streaming(start_plainset)[0]), True)
        # ...and the accessor must NOT stand in for that read: it answers a
        # question about the OTHER claim. `_reads_name_in` is what makes this
        # arm differ from the config side's, where the accessor IS legitimate.
        start_via_reader = start_plainset.replace(
            "    if (0) {\n    } else if (gCfgChangeBusy != 0u) {",
            "    if (Streaming_ConfigChangeInProgress()) {")
        assert start_via_reader != start_plainset
        _ck("the config accessor is not credited as reading the START flag",
            any("never READS it there" in p
                for p in check_streaming(start_via_reader)[0]), True)

        start_outside = _GOOD_STREAM.replace(
            "    bool cfgHeld = false;\n    taskENTER_CRITICAL();",
            "    bool cfgHeld = false;\n    gSessionStartBusy = 1u;\n"
            "    taskENTER_CRITICAL();").replace(
            "        gSessionStartBusy = 1u;\n"
            "        result = STREAM_START_CLAIM_OK;",
            "        result = STREAM_START_CLAIM_OK;")
        assert start_outside != _GOOD_STREAM
        _ck("a session-start set outside the critical section is caught",
            any("sets gSessionStartBusy outside its critical section" in p
                for p in check_streaming(start_outside)[0]), True)

        start_split = _GOOD_STREAM.replace(
            "        gSessionStartBusy = 1u;\n"
            "        result = STREAM_START_CLAIM_OK;\n"
            "    }\n    taskEXIT_CRITICAL();\n    if (cfgHeld)",
            "        result = STREAM_START_CLAIM_OK;\n"
            "    }\n    taskEXIT_CRITICAL();\n"
            "    gSessionStartBusy = 1u;\n"
            "    taskENTER_CRITICAL();\n    (void)result;\n"
            "    taskEXIT_CRITICAL();\n    if (cfgHeld)")
        assert start_split != _GOOD_STREAM
        _ck("a session-start set between two disjoint sections is refused",
            any("Streaming_BeginSessionStart() contains 2 taskENTER_CRITICAL() "
                "and 2 taskEXIT_CRITICAL()" in p
                for p in check_streaming(start_split)[0]), True)

        # Vacuity, the #977 half: a missing session-start claim must FAIL
        # rather than leave the interlock unchecked. This is the shape the
        # whole file exists to refuse -- a gate that runs and establishes
        # nothing -- and it is why the checker refuses instead of skipping.
        no_start = _GOOD_STREAM[:_GOOD_STREAM.index(
            "StreamingStartClaim Streaming_BeginSessionStart(void) {")]
        _ck("a streaming source with no session-start claim fails",
            any("not found in the streaming source" in p and START_BEGIN in p
                for p in check_streaming(no_start)[0]), True)
        no_end = _GOOD_STREAM.replace(
            "void Streaming_EndSessionStart(void) {\n"
            "    gSessionStartBusy = 0u;\n}\n", "")
        assert no_end != _GOOD_STREAM
        _ck("a session-start claim with no release at all fails",
            any(START_END in p and "never released" in p
                for p in check_streaming(no_end)[0]), True)

        # An AMBIGUOUS session-start flag is refused, not guessed at -- the
        # same stance `claim_flag` takes, and for the same reason.
        ambiguous = _GOOD_STREAM.replace(
            "    bool cfgHeld = false;\n    taskENTER_CRITICAL();",
            "    bool cfgHeld = false;\n    (void)gSomethingElse;\n"
            "    taskENTER_CRITICAL();")
        assert ambiguous != _GOOD_STREAM
        _ck("two candidate session-start flags are refused, not guessed",
            any("needs exactly one to follow the session-start claim" in p
                for p in check_streaming(ambiguous)[0]), True)

        # A rename of EITHER flag must be followed, not reported.
        _ck("renaming the session-start flag is followed, not flagged",
            check_streaming(_GOOD_STREAM.replace(
                "gSessionStartBusy", "gStartHeld"))[0], [])

        # And the #977 checks must still run when the CONFIG flag is
        # undiscoverable: an unreadable reader must not silently disarm the
        # interlock half as well. (Both directions become unverifiable, which
        # is reported -- not passed.)
        gutted_both = _GOOD_STREAM.replace(
            "    return (gCfgChangeBusy != 0u);", "    return false;")
        _ck("an unidentifiable config flag reports the interlock as unchecked",
            any("interlock could NOT be checked in either direction" in p
                for p in check_streaming(gutted_both)[0]), True)

        # vacuity: a file with no table must FAIL, not pass quietly
        probs, n3 = check("int main(void) { return 0; }")
        _ck("an unreadable table fails rather than passing", len(probs) >= 1, True)
        _ck("...and reports nothing examined", n3, 0)
        _ck("...with a message naming the refusal",
            any("could not read" in p for p in probs), True)
    finally:
        KNOWN_MEM_SETTERS = saved

    bad = _CHECKS.count(False)
    print("self-test: %d/%d checks passed" % (_CHECKS.count(True), len(_CHECKS)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scpi", default="firmware/src/services/SCPI/SCPIInterface.c",
                    help="path to SCPIInterface.c")
    ap.add_argument("--streaming", default="firmware/src/services/streaming.c",
                    help="path to streaming.c, where the claim primitive lives")
    ap.add_argument("--self-test", action="store_true",
                    help="run the built-in checks and exit (no source needed)")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    for path, flag in ((args.scpi, "--scpi"), (args.streaming, "--streaming")):
        if not os.path.isfile(path):
            sys.exit("error: %r not found (run from the repo root, or pass %s)"
                     % (path, flag))

    def _read(path):
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            return fh.read()

    problems, examined = check(_read(args.scpi))
    streaming_src = _read(args.streaming)
    stream_problems, claimflag = check_streaming(streaming_src)
    problems = problems + stream_problems
    startflag, _ = session_start_flag(strip_c_comments(streaming_src), claimflag)

    if problems:
        print("FAIL: SYSTem:MEMory:* claim-path check (%d examined)" % examined)
        for p in problems:
            print("  - %s" % p)
        return 1
    # States ALL THREE halves, because the CI gate re-runs on streaming.* and a
    # summary naming only the SCPI half would report a pass on a file it had
    # not spoken about. #977 added the third clause for the same reason: a
    # summary that stopped at the config claim would say nothing about the
    # interlock it had just established.
    print("OK: all %d SYSTem:MEMory:* setters take the claim via %s(), which "
          "uses its verdict and holds it across the dispatch; %s() sets and "
          "%s() clears %s, the set inside a single critical section that also "
          "reads it; and the two claims INTERLOCK -- %s() reads %s and %s() "
          "reads %s, each inside its own single critical section (#977)"
          % (examined, CLAIM_HELPER, CLAIM_BEGIN, CLAIM_END, claimflag,
             CLAIM_BEGIN, startflag, START_BEGIN, claimflag))
    return 0


if __name__ == "__main__":
    sys.exit(main())
