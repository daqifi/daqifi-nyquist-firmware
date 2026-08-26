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


# The last non-blank line before a definition whose return type sits on its
# own line. It must LOOK like the tail of a declaration: end in an identifier
# character or a `*`, with its parentheses balanced. `if (`, `if (foo(a)`,
# `while (x &&` all fail it, which is the point -- see `_signature`.
_DECL_TAIL = re.compile(r"[\w\*]\s*$")


def _signature(text, name):
    r"""Match object for a DEFINITION of C function `name`, or None.

    Two layouts, tried in that order, because both are ordinary C and
    accepting only the first reported "could not find that function to check
    it" about a definition that is right there -- a false red, and a false red
    is how a gate gets deleted (#896 section B).

    1. `static scpi_result_t SCPI_Foo(args) {` -- return type and name on one
       line. Anchored to the line start, because the unanchored form
       `\bNAME\s*\([^;{]*\)\s*\{` also matches a CALL inside a condition: in
       `if (foo(a)) {` the parameter span absorbs `a)` and takes the OUTER `)`
       before the brace, so the `if` body is returned as the function body. A
       prototype is excluded either way -- it ends in `;`, which `[^;{]*`
       cannot cross.

    2. Return type on its own line, name at column 0. Making the return type
       merely OPTIONAL in 1 is NOT enough here, and the difference is a real
       hole rather than a nicety: a condition split across lines with the call
       at column 0 --

           if (
       helper_probe(c)) { ... }

       -- puts the CALLED NAME at the start of a line, so the parameter span
       absorbs the inner `)` exactly as in the unanchored case. (Verified by
       running it: the first attempt at this fix returned the `if` body.) So
       layout 2 additionally requires the preceding non-blank line to look
       like the tail of a declaration.
    """
    esc = re.escape(name)
    m = re.search(r"(?m)^[A-Za-z_][\w \t\*]*\b%s\s*\(([^;{]*)\)\s*\{"
                  % esc, text)
    if m:
        return m
    for m in re.finditer(r"(?m)^%s\s*\(([^;{]*)\)\s*\{" % esc, text):
        before = [ln for ln in text[:m.start()].split("\n") if ln.strip()]
        prev = before[-1] if before else ""
        if (_DECL_TAIL.search(prev)
                and prev.count("(") == prev.count(")")):
            return m
    return None


def function_body(text, name):
    """The brace-delimited body of C function `name`, or None if not found.

    `text` must already have comments stripped. String and character literals
    are blanked before brace counting so a brace inside a literal -- e.g. a
    format string -- cannot unbalance the scan.
    """
    sig = _signature(text, name)
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
    # Same matcher as `function_body`, for the same reason: a helper whose
    # return type sits on its own line has a dispatch parameter too, and
    # failing to find it there is the same false red (#896 section B).
    sig = _signature(text, name)
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

# The SECOND and later declarators of one declaration:
# `static volatile uint32_t gOther = 0u, gCfgChangeBusy = 0u;`. The anchor
# above cannot cross the first `=`, so that flag was invisible and the gate
# redded correct C with "reads 0 file-scope static volatile variable(s)"
# (#896 section B).
#
# Added as a SEPARATE alternative rather than by loosening the anchor, which
# would put back the defect #899 armed: the name here must follow a comma
# DIRECTLY, so an initialiser still cannot be offered as a declarator -- in
# `... = false;` there is no comma at all.
#
# `[^;{}()]` bounds the body to ONE declaration and, crucially, keeps it OUT
# OF ANY PARENTHESISED INITIALISER. Allowing parentheses made
# `static volatile uint32_t gOther = FOO(1, gCfgChangeBusy, 2);` offer a MACRO
# ARGUMENT as a declarator, so a flag that was not `static volatile` at all
# was accepted and the gate reported OK -- a silent pass, which is worse than
# anything it was fixing (codex leg, PR #901 round 1). An earlier version of
# this comment reasoned about `... = f(x, y)` and concluded the tail rejected
# it; that is true of the LAST argument only, and every earlier argument is
# followed by a comma, which the tail accepts. Excluding parentheses is what
# actually closes it. Cost: a legitimate declarator list whose initialiser
# calls a macro is no longer matched -- a red, i.e. the safe direction.
_MULTI_DECLARATOR = (r"(?:static\s+volatile|volatile\s+static)\s+"
                     r"[A-Za-z_][^;{}()]*?,\s*\**\s*%s\s*(?:=|;|\[|,)")


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

    Only `=`, `|=` and `&=` are recognised, and `&= ~MASK` is DELIBERATELY not
    a clear. #896 filed that as a false red on idiomatic C; it is not one, and
    fixing it would be a hole. This flag is binary -- `Begin` sets it to `1u`
    and `End` must take it back to zero -- so `flag &= ~SOME_MASK` clears the
    claim only if that mask covers the bit `Begin` set, which needs the mask's
    value. Crediting it regardless would turn a LOUD red into a SILENT pass on
    an `End` that releases nothing, and a silent pass is the failure this file
    exists to prevent. The red stands until an `End` in this tree is actually
    written that way, at which point the mask is a known constant.

    NOT handled, and known: a macro-wrapped write (`CLAIM_CLEAR(gFlag);`) is
    invisible here and would red the gate on correct code. It does not occur
    in this tree. See #896. (The multi-declarator case listed beside it there
    was a different search -- the flag DECLARATION, not its writes -- and is
    handled by `_MULTI_DECLARATOR`.)
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


def _call_end(blanked, open_paren):
    """Index just past the `)` closing the call whose `(` sits at `open_paren`.

    Length of the text if the parentheses do not balance, which makes the
    caller's lookahead match nothing rather than match the wrong thing.
    """
    depth = 0
    for i in range(open_paren, len(blanked)):
        if blanked[i] == "(":
            depth += 1
        elif blanked[i] == ")":
            depth -= 1
            if depth == 0:
                return i + 1
    return len(blanked)


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
# Both label forms C11 6.8.1 gives a statement: an identifier label (which
# covers `default:`, an ordinary identifier) and `case constant-expression :`.
# The `case` half was missing while the docstring below said "a labelled
# statement", so `case 1: Streaming_BeginConfigChange();` was credited as
# USING the verdict -- the enumeration promising a form it did not catch, in
# the function whose whole purpose is to enumerate (#896 (f)).
# `.*` and not `[^:]*`, because a case expression may CONTAIN a colon:
# `case (1 ? 1 : 3):` is a valid label and `[^:]*` stopped at the ternary's
# colon, so the whole prefix never matched (codex leg, PR #901 round 1). The
# greedy form backtracks to the LAST colon, which is the label's.
_LABEL_ONLY = re.compile(r"(?:case\b.*|[A-Za-z_]\w*)\s*:")
_FOR_INIT = re.compile(r"\bfor\s*\($")

# An assignment whose value the DIRECTLY enclosing construct tests or returns:
# `if ((claim = Begin()) != OK)`, `while (...)`, `switch (...)`, `return`.
# That is an inspection at the assignment itself, so the store-and-ignore scan
# below must not run on it -- it used to, and reported "stores but never
# inspects" about a helper that branches on the verdict two characters later
# (#896 (e)). Only a DIRECTLY enclosing construct counts: in
# `if (foo(claim = Begin()))` the value goes to `foo`, not to the `if`.
_TESTED_TAIL = re.compile(r"(?:\b(?:if|while|switch)\s*\(+|\breturn\b\s*\(*)$")

# ...and being inside a test is not enough, because a COMMA OPERATOR throws
# the left operand away before the test ever sees it:
# `if ((claim = Begin(), 1))` tests the constant, not the verdict. The first
# version of this exemption skipped the store scan on that shape, which made
# the checker WEAKER than the version it was fixing -- pre-#901 caught it, by
# accident, through the very mis-scan #896 (e) is about (codex leg, PR #901
# round 1). So the exemption is refused when a comma follows the assignment
# before anything else does; closing parentheses are stepped over, so both
# `(claim = Begin(), 1)` and `(claim = Begin()) , 1` are refused.
_COMMA_FIRST = re.compile(r"[\s)]*,")


def _discards(prefix):
    """True iff a value produced at this statement prefix goes nowhere.

    A labelled statement means BOTH C label forms -- `retry:`/`default:` and
    `case <expr>:`. Enumerating one and describing both is what #896 (f) was.
    """
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

    A call used inline -- in an `if`, in a `return` -- is a use, and so is an
    assignment the enclosing construct immediately tests:
    `if ((claim = Begin()) != STREAM_CFG_CLAIM_OK)` is a correct helper, and
    reporting it as "stores but never inspects" was a FALSE RED on the most
    ordinary refactor of the shape this tree already uses. The store-and-ignore
    scan cannot see those inspections at all -- both of them sit BEFORE the
    `;` it starts scanning from, because that `;` is the one ending the
    `return` inside the if-body (#896 (e)).

    KNOWN LIMITS, both control flow, which this checker does not have (#896):
    passing the verdict to another function counts as a use, so
    `LOG_I("claim=%u", Streaming_BeginConfigChange());` followed by an
    unconditional dispatch passes; and only a DIRECTLY enclosing test counts,
    so `if (foo(claim = Begin()))` falls through to the store scan.
    """
    b = _blank(body)
    for m in re.finditer(r"\b%s\s*\(" % re.escape(name), b):
        prefix = _stmt_prefix(b, m.start())
        if _discards(prefix):
            return "discards the result of"
        target = re.search(r"([A-Za-z_]\w*)\s*=$", prefix)
        if target:
            if (_TESTED_TAIL.search(prefix[:target.start()])
                    and not _COMMA_FIRST.match(b, _call_end(b, m.end() - 1))):
                continue          # the enclosing if/while/switch/return uses it
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
    # Declarations are searched in BLANKED text: a string literal spelling a
    # declarator list -- `static volatile char gMsg[] = "x, gFlag, y";` --
    # otherwise satisfies `_MULTI_DECLARATOR` and the checker follows a name
    # that has no such declaration (found while verifying the codex leg's
    # macro-argument finding, PR #901 round 1). `_STATIC_VOLATILE` could not
    # reach into a literal, because it cannot cross the `=`; the second
    # alternative can, so both now read the blanked copy.
    decls = _blank(text)
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
        # `_MULTI_DECLARATOR` beside it is the second and later declarators of
        # one declaration; it is a separate pattern precisely so this anchor
        # stays as it is.
        #
        # This is the ONLY filter. A keyword blocklist was written here first
        # and then removed: with the anchor in place no arm could tell the two
        # apart, so it was defensive code that could not fail -- and a check
        # that cannot fail is what this checker is about.
        if (re.search(_STATIC_VOLATILE % re.escape(name), decls)
                or re.search(_MULTI_DECLARATOR % re.escape(name), decls)):
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


def check_streaming(streaming_text):
    """-> (problems, flag_name_or_None) for the claim primitive itself."""
    text = strip_c_comments(streaming_text)
    flag, why = claim_flag(text)
    if flag is None:
        return [why], None

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
        return problems, flag

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
                "it at once (CLAUDE.md, Atomicity & Concurrency Rules)."
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
_GOOD_STREAM = '''
static volatile bool gSomethingElse = false;
static volatile uint32_t gCfgChangeBusy = 0u;

StreamingCfgClaim Streaming_BeginConfigChange(void) {
    StreamingRuntimeConfig* pCfg = BoardRunTimeConfig_Get(BOARDRUNTIME_STREAM);
    StreamingCfgClaim result;
    taskENTER_CRITICAL();
    if (pCfg->IsEnabled || pCfg->Running) {
        result = STREAM_CFG_CLAIM_STREAMING;
    } else if (gCfgChangeBusy != 0u) {
        result = STREAM_CFG_CLAIM_BUSY;
    } else {
        gCfgChangeBusy = 1u;
        result = STREAM_CFG_CLAIM_OK;
    }
    taskEXIT_CRITICAL();
    return result;
}

void Streaming_EndConfigChange(void) {
    gCfgChangeBusy = 0u;
}

bool Streaming_ConfigChangeInProgress(void) {
    return (gCfgChangeBusy != 0u);
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

        # #896 section B: a return type on its own line is ordinary C, and
        # reporting "could not find that function to check it" about a
        # definition that is present is a false red -- which is how a gate
        # gets bypassed or deleted.
        split = _GOOD.replace(
            "static scpi_result_t SCPI_MemRunClaimed(scpi_t *c, scpi_result_t (*b)(scpi_t *),\n"
            "                                        const char *what) {",
            "static scpi_result_t\n"
            "SCPI_MemRunClaimed(scpi_t *c, scpi_result_t (*b)(scpi_t *),\n"
            "                   const char *what) {")
        assert split != _GOOD
        _ck("a return type on its own line is still a definition",
            check(split)[0], [])
        _ck("...and its dispatch parameter is still found there",
            dispatch_param(split, CLAIM_HELPER), "b")

        # ...and the hole that layout costs if it is taken as merely making
        # the return type optional. A condition split across lines puts the
        # CALLED name at column 0, and the parameter span then absorbs the
        # inner `)` exactly as an unanchored pattern would. The first attempt
        # at the arm above returned the `if` body here.
        split_cond = _GOOD + """
static scpi_result_t decoy2(scpi_t * c) {
    if (
helper_probe2(c)) { return SCPI_RES_ERR; }
    return SCPI_RES_OK;
}
"""
        _ck("a call at column 0 inside a split condition is not a definition",
            function_body(split_cond, "helper_probe2"), None)

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

        # ...and the arm above passes with `_blank` gutted, because its
        # fixture has no `(` and the CALL FORM alone rejects it. This one has
        # the parenthesis, so it is the arm that actually holds `_blank`
        # up: without literal-blanking the mention satisfies `_calls` and a
        # setter with no claim at all reports OK (#896 (g)).
        spelled = _GOOD.replace(
            'return SCPI_MemRunClaimed(context, SCPI_SetMemSdBufClaimed, "a{b}c");',
            'const char *m = "SCPI_MemRunClaimed(context, cb, why)"; (void)m;\n'
            '    return SCPI_SetMemSdBufClaimed(context);')
        assert spelled != _GOOD
        _ck("a literal spelling a whole CALL is not accepted as one",
            any("does not go through" in p for p in check(spelled)[0]), True)

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

        # #896 (e): assignment-in-condition. A correct helper, reported with a
        # message that says the opposite of what it does -- the store scan
        # starts at the first `;` after the call, which here ends the `return`
        # INSIDE the if-body, so both real inspections sit before it.
        condtest = _GOOD.replace(
            "    StreamingCfgClaim claim = Streaming_BeginConfigChange();\n"
            "    if (claim != STREAM_CFG_CLAIM_OK) { return SCPI_RejectCfgClaim(c, 1, what); }",
            "    StreamingCfgClaim claim;\n"
            "    if ((claim = Streaming_BeginConfigChange()) != STREAM_CFG_CLAIM_OK) {\n"
            "        return SCPI_RejectCfgClaim(c, claim == STREAM_CFG_CLAIM_BUSY, what);\n"
            "    }")
        assert condtest != _GOOD
        _ck("an assignment the `if` tests is an inspection, not a false red",
            check(condtest)[0], [])

        # ...and the exemption is only for a DIRECTLY enclosing test. Handing
        # the verdict to another function and never looking at it again must
        # still be caught, or the fix above would have widened the hole it
        # was closing.
        handed = _GOOD.replace(
            "    StreamingCfgClaim claim = Streaming_BeginConfigChange();\n"
            "    if (claim != STREAM_CFG_CLAIM_OK) { return SCPI_RejectCfgClaim(c, 1, what); }",
            "    StreamingCfgClaim claim;\n"
            "    log_claim(claim = Streaming_BeginConfigChange());")
        assert handed != _GOOD
        _ck("...but a verdict merely handed to another call is still caught",
            any("never inspects the verdict" in p for p in check(handed)[0]),
            True)

        # #896 (g): the BARE discarded call -- the module docstring's own
        # edit #2, "keep the call and throw the verdict away". Round 4 armed
        # the other three clauses of `_discards` and not this one, so deleting
        # its `prefix == ""` clause left the suite green.
        barecall = _GOOD.replace(
            "StreamingCfgClaim claim = Streaming_BeginConfigChange();",
            "Streaming_BeginConfigChange();")
        assert barecall != _GOOD
        _ck("a bare discarded call is caught",
            any("discards the result" in p for p in check(barecall)[0]), True)

        # #896 (f): `case <expr>:` is a labelled statement too (C11 6.8.1),
        # and `_discards` enumerated only the identifier form while its
        # docstring said "a labelled statement".
        cased = _GOOD.replace(
            "StreamingCfgClaim claim = Streaming_BeginConfigChange();",
            "case 1: Streaming_BeginConfigChange();")
        assert cased != _GOOD
        _ck("a `case` label discards just as an identifier label does",
            any("discards the result" in p for p in check(cased)[0]), True)

        # PR #901 round 1 (codex leg): a case EXPRESSION may contain a colon.
        # `[^:]*` stopped at the ternary's, so the label was not recognised
        # and the discarded call was credited as a use.
        ternary_case = _GOOD.replace(
            "StreamingCfgClaim claim = Streaming_BeginConfigChange();",
            "switch (1) { case (1 ? 1 : 3): Streaming_BeginConfigChange(); break; }")
        assert ternary_case != _GOOD
        _ck("a `case` label whose expression contains a colon still discards",
            any("discards the result" in p for p in check(ternary_case)[0]),
            True)

        # PR #901 round 1 (codex leg), and the most important arm here: being
        # inside a test is NOT an inspection if a COMMA OPERATOR throws the
        # value away first. The exemption's first version skipped the store
        # scan on these, making the checker weaker than the code it fixed --
        # pre-#901 caught them, by accident, through the very mis-scan #896
        # (e) is about. Both comma placements, because stepping over the
        # closing parenthesis is what makes the second one reachable.
        for label, body in (
                ("a comma operator inside the assignment's parens",
                 "    StreamingCfgClaim claim;\n"
                 "    if ((claim = Streaming_BeginConfigChange(), 1)) { ; }"),
                ("a comma operator after them",
                 "    StreamingCfgClaim claim;\n"
                 "    if ((claim = Streaming_BeginConfigChange()) , 1) { ; }")):
            mutated = _GOOD.replace(
                "    StreamingCfgClaim claim = Streaming_BeginConfigChange();\n"
                "    if (claim != STREAM_CFG_CLAIM_OK) { return SCPI_RejectCfgClaim(c, 1, what); }",
                body)
            assert mutated != _GOOD, label
            _ck("%s still discards the verdict" % label,
                any("never inspects the verdict" in p
                    for p in check(mutated)[0]), True)

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
        # #896 (g): `|= 0u` in BEGIN. The existing `|= 0u` arm is on End, so
        # crediting `|= 0` as a SET left the suite green.
        _ck("`|= 0u` is not taking the claim either (it is a no-op)",
            any("never sets" in p for p in check_streaming(_GOOD_STREAM.replace(
                "        gCfgChangeBusy = 1u;",
                "        gCfgChangeBusy |= 0u;"))[0]), True)
        # #896 filed `&= ~MASK` as a false red. It is not one, and this arm
        # pins the refusal so a future "fix" trips it: the flag is binary, so
        # `&= ~MASK` releases the claim only if MASK covers the bit Begin set,
        # which needs the mask value. Crediting it regardless would turn a
        # loud red into a SILENT pass on an End that releases nothing.
        maskclr = _GOOD_STREAM.replace("    gCfgChangeBusy = 0u;\n}",
                                       "    gCfgChangeBusy &= ~CFG_BUSY;\n}")
        assert maskclr != _GOOD_STREAM
        _ck("`&= ~MASK` is deliberately NOT credited as releasing the claim",
            any("never clears" in p for p in check_streaming(maskclr)[0]), True)
        # #896 section B: a second declarator in one declaration is ordinary
        # C, and the anchor that stops an initialiser being read as a
        # declarator cannot cross the first `=`, so the flag was invisible and
        # the gate redded correct code.
        multi = _GOOD_STREAM.replace(
            "static volatile uint32_t gCfgChangeBusy = 0u;",
            "static volatile uint32_t gPoolBytes = 0u, gCfgChangeBusy = 0u;")
        assert multi != _GOOD_STREAM
        _ck("a second declarator in one declaration is still found",
            check_streaming(multi)[0], [])
        # PR #901 round 1 (codex leg): the declarator search must not enter a
        # parenthesised initialiser. Here the flag is NOT `static volatile` at
        # all -- accepting a macro ARGUMENT as a declarator made the gate
        # report OK on a claim flag with no such declaration, which is a
        # silent pass.
        macroarg = _GOOD_STREAM.replace(
            "static volatile uint32_t gCfgChangeBusy = 0u;",
            "static volatile uint32_t gOther2 = FOO(1, gCfgChangeBusy, 2);\n"
            "uint32_t gCfgChangeBusy = 0u;")
        assert macroarg != _GOOD_STREAM
        _ck("a macro ARGUMENT is not a declarator",
            any("static volatile" in p
                for p in check_streaming(macroarg)[0]), True)
        # ...and the same search must not read a string literal. Found while
        # verifying the finding above: `_STATIC_VOLATILE` could never reach
        # into a literal (it cannot cross the `=`), but the second alternative
        # can, so both now read a blanked copy.
        litdecl = _GOOD_STREAM.replace(
            "static volatile uint32_t gCfgChangeBusy = 0u;",
            'static volatile char gMsg[] = "x, gCfgChangeBusy, y";\n'
            "uint32_t gCfgChangeBusy = 0u;")
        assert litdecl != _GOOD_STREAM
        _ck("a declarator list spelled inside a literal is not a declaration",
            any("static volatile" in p
                for p in check_streaming(litdecl)[0]), True)

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
    stream_problems, claimflag = check_streaming(_read(args.streaming))
    problems = problems + stream_problems

    if problems:
        print("FAIL: SYSTem:MEMory:* claim-path check (%d examined)" % examined)
        for p in problems:
            print("  - %s" % p)
        return 1
    # States BOTH halves, because the CI gate re-runs on streaming.* and a
    # summary naming only the SCPI half would report a pass on a file it had
    # not spoken about.
    print("OK: all %d SYSTem:MEMory:* setters take the claim via %s(), which "
          "uses its verdict and holds it across the dispatch; %s() sets and "
          "%s() clears %s, the set inside a single critical section that also "
          "reads it"
          % (examined, CLAIM_HELPER, CLAIM_BEGIN, CLAIM_END, claimflag))
    return 0


if __name__ == "__main__":
    sys.exit(main())
