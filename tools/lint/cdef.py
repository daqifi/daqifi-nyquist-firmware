#!/usr/bin/env python3
"""ONE answer to "what is a definition of this C function", for every tool
in this repository that has to ask.

WHY THIS FILE EXISTS. PR #976 shipped a textual SD-arm-path lint and a
host-test drift pin, and FIVE separate places each answered that question in
their own words: `hash_function.py`'s extractor, `scpi_sd_arm_path.py`'s
`_DEF`, its `function_body()`, its `signature_params()`, and a `grep` in
`tests/host/Makefile`. Three adversarial audit rounds found six defects and
every single one was a DISAGREEMENT between two of those five -- never a
disagreement about C, always about which matcher had been taught what:

  round 1  the extractor took the FIRST definition and never noticed a
           second, so an `#if 0`-disabled original plus a live replacement
           pinned the digest to text the compiler never builds.
  round 1  `_DEF` was anchored within one line, so a definition with its
           return type on its own line had no body span at all and its call
           sites dropped out of the census while the floor count passed.
  round 2  the pair could simply be written in TWO STYLES: round 1 taught
           `_DEF` the split-line form and left the extractor on a literal
           one-line `str.find`, so one literal match meant no ambiguity to
           report and a digest of the dead copy.
  round 2  `__attribute__((...))` between the return type and the name was
           captured AS the name -- live on shipped source.
  round 3  `signature_params()` still required type and name to share a
           line, so splitting a definition FAILED the lint on formatting
           that changes no behaviour.
  round 4  `function_body()` and `signature_params()` still took the FIRST
           match with no ambiguity check, so a disabled original plus a live
           replacement with no claim and no arm call passed the lint clean.

The pattern is the finding. Two files that agree by a comment saying they
must agree will drift, and each drift is silent. So the rule lives here, in
one module, and the callers import it.

WHAT THE RULE IS.

  prefix      identifier characters, spaces, tabs and `*`
  attribute   an optional `__attribute__((...))`, one level of nesting
  break       at most ONE newline between the prefix and the name, so the
              prefix cannot run away across unrelated declarations
  name        the function's identifier
  params      `(...)` containing no `;` or `{`
  body        an opening `{`

MATCHING RUNS ON MASKED TEXT, and on a DIFFERENT mask from brace counting.
`mask()` preserves newlines so that offsets AND line structure survive, which
is what brace counting needs. But a comment carrying newlines INSIDE a
signature -- `static bool /*\n * why\n */ Foo(void)` -- then leaves several
newlines between the prefix and the name, and the one-break rule refuses it.
That was round 4's first finding: the live definition became unmatchable, only
the disabled copy matched, and the ambiguity guard never fired.
`mask_for_match()` blanks comment newlines to spaces as well, so a comment
inside a signature collapses to whitespace while REAL line structure, which
lies outside comments, is untouched.

AMBIGUITY IS REFUSED, NEVER RESOLVED. Two definitions answering to one name
is a question about preprocessor state, and no tool here evaluates that.
Taking the first is how the pin digested dead code; taking the last is no
better. `find_definitions()` returns all of them and the callers refuse.

EVERY OCCURRENCE IS CLASSIFIED OR REPORTED. Rounds 5 and 6 and the review
after them kept finding ONE MORE valid spelling the matchers above had not
been taught -- an indented definition, an attribute on its own line, `(F)`,
`((F))`, a spliced `//` -- and after them two more were live at once: a
replacement with TWO `__attribute__((...))` prefixes next to an `#if 0`
original (one definition found, the DEAD one, reported unambiguous and
pinned), and a call written `(F)(...)` (invisible to the arm census). Every
one was FAIL-OPEN: live code left the text being checked while the check
reported clean. Teaching the definition regex every valid C spelling, one
per audit round, cannot terminate.

So the question is turned round. Instead of recognising every valid
spelling positively, `account_occurrences()` requires that EVERY raw
occurrence of a name -- every identifier token the compiler would see,
found WITHOUT any grammar -- be claimed by exactly one recogniser:
definition, file-scope prototype, or a call written the way the call
contract below allows. Anything left over is reported, with its physical
line: an alias, `&F`, `p = F;`, a declaration, a call written in a style the
contract does not accept, a spelling nobody has thought of. An unrecognised
construct therefore fails CLOSED -- the lint reds -- instead of silently
falling out of view; nobody has to anticipate the next spelling, only
whether every occurrence was explained. "Explained" means "claimed by a
recogniser", and a claim is only as good as the recogniser's own rule: the
declarator grammar above for definitions and prototypes, and the contract
below for calls.

THE CALL RECOGNISER IS A BOUNDED STYLE CONTRACT. It does NOT recognise
"every call C evaluates", and does not try to. A tracked name counts as a
call only when it is written `F(args)` -- the bare name, then its argument
list -- and that is the ENTIRE expression of one of five statement shapes:

  1  an expression statement      `F(args);`  or  `(void)F(args);`
  2  an `if` condition            `if (F(args))`  or  `if (!F(args))`
  3  a declaration's initializer  `T v = F(args);`  -- `T` identifier words
                                  and `*` only, `v` one identifier
  4  an assignment                `v = F(args);`  -- `v` one identifier
  5  a return                     `return F(args);`

and that statement sits at BLOCK LEVEL: it begins right after a `;`, right
after a compound statement's `{`, or right after the `}` that ends one; and
every bracket enclosing it, from the function body's own `{` down, is a `{`
that opens a compound statement -- the body, a block after the `)` of `if`,
`while`, `for` or `switch`, a block after `else` or `do`, or a bare `{ }`
block standing as a statement. Never a `(`, never a `[`, never an
initializer's or a compound literal's `{`, and never a GNU statement
expression's `({` -- nor anything inside one, however block-like it looks.
The argument list may span lines. `_not_a_call` holds the rule, and
`_brace_scan` walks the enclosing brackets FORWARD from the body's `{`: the
shape alone is not enough, because `if (F(1)) {}` inside a statement
expression inside an array bound inside a declarator LOOKS like shape 2.

Everything else is unclassified, and some of it is a call the compiler DOES
make: `x = a * F(1);`, `g(a, F(1));`, `(F)(1);`, a braceless `if (x)
F(1);`, a `while` condition, `case 1: F(1);`, `else if (F(1))`,
`v = F(1), w;`. That is the price, and it is deliberate. The recogniser
this replaces inferred "is this call evaluated?" from local punctuation and
subtracted the unevaluated and non-call contexts it had been taught
(`sizeof`, declarations, `__typeof__`, member access, ...). Audit rounds 8
and 9 each found another context it read as an evaluated call --
`__builtin_choose_expr`'s unchosen operand, a statement expression inside an
array bound, a VLA bound in a block-scope prototype (never evaluated, C11
6.7.6.2p5) -- and an external review of that design found no completion
criterion for it: a list of refusals is never finished. Five ACCEPTED shapes
are finished by construction. An occurrence either is one of them or it is
not, and one that is not is refused, for a human to read or to rewrite in
one of the five shapes -- which every tracked call in the firmware already
is.

What the contract does NOT establish -- out of scope, by design:

  * preprocessing and macro expansion. No directive is evaluated: a call
    inside `#if 0` still counts as a call, deliberately, and a macro defined
    in the text that expands around an accepted shape (a type word that is
    really `if (0)`) is not seen through. A name ON a directive line is
    refused.
  * symbols defined outside the text given: an alias or a `-D` define in a
    header, a typedef or macro elsewhere reusing a tracked name, and macro
    token pasting (see `line_comment_end`'s #1066 note).
  * reachability and control flow. `if (0) { F(x); }` counts as a call: the
    shape is accepted, and no reachability analysis is done. That is
    intentional -- the honest-regression limit #896 tracks for every checker
    of this family, not a new gap -- and so is the absence of a symbol table.
  * runtime ownership. An accepted call proves only that the TEXT reads
    claim-then-arm; nothing here says a claim is HELD when an arm runs.

Two cross-checks make the claim bind the callers rather than float beside
them: the definitions the as-written matcher (`find_definitions`) reports
must be the ones the compiler's view reports, and `scpi_sd_arm_path.py` runs
its census on the SAME compiler's view (`compiler_view(src).matchmask`) and
requires it to find exactly the calls accounting found, AT THE SAME
POSITIONS. The first version compared per-function COUNTS, and two
misreadings that cancel in a count -- a claim only the census reads, before
the arm, and one only accounting reads, after it -- moved the claim past its
arm with both reporting clean (#976 review).

What none of that buys is anything about compiled behaviour: it is textual,
with exactly the limits listed under the call contract above. (One that used
to be listed here -- `UNUSED(F(x))`, a call handed to a macro that may
discard it, read as a call -- is now refused: `F(x)` there is an argument,
not the entire expression of an accepted shape.)
"""

import bisect
import os
import re
from collections import namedtuple


class AmbiguousDefinition(Exception):
    """Two or more definitions answer to the same name.

    Raised rather than resolved: which one the compiler builds depends on
    preprocessor state this module does not evaluate. The caller reports it
    separately from a drift MISMATCH, so a reader is not sent to diff a
    function that did not change.
    """


def line_comment_end(src, start, already_spliced=False):
    """Index of the newline that truly ends a `//` comment beginning at
    `start` (the index of its leading `/`).

    C's translation phase 2 deletes a backslash immediately followed by a
    newline, splicing the two physical lines into one logical line -- BEFORE
    phase 3 even recognises comments. So a `//` comment does not end at a
    newline that phase 2 already erased; it continues onto the next physical
    line, and the next, for as many such splices as appear in a row (#976
    audit round 6). A comment-boundary finder that stops at the first bare
    newline is looking for a boundary the compiler never sees.

    NOT HANDLED HERE (#1066): a splice can also CREATE a comment marker --
    `/` + `\\` + newline + `/` splices into `//`, `/` + `\\` + newline + `*`
    into `/*` -- or END a block comment early -- `*` + `\\` + newline + `/`
    is `*/`, where a raw reader runs on to the next literal `*/` and hides
    every line between, code the compiler builds -- and a splice can land
    INSIDE an identifier (`SD_Arm` + `\\` + newline + `OrRefuseWithCleanup`
    is ONE token, `SD_ArmOrRefuseWithCleanup`, to the compiler). This
    function and `mask()`/`mask_for_match()` look for literal, unspliced
    markers and identifiers, so fed RAW text they see none of it. Verified
    live against `SCPIStorageSD.c` before occurrence accounting existed: the
    first shape deleted the CRC claim guard with `scpi_sd_arm_path.py`
    reporting zero problems; the `*/` shape (#976 review) added an early
    `return true;` to the pinned helper with its sha256 unchanged.

    WHERE IT IS HANDLED, AND WHERE IT IS NOT. `splice()` joins every splice
    across the whole text ONCE, and the compiler's view (`_View`,
    `compiler_view()`) runs `mask()`/`mask_for_match()` on the joined
    result, where every shape above is an ordinary marker or a whole
    identifier. Reading that view: occurrence accounting (this module);
    `scpi_sd_arm_path.py`'s census, whose `check()`/`check_stream()` scan
    `compiler_view(src).matchmask` and nothing else; and `hash_function.py`'s
    digest, which hashes the splice-joined text of the body it extracts.
    Still reading RAW text, bound rather than fixed: the as-written
    definition matcher (`find_definitions`/`one_definition`) --
    `account_occurrences` refuses when it and the compiler's view disagree
    about where a pinned name is defined -- and `hash_function.extract()`'s
    brace count, whose span is then digested in the compiler's view: if the
    raw count did not stop at the compiler's own closing `}`, the digested
    code stops short of that brace or carries on past it, so it equals the
    pin only if the compiler's view of the function does. NOT COVERED:
    `scpi_wiki_sync._CODE_OR_COMMENT`/`registered_patterns()`, which this
    change does not touch -- #1066 stays open there. The joined view also
    takes GCC's backslash-SPACE-newline (see `_SPLICE`).

    A GAP NO LEXICAL SCAN CLOSES: a name that is not in the text. Macro
    token pasting -- `CAT(SD_ArmOrRefuse, WithCleanup)(...)` -- assembles the
    identifier from two half-tokens (`##`), and an alias defined in a header
    or with `-D` puts the only visible spelling somewhere this module is
    never given. This module's domain is the spellings VISIBLE in the text it
    is given, not compiled behaviour. (An alias `#define A F` INSIDE the text
    is not this gap: F is visible there, and an occurrence in a directive is
    refused.) Also outside it, and believed inert: trigraphs (`??/` is a
    backslash in phase 1 under strict ISO modes; the firmware build passes
    no `-std`, so XC32 runs in a GNU mode, where GCC does not replace them).

    `already_spliced` SAYS WHICH TEXT THIS IS, and it has to be said: the
    same characters mean different things before and after phase 2. On RAW
    text (the default) a backslash before a newline IS a splice site, so the
    comment runs on. On text that has ALREADY been through `splice()` --
    the compiler's view -- phase 2 is over: it ran once, over the physical
    source, and never re-scans its own output. A backslash still standing
    before a newline there is an ordinary character (`\\\\` + newline
    splices only the SECOND backslash, leaving the first), and the newline
    ends the comment like any other. Scanning joined text in raw mode applied
    phase 2 a SECOND time: `// x \\\\` above a blank line swallowed the line
    after the blank -- an early `return true;` that GCC builds -- from the
    census and from the sha256 pin at once (#976 audit round 8).
    """
    n = len(src)
    i = start
    while True:
        end = src.find("\n", i)
        if end < 0:
            return n
        if not already_spliced and end > 0 and src[end - 1] == "\\":
            i = end + 1
            continue
        return end


def literal_end(src, start, already_spliced=False):
    """Index just past the string or character literal whose opening quote
    is at `start` -- or, for one never closed, the index of the newline that
    ends it (which is not part of it).

    A backslash escapes the character after it, and -- in RAW text, the
    default -- a backslash-newline splice (either form `_SPLICE` accepts)
    continues the literal onto the next physical line, as translation phase
    2 does. With `already_spliced=True` the text is past phase 2 (see
    `line_comment_end`), so there is no splice left to find, and a backslash
    standing before a newline escapes nothing: GCC's lexer does not let `\\`
    escape a newline (`lex_string`), the newline ends the literal
    unterminated, and the next line is lexed as code. Raw mode there ran the
    literal on across that line: `#define Q 'x\\\\` above a blank line hid
    the line after the blank from every reader of the compiler's view
    (#976 audit round 8, the literal twin of `line_comment_end`'s). An
    UNESCAPED newline
    ends it: no literal can contain one, and GCC -- after "missing
    terminating ' character", only a warning in an `#if 0` block or on an
    `#error` line -- lexes the next line as code. Until the #976 review this
    ran on to the next matching quote instead, so an apostrophe in `#if 0`
    prose (`it's`) blanked every line up to the next `'` in the file: code
    the compiler builds, hidden from every reader of `mask()` at once, and an
    arm placed between two such blocks was counted by nobody.

    The ONE literal rule: `mask()`, `mask_for_match()` and
    `hash_function.strip_comments()` all end a literal here.
    """
    n, i = len(src), start + 1
    quote = src[start]
    while i < n:
        ch = src[i]
        if ch == "\\":
            sp = None if already_spliced else _SPLICE.match(src, i)
            if sp:
                i = sp.end()
            elif i + 1 < n and src[i + 1] == "\n":
                # Joined text only (in raw text `_SPLICE` always matches
                # here): the newline is not escaped, it ends the literal.
                i += 1
            else:
                i += 2
            continue
        if ch == "\n":
            return i
        i += 1
        if ch == quote:
            return i
    return n


def mask(src, already_spliced=False):
    """`src` with every comment and literal blanked, LENGTH and LINES kept.

    Length preservation is the point: offsets into the mask are offsets into
    the original, so braces can be counted on text where no brace inside a
    comment or a string can be mistaken for code. Newlines survive so `(?m)^`
    still sees the real line structure -- including the one a splice inside
    a literal carries, which this used to blank along with its backslash.

    `already_spliced=True` for text that has been through `splice()` (the
    compiler's view): no backslash-newline there is a splice any more, and
    the boundary finders are told so -- see `line_comment_end`. The default
    is RAW text, which every as-written reader passes.
    """
    out = list(src)
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c in ('"', "'"):
            end = literal_end(src, i, already_spliced)
            for j in range(i, end):
                out[j] = "\n" if src[j] == "\n" else " "
            i = end
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            end = src.find("*/", i + 2)
            end = n if end < 0 else end + 2
            for j in range(i, end):
                out[j] = "\n" if src[j] == "\n" else " "
            i = end
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            end = line_comment_end(src, i, already_spliced)
            for j in range(i, end):
                out[j] = "\n" if src[j] == "\n" else " "
            i = end
            continue
        i += 1
    return "".join(out)


def mask_for_match(src, already_spliced=False):
    """`mask()`, and comment newlines blanked to spaces as well.

    For DEFINITION MATCHING only. A comment inside a signature is whitespace
    to the compiler, and leaving its newlines in place made the live
    definition unmatchable while a disabled copy elsewhere still matched --
    the ambiguity guard then had nothing to report (#976 audit, round 4).
    Real line structure lies outside comments and is untouched, so `(?m)^`
    still anchors where it should.

    Length is preserved, so offsets remain offsets into the original.
    """
    # `mask()` keeps every newline the source has, including the ones inside
    # a block comment OR a `//` comment extended by a backslash-newline
    # splice (`line_comment_end`, #976 audit round 6 -- a spliced `//`
    # comment straddling a signature is the same round-4 shape, reached
    # through a line comment instead of a block comment). Those are the ones
    # to blank here, so re-walk the SOURCE for comment spans and flatten
    # them; string literals cannot contain a raw newline in C, so they need
    # no such pass.
    # `already_spliced` as in `mask()`, and passed to every boundary finder
    # below as well as to `mask()` itself, so the two walks cannot disagree
    # about where a comment or a literal ends.
    out = list(mask(src, already_spliced))
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            end = src.find("*/", i + 2)
            end = n if end < 0 else end + 2
            for j in range(i, end):
                out[j] = " "
            i = end
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            end = line_comment_end(src, i, already_spliced)
            for j in range(i, end):
                out[j] = " "
            i = end
            continue
        if c in ('"', "'"):
            i = literal_end(src, i, already_spliced)
            continue
        i += 1
    return "".join(out)


# LEADING WHITESPACE IS ALLOWED. Anchoring the prefix at column zero made a
# definition indented by a single space invisible to EVERY caller at once --
# so an `#if 0`-disabled original plus an indented live replacement matched
# only the disabled copy, nothing was ambiguous, and the pin digested dead
# code. That is the same silent class rounds 1, 2 and 4 each fixed at a
# different position, arriving through indentation instead (#976 audit,
# round 5). C does not care about the column and neither can this.
_INDENT = r"[ \t]*"
_PREFIX = r"[A-Za-z_][\w \t\*]*"
# A line break is allowed on EITHER side of the attribute, not only after it.
# With one break available only after `_ATTR`, an attribute on its own line
# between the return type and the name did not merely fail to match -- the
# pattern backtracked until NAME bound to the literal `attribute`, swallowing
# the real definition and reporting it absent. Valid, unambiguous,
# behaviour-neutral C that the checker refused (#976 audit, round 5).
_ATTR = (r"(?:__attribute__\s*\(\((?:[^()]|\([^()]*\))*\)\)[\w \t\*]*)?")
_BREAK = r"(?:[ \t]*\n[ \t]*)?"
HEAD = r"(?m)^" + _INDENT + _PREFIX + _BREAK + _ATTR + _BREAK

# A declarator's name may sit inside one level of redundant parentheses --
# `static bool (F)(void)` declares exactly the same function as
# `static bool F(void)` (a parenthesized direct-declarator, C11 6.7.6). It is
# valid, warning-free C, though NOT how `SD_ArmOrRefuseWithCleanup` is
# actually written in `SCPIStorageSD.c` today (that shipped source is the
# ordinary, unparenthesized form; an earlier revision of this comment claimed
# otherwise -- checked against source, corrected). The audit reproduced the
# bypass by rewriting that real function's declarator as
# `static bool (SD_ArmOrRefuseWithCleanup)(scpi_t *ctx)` -- a shape the
# compiler accepts unchanged -- because `_PREFIX` cannot span the `(`, so
# without this every matcher here was blind to the form at once: an
# `#if 0`-disabled original plus a live parenthesized replacement left
# `find_definitions` reporting the DEAD one as the only definition, and
# `AmbiguousDefinition` never fired (#976 audit round 6).
#
# Open and close are NOT independently-optional groups -- a first draft of
# this fix used `(?:\(...)?` and `(?:...\))?` separately, and that let a
# lone wrap-open borrow an UNRELATED `)` from real code that happened to
# follow: `if (F(x)) {` matched as a definition of F, with the `if`
# condition's own `(` eaten as the "wrap" and the call's closing `)` (plus
# its own `(x)`) reinterpreted as the parameter list -- caught by this
# module's OWN self-test, not by inspection. Each nesting level captures
# whether ITS open paren was seen (groups 1 and 2, outer then inner); the
# matching conditional (`(?(2)...)`, then `(?(1)...)`, innermost first)
# REQUIRES a close only for a level that opened -- open and close are
# all-or-nothing, at every level, and a level can only open if the one
# outside it did too (it is nested inside that level's own optional group).
#
# TWO levels, not one: a second-round finding (#976 audit round 6 review)
# showed the single-level version still silently bound an ambiguous pair to
# the WRONG (disabled) copy when the live replacement used `((F))` instead of
# `(F)` -- the exact defect class this rule exists to close, one paren away.
# A single newline (with surrounding spaces/tabs) is allowed at each
# boundary too, for the same reason `_BREAK` allows it elsewhere: C does not
# care about the line, and refusing a definition over formatting is how a
# live replacement gets silently outvoted by a disabled one.
#
# Three or more levels remain UNSUPPORTED BY THIS MATCHER -- `_CASES`
# records this explicitly rather than leaving it an unstated assumption;
# nothing in the 446-file firmware source tree uses even one level. The
# matcher alone still binds an `#if 0` original plus a `(((F)))` replacement
# to the dead copy; what makes that safe for every caller is occurrence
# accounting, which finds the live copy's name token claimed by nothing and
# refuses (see "EVERY OCCURRENCE IS CLASSIFIED OR REPORTED"). The cost of supporting two
# levels is two extra numbered groups ahead of whatever the caller captures:
# `capture_params=True`'s parameter-list group and `ANY_DEF`'s name group
# both shift from group 1 to group 3. Every reader of either has been
# updated to match.
_WRAP_SPACE = r"[ \t]*(?:\n[ \t]*)?"
_NAME_WRAP_OPEN = (r"(?:(\()" + _WRAP_SPACE
                    + r"(?:(\()" + _WRAP_SPACE + r")?)?")
_NAME_WRAP_CLOSE = (r"(?(2)" + _WRAP_SPACE + r"\))"
                     r"(?(1)" + _WRAP_SPACE + r"\))")


def _signature(name_re, inner, end):
    """HEAD, the (possibly wrapped) name, a parameter list, then `end`.

    The ONE grammar for "a function declarator at the start of a line".
    `end` is `\\{` for a definition and `;` for a prototype, and nothing else
    differs between the two -- so the prototype matcher occurrence accounting
    needs (below) cannot drift from the definition matcher by construction,
    which is the whole lesson of this module's docstring. `def_pattern` and
    `ANY_DEF` produce exactly the strings they produced before this builder
    existed.
    """
    return (HEAD + _NAME_WRAP_OPEN + r"\b" + name_re + _NAME_WRAP_CLOSE
            + r"\s*\(" + inner + r"\)\s*" + end)


def def_pattern(name, capture_params=False):
    """The definition pattern for ONE named function, as a string.

    When `capture_params` is True, the parameter list is GROUP 3, not group
    1 -- groups 1 and 2 are `_NAME_WRAP_OPEN`'s own paren-seen markers (see
    its comment above).
    """
    inner = r"([^;{]*)" if capture_params else r"[^;{]*"
    return _signature(re.escape(name), inner, r"\{")


def definition_re(name, capture_params=False):
    """The compiled definition pattern for one named function."""
    return re.compile(def_pattern(name, capture_params))


# GROUP 3 is the name; groups 1 and 2 are `_NAME_WRAP_OPEN`'s markers (see
# `def_pattern`'s docstring).
ANY_DEF = re.compile(_signature(r"([A-Za-z_]\w*)", r"[^;{]*", r"\{"))


def find_definitions(text, name, capture_params=False):
    """Every definition of `name` in `text`, as a list of match objects.

    Matching runs on `mask_for_match(text)`, so a definition-shaped line
    inside a comment or a literal is not a definition, and a comment inside a
    real signature does not hide it. Offsets index into `text`.
    """
    return list(definition_re(name, capture_params)
                .finditer(mask_for_match(text)))


def one_definition(text, name, capture_params=False):
    """The single definition of `name`, or None. Raises on more than one."""
    found = find_definitions(text, name, capture_params)
    if not found:
        return None
    if len(found) > 1:
        raise AmbiguousDefinition(
            "%d definitions answer to %r; refusing to choose" % (len(found), name))
    return found[0]


# ===========================================================================
# OCCURRENCE ACCOUNTING -- see "EVERY OCCURRENCE IS CLASSIFIED OR REPORTED"
# and "THE CALL RECOGNISER IS A BOUNDED STYLE CONTRACT" in the module
# docstring for why this exists and what it does NOT do.
# ===========================================================================

# Phase 2's splice, as the compiler that builds this firmware performs it.
# ISO C deletes a backslash IMMEDIATELY followed by a newline; GCC -- which
# XC32 is -- also accepts spaces/tabs between the two ("backslash and newline
# separated by space" is a warning, and the lines ARE joined; GCC cpp manual,
# "Initial processing"). Accepting the GCC form here is the fail-CLOSED
# direction: every other stripper in this repository stops at the ISO form,
# so on a `// ... \<space>` comment they hand a swallowed line to their
# callers as live code, and a caller that cross-checks against this view
# (`scpi_sd_arm_path.py` does) then sees the disagreement and refuses. `\r`
# is phase 1's CRLF, for text not read in universal-newline mode.
_SPLICE = re.compile(r"\\[ \t\f\v]*\r?\n")


def splice(src):
    """-> (joined, omap): `src` after translation phase 2, and the way back.

    Every backslash-newline splice is DELETED, so `joined` is SHORTER than
    `src` whenever there is one. `omap[j]` is the index in `src` that
    `joined[j]` came from; it has `len(joined) + 1` entries, the last mapping
    one-past-the-end to `len(src)`, so an END offset maps as well as a start.
    Counting newlines in `src` up to `omap[j]` gives the PHYSICAL line a
    reader has to open, which is what every diagnostic below reports.

    One pass, left to right, never re-scanned: `\\\\` + newline leaves ONE
    backslash, not zero, because the result of a splice is not itself
    spliced (phase 2 applies to the physical source, once).
    """
    pieces, omap, pos = [], [], 0
    for m in _SPLICE.finditer(src):
        pieces.append(src[pos:m.start()])
        omap.extend(range(pos, m.start()))
        pos = m.end()
    pieces.append(src[pos:])
    omap.extend(range(pos, len(src)))
    omap.append(len(src))
    return "".join(pieces), omap


def match_brace(masked, start):
    """Index just past the `}` closing the `{` at `start`, or None.

    `masked` must be a `mask()`ed text, so no brace in a comment or literal
    is counted. Moved here from `scpi_sd_arm_path.py` so the census and
    occurrence accounting cannot disagree about where a body ends.
    """
    depth = 0
    for i in range(start, len(masked)):
        if masked[i] == "{":
            depth += 1
        elif masked[i] == "}":
            depth -= 1
            if depth == 0:
                return i + 1
    return None


def _spans(matchmask, masked):
    """[(name, body_start, body_end)] for every OUTERMOST definition.

    `body_start` is the index of the opening `{`, `body_end` is one past the
    matching `}`. A definition-shaped match whose `{` falls inside a body
    already collected is skipped -- inside a body, `else if (x) {` is
    definition-SHAPED (`ANY_DEF` reads `if` as a name) and must not become a
    function of its own.
    """
    spans = []
    for m in ANY_DEF.finditer(matchmask):
        start = m.end() - 1
        if any(s <= start < e for _, s, e in spans):
            continue
        end = match_brace(masked, start)
        if end is not None:
            # GROUP 3 is the name; groups 1 and 2 are `_NAME_WRAP_OPEN`'s
            # paren-seen markers.
            spans.append((m.group(3), start, end))
    return spans


def function_spans(text):
    """[(name, body_start, body_end)] for every outermost definition in
    `text`, offsets into `text` as given (no splicing -- a caller that works
    on already-stripped text gets offsets into THAT text).

    Used to attribute an offset to the function whose body contains it. A
    definition's own name lies BEFORE its body starts, so it falls in no
    span and is never mistaken for a call site.
    """
    return _spans(mask_for_match(text), mask(text))


def enclosing(spans, pos):
    """Name of the function whose BODY contains `pos`, or None."""
    for name, start, end in spans:
        if start < pos < end:
            return name
    return None


# A digraph and the punctuator it spells (C11 6.4.6p3), two characters for
# two. `%:` and `%:%:` map to themselves -- they are `#` and `##`, which only
# a directive holds -- and are matched only so that `%:>` is not read as
# `%` + `:>`, the way the compiler's longest-match lexing does not.
_DIGRAPHS = {"<:": "[ ", ":>": " ]", "<%": "{ ", "%>": " }",
             "%:": "%:", "%:%:": "%:%:"}
_DIGRAPH_RE = re.compile(r"%:%:|%:|<:|:>|<%|%>")


def _code_text(matchmask):
    """`matchmask` as the compiler PROPER reads it, length kept.

    Every preprocessor directive's logical line is blanked -- the
    preprocessor removes it, and `matchmask` is joined with comment
    newlines blanked, so a continued or commented directive is one line
    here exactly as in `_in_directive` -- and every digraph is spelled as
    the bracket it is. The call contract reads this text in BOTH directions
    -- backward from the name for the statement's shape, forward for the
    argument list and for `_brace_scan`'s bracket chain -- so the two cannot
    disagree about a `)` on a directive line or about a `<:` (a round-8
    forward reader once read the one while a backward walk skipped it, #976
    round-8 review).
    """
    lines = matchmask.split("\n")
    for n, line in enumerate(lines):
        if line.lstrip(" \t\f\v").startswith(("#", "%:")):
            lines[n] = " " * len(line)
    return _DIGRAPH_RE.sub(lambda m: _DIGRAPHS[m.group(0)], "\n".join(lines))


class _View(object):
    """One text read the way the compiler reads it, with the way back.

    `joined` is `src` after phase 2. `masked` and `matchmask` are the SAME
    `mask()` and `mask_for_match()` every other caller uses, fed the joined
    text -- not a second comment scanner -- so a splice that CREATES a
    comment marker, or joins two halves of an identifier, is seen here the
    way the compiler sees it.

    Both are told the text is `already_spliced`. Phase 2 has run on
    `joined`, once, and a backslash left before a newline there -- `\\\\` +
    newline leaves one -- is an ordinary character to the compiler. Masked
    in raw mode, that backslash re-opened the splice phase 2 had closed and
    the comment or literal before it swallowed the next line: live code gone
    from accounting, the census and the pin at once (#976 audit round 8).
    """

    def __init__(self, src):
        self.src = src
        self.joined, self.omap = splice(src)
        self.masked = mask(self.joined, already_spliced=True)
        self.matchmask = mask_for_match(self.joined, already_spliced=True)
        # What the call recogniser reads: `_code_text`.
        self.code = _code_text(self.matchmask)
        self.spans = _spans(self.matchmask, self.masked)
        self._newlines = [m.start() for m in re.finditer("\n", src)]
        self._raw_matchmask = None

    def raw_matchmask(self):
        """`mask_for_match(src)` -- the AS-WRITTEN view every
        `find_definitions` caller reads -- computed once, on demand."""
        if self._raw_matchmask is None:
            self._raw_matchmask = mask_for_match(self.src)
        return self._raw_matchmask

    def line_of_orig(self, orig):
        """Physical (1-based) line of offset `orig` in the ORIGINAL text."""
        return bisect.bisect_left(self._newlines, orig) + 1

    def line(self, j):
        """Physical line, in the ORIGINAL text, of JOINED offset `j`."""
        return self.line_of_orig(self.omap[j])


# One entry, keyed by IDENTITY: a caller accounting five names in one file
# pays for the splice, the two masks and the span scan once, not five times.
# Holding the reference keeps the id from being reused by another string.
_VIEW_CACHE = []


def _view(src):
    if _VIEW_CACHE and _VIEW_CACHE[0][0] is src:
        return _VIEW_CACHE[0][1]
    view = _View(src)
    _VIEW_CACHE[:] = [(src, view)]
    return view


def compiler_view(src):
    """`src` read the way the compiler reads it: a `_View` -- `.joined`
    (after phase 2), `.masked` and `.matchmask` (comments and literals
    blanked, lengths kept), `.spans`, `.omap` (joined offset -> original
    offset) and `.line(j)` / `.line_of_orig(o)` (physical lines). Cached by
    identity, so every caller reading one file shares one view, which is what
    lets `scpi_sd_arm_path.py`'s census and occurrence accounting read the
    same characters."""
    return _view(src)


def identifier_re(name):
    """`name` as a whole identifier token, with GCC's boundaries -- `$` is an
    identifier character, so `my$F` is not an occurrence of F (see
    `_token_re`)."""
    return _token_re(name)


def directive_lines(src, name):
    """Physical lines on which `name` occurs inside a preprocessor directive
    (`#` or `%:`), in the compiler's view: where an alias or a macro body
    that reaches it is defined."""
    view = _view(src)
    return [view.line(m.start())
            for m in _token_re(name).finditer(view.masked)
            if _in_directive(view.matchmask, m.start())]


RawOccurrence = namedtuple("RawOccurrence", "offset line joined_offset")


def _token_re(name):
    # Identifier boundaries spelled out rather than `\b`, and deliberately
    # NOT Python's `\b`, because the census (`\bF\s*\(`) uses `\b` and the
    # agreement check only catches what the two read DIFFERENTLY:
    #
    #   `$` IS an identifier character here. GCC accepts it in identifiers
    #   (-fdollars-in-identifiers, on by default), so `my$F(x)` calls
    #   `my$F`, not F. `\b` sees a boundary before F and the census counts
    #   an arm; were `$` a boundary here too, accounting would count the
    #   same phantom call, the two would AGREE, and a real arm replaced by
    #   `my$F(...)` would pass. Reading no occurrence here makes it a
    #   disagreement, which reds.
    #
    #   Non-ASCII letters are NOT (ASCII class only). `\b` treats `é` as a
    #   word character and the census does not count `xéF(`; reading an
    #   occurrence of F here makes that a disagreement too. Either way the
    #   odd spelling fails closed.
    return re.compile(r"(?<![A-Za-z0-9_$])" + re.escape(name)
                      + r"(?![A-Za-z0-9_$])")


def raw_occurrences(src, name):
    """Every identifier-token occurrence of `name` the compiler would see.

    Runs on the splice-joined, comment/literal-masked view, so an occurrence
    in a comment or string is not one, and an identifier split across a
    backslash-newline IS one. The preprocessor is NOT evaluated: a copy
    under `#if 0` still shows up, deliberately (two textual definitions is
    what `AmbiguousDefinition` is for). Each result carries the ORIGINAL
    offset and PHYSICAL line, and the joined offset it was found at.
    """
    view = _view(src)
    return [RawOccurrence(view.omap[m.start()], view.line(m.start()),
                          m.start())
            for m in _token_re(name).finditer(view.masked)]


def _named_def_re(name):
    """`def_pattern(name)` with the NAME TOKEN captured as group 3 (1 and 2
    are the wrap markers). A capturing group changes what is reported, never
    what matches, so this matches exactly where `def_pattern` does."""
    return re.compile(_signature("(" + re.escape(name) + ")", r"[^;{]*",
                                 r"\{"))


def _named_proto_re(name):
    """The PROTOTYPE twin: the same declarator grammar, ending in `;`."""
    return re.compile(_signature("(" + re.escape(name) + ")", r"[^;{]*", ";"))


def _call_re(name):
    """The name -- bare, or inside the same one or two levels of redundant
    parentheses a declarator may wear -- then `(`. Name is group 3.

    `(F)(x)` calls F exactly as `F(x)` does, and `\\bF\\s*\\(` cannot see it
    because the `(` follows a `)`, not the name: that is how a parenthesized
    callee escaped `scpi_sd_arm_path.py`'s arm census. It is still FOUND
    here, so that it is refused for what it is -- the call contract accepts
    only the bare `F(args)` (`_not_a_call`) -- rather than reported as a name
    "not followed by `(`". The wrap is `_NAME_WRAP_OPEN`/`_NAME_WRAP_CLOSE`
    -- the definition side's own -- so the two sides agree on how deep a
    wrap can be (two levels; a third is call-shaped to neither and so fails
    closed as unclassified). The gap before the argument list is `\\s*`,
    the census's own, so formatting that the census reads as a call is read
    as call-shaped here too.
    """
    return re.compile(_NAME_WRAP_OPEN + "(" + re.escape(name) + ")"
                      + _NAME_WRAP_CLOSE + r"\s*\(")


def _call_at(call_re, matchmask, j):
    """The call match whose NAME starts at `j`, or None.

    Tried at `j` and at up to two `(` immediately before it (whitespace
    aside) -- every place a wrapped call can begin -- rather than by a
    left-to-right scan, which could consume one occurrence inside another's
    match and never test it.
    """
    starts, k = [j], j
    for _ in range(2):
        i = k - 1
        while i >= 0 and matchmask[i] in " \t\n":
            i -= 1
        if i < 0 or matchmask[i] != "(":
            break
        starts.append(i)
        k = i
    for s in starts:
        m = call_re.match(matchmask, s)
        if m and m.start(3) == j:
            return m
    return None


# A GCC identifier's characters (`$` included -- see `_token_re`), and the
# whitespace `_prev` steps over.
_IDENT_CHARS = frozenset("abcdefghijklmnopqrstuvwxyz"
                         "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$")
_WS = frozenset(" \t\n\f\v\r")


def _back(mm, i):
    """Indices i-1, i-2, ..., 0 -- skipping every preprocessor directive's
    line, which the preprocessor removes and so is never part of a statement.

    Without the skip, `#endif` directly above `(void)F(x);` made `endif` the
    word before the cast, and a plain call read as "declaration-shaped"
    (#976 review, found by the apostrophe row's own control).
    """
    k = i - 1
    while k >= 0:
        if mm[k] == "\n":
            line_start = mm.rfind("\n", 0, k) + 1
            if mm[line_start:k].lstrip(" \t\f\v").startswith(("#", "%:")):
                k = line_start - 1
                continue
        yield k
        k -= 1


def _prev(mm, i):
    """Index of the last character before `i` that is neither whitespace nor
    on a directive line, or -1."""
    for k in _back(mm, i):
        if mm[k] not in _WS:
            return k
    return -1


def _word_at(mm, k):
    """The identifier whose LAST character is at `k`, or '' if none is."""
    s = k
    while s >= 0 and mm[s] in _IDENT_CHARS:
        s -= 1
    return mm[s + 1:k + 1]


def _skip_ws(mm, i):
    """The first index at or after `i` that is not whitespace."""
    n = len(mm)
    while i < n and mm[i] in _WS:
        i += 1
    return i


# The opening bracket each closing one pairs with.
_OPENER = {")": "(", "]": "[", "}": "{"}


def _group_end(mm, i):
    """Index just past the bracket closing the `(`, `[` or `{` at `i`, or
    None if the text ends first or a bracket inside closes the wrong kind of
    group (`F(a]`). A digraph bracket (`<:` `:>` `<%` `%>`) counts too: the
    code view this reads (`_View.code`) spells each as the bracket it is."""
    stack = []
    for k in range(i, len(mm)):
        c = mm[k]
        if c in "([{":
            stack.append(c)
        elif c in ")]}":
            if not stack or stack[-1] != _OPENER[c]:
                return None
            stack.pop()
            if not stack:
                return k + 1
    return None


# ---- the call contract (#976 audit round 9) ---------------------------------
#
# See "THE CALL RECOGNISER IS A BOUNDED STYLE CONTRACT" in the module
# docstring. Two questions, and a call is one only if both answers are yes:
#
#   * SHAPE: is `F(args)` the entire expression of one of the five statement
#     shapes? Read from the characters directly around it
#     (`_statement_start`).
#   * BLOCK LEVEL: does that statement begin at a statement boundary, with
#     nothing but compound-statement braces between it and the function
#     body's own `{`? Read FORWARD from that `{` (`_brace_scan`).
#
# The second is what makes the first safe to answer locally. Every construct
# that holds statement-shaped text without running it as a statement -- a
# declarator's `(...)` or `[...]`, `sizeof (...)`, an initializer's `{...}`,
# a GNU statement expression `({...})` -- holds it inside a `(`, a `[` or a
# `{` that does not open a compound statement, and the scan sees that
# bracket however the text inside it looks.

# A `{` directly after `KW (...)` opens that statement's body.
_BLOCK_KEYWORDS = frozenset(("if", "while", "for", "switch"))
# Words that cannot stand in shape 3's `T`, or be the `v` of shape 3 or 4:
# each makes the text in front of `=` something other than a declaration's
# specifiers or a lone variable -- `else v = F(1);` is a braceless else body,
# `return v = F(1);` returns an assignment, and `case`, `sizeof` and GCC's
# prefix keywords begin an expression -- so a statement holding one is
# refused.
_NOT_TYPE_WORDS = frozenset((
    "return", "if", "else", "do", "while", "for", "switch", "case",
    "default", "goto", "break", "continue", "typedef",
    "sizeof", "_Alignof", "alignof", "__alignof__", "__alignof",
    "__extension__", "__real__", "__real", "__imag__", "__imag"))


def _opens_block(mm, k, closes, opens):
    """True iff what stands directly before the `{` at `k` makes it a
    COMPOUND STATEMENT's brace -- provided the bracket enclosing it is one,
    which `_brace_scan` checks and this does not.

    This is the rule the former `_closes_block` (removed with the
    recogniser this contract replaced) applied to the `{` that a `}`
    closes, now applied to EVERY `{` on the way down from the body, inside
    the one forward scan -- so "does the `}` before this statement end a
    compound statement?" and "is every bracket around it one?" are answered
    by the same code and cannot disagree. Accepted: a statement boundary before it
    (`;`, or the enclosing `{` -- a bare block), a `}` that itself ended a
    compound statement (`closes`, filled in by the scan as it goes), `else`
    or `do`, or the `)` closing `if (...)`, `while (...)`, `for (...)` or
    `switch (...)` (`opens` maps every `)` scanned to its `(`). Anything
    else is not: a struct, union or enum tag or keyword, `=` (an
    initializer), a cast's `)` (a compound literal), a `(` (a statement
    expression), a `:` (a label or `case`), or the `)` of any other
    `w(...)` -- a macro, an attribute, a nested function -- whose meaning
    the text does not give. `_closes_block` accepted those last two; the
    contract refuses labels and macros, so this does too.
    """
    p = _prev(mm, k)
    c = mm[p] if p >= 0 else ""
    if c in (";", "{"):
        return True
    if c == "}":
        return closes.get(p, False)
    if c == ")":
        op = opens.get(p)
        return (op is not None
                and _word_at(mm, _prev(mm, op)) in _BLOCK_KEYWORDS)
    return _word_at(mm, p) in ("else", "do")


def _brace_scan(mm, lo, pos):
    """-> (open, closes) for the text from the function body's `{` at `lo`
    up to `pos`, read FORWARD; None if its brackets do not nest (a `)`
    closing a `[`, or the body itself closing before `pos`).

    `open` is every bracket still open at `pos`, outermost -- the body --
    first, each as (index, compound). `compound` is True only for a `{`
    that `_opens_block` accepts AND whose enclosing bracket is itself
    compound; never for a `(` or a `[`. So everything inside a `(` or a `[`
    is non-compound however it looks: the `{` of a GNU statement expression
    `({ ... })`, and an `if (F(1)) {}` inside one inside a declarator's array
    bound, which reads exactly like shape 2 to anything that only looks at
    the characters around the call (#976 audit round 9). `closes` maps every
    `}` scanned to whether it ended a compound statement.

    A loop over the text, not recursion, so no depth of nesting can raise.
    """
    stack = [(lo, True)]
    closes, opens = {}, {}
    for k in range(lo + 1, pos):
        c = mm[k]
        if c == "{":
            stack.append((k, stack[-1][1]
                          and _opens_block(mm, k, closes, opens)))
        elif c in "([":
            stack.append((k, False))
        elif c in ")]}":
            if len(stack) < 2 or mm[stack[-1][0]] != _OPENER[c]:
                return None
            at, compound = stack.pop()
            if c == "}":
                closes[k] = compound
            elif c == ")":
                opens[k] = at
    return stack, closes


def _is_name(w):
    """True iff `w` is an identifier that may be a word of shape 3's `T`,
    or the `v` of shape 3 or 4."""
    return bool(w) and not w[0].isdigit() and w not in _NOT_TYPE_WORDS


def _assignment_start(mm, eq):
    """Start of `v` in `v = `, or of `T` in `T v = `, where `eq` is the `=`;
    None if anything else stands in front of it.

    `v` is ONE identifier; `T` is identifier words and `*`, beginning with a
    word (`bool`, `scpi_result_t`, `const char *`,
    `sd_card_manager_settings_t*`). `a[0] = ` and `*p = ` are refused here;
    `a.v = `, `p->v = `, `x = v = ` and `int a, v = ` get as far as the
    block-level check, which refuses them because the statement does not
    begin at `v` -- a `.`, `->`, `=` or `,` stands before it. Either way,
    whatever they store to, `F(args)` is not the whole of a shape-3 or
    shape-4 statement.
    """
    k = _prev(mm, eq)
    v = _word_at(mm, k)
    if not _is_name(v):
        return None
    start, first = k - len(v) + 1, "word"
    k = _prev(mm, start)
    while k >= 0 and (mm[k] == "*" or mm[k] in _IDENT_CHARS):
        if mm[k] == "*":
            start, first = k, "*"
            k = _prev(mm, k)
            continue
        w = _word_at(mm, k)
        if not _is_name(w):
            return None
        start, first = k - len(w) + 1, "word"
        k = _prev(mm, start)
    return start if first == "word" else None


def _statement_start(mm, j, end):
    """Where the statement begins, if the call whose bare name starts at `j`
    and whose argument list ends just before `end` is the ENTIRE expression
    of one of the five shapes; None if it is not.

    Read from the characters directly around the call and nothing further;
    whether that statement is at block level is `_block_level`'s question:

      1  `F(args);`           a `;`, `{` or `}` before, `;` after
         `(void)F(args);`     `( void )` before, `;` after
      2  `if (F(args))`       `if (` before, `)` after
         `if (!F(args))`      `if ( !` before, `)` after
      3  `T v = F(args);`     `=` before (not `==`, `<=`, `+=`, ...), with
      4  `v = F(args);`       `_assignment_start` in front of it; `;` after
      5  `return F(args);`    the word `return` before, `;` after

    So a comma suffix (`v = F(1), w;`), a comparison (`v = F(1) == 0;`), a
    call through the result (`F(1)(2)`), a subscript or member access on it,
    an operator or a cast before it, and an enclosing call (`g(a, F(1))`)
    are none of them.
    """
    n = len(mm)
    a = _skip_ws(mm, end)
    after = mm[a] if a < n else ""
    p = _prev(mm, j)
    before = mm[p] if p >= 0 else ""
    if before in (";", "{", "}"):
        return j if after == ";" else None
    if before == ")":
        q = _prev(mm, p)
        r = _prev(mm, q - 3)
        if (_word_at(mm, q) == "void" and r >= 0 and mm[r] == "("
                and after == ";"):
            return r
        return None
    if before in ("(", "!"):
        q = p if before == "(" else _prev(mm, p)
        if q < 0 or mm[q] != "(" or after != ")":
            return None
        w = _prev(mm, q)
        return w - 1 if _word_at(mm, w) == "if" else None
    if before == "=":
        if p > 0 and mm[p - 1] in "=!<>+-*/%&|^":
            return None
        s = _assignment_start(mm, p)
        return s if s is not None and after == ";" else None
    if _word_at(mm, p) == "return":
        return p - 5 if after == ";" else None
    return None


# What a non-compound bracket on the chain is, for the refusal message.
_NESTED = {
    "(": "a `(` -- an argument list, a condition, a cast, a declarator, or "
         "the `(` of a GNU statement expression `({ ... })`",
    "[": "a `[` -- a subscript or an array declarator's bound",
    "{": "a `{` that does not open a compound statement -- an initializer or "
         "a compound literal, a struct, union or enum body, a GNU statement "
         "expression's `{`, or a block after a label, a `case` or a macro",
}


def _block_level(view, lo, s):
    """None if the statement beginning at `s` is at BLOCK LEVEL in the body
    whose `{` is at `lo`; otherwise why not, completing "it ...".

    Block level: every bracket open at `s` is a compound statement's `{`
    (`_brace_scan`), and `s` follows a `;`, a compound statement's `{` (the
    innermost open bracket, so already checked) or a `}` that ended one.
    After anything else the statement is a braceless `if`/`else`/`do`/
    `for`/`while` body, a labelled or `case` statement, or not a statement
    at all, and the contract refuses every one of those rather than working
    out which it is.
    """
    mm = view.code
    scan = _brace_scan(mm, lo, s)
    if scan is None:
        return ("lies in a function body whose brackets do not pair up "
                "between its `{` and this statement, so where the statement "
                "sits cannot be read")
    stack, closes = scan
    for at, compound in stack:
        if not compound:
            return ("is written in a call shape, but the statement is not at "
                    "BLOCK level: it lies inside %s (line %d), where "
                    "statement-shaped text is not necessarily a statement "
                    "that runs -- the call contract accepts a statement only "
                    "when every bracket around it is a compound statement's "
                    "`{`" % (_NESTED[mm[at]], view.line(at)))
    p = _prev(mm, s)
    if p >= 0 and (mm[p] in (";", "{") or (mm[p] == "}" and closes.get(p))):
        return None
    what = (_word_at(mm, p) or mm[p]) if p >= 0 else "the start of the text"
    return ("is written in a call shape, but its statement does not begin at "
            "a statement boundary (after a `;`, a compound statement's `{`, "
            "or the `}` that ends one): it follows `%s` -- a braceless "
            "`if`/`else`/`do`/`for`/`while` body, a label or `case`, a "
            "declaration's specifiers, or a `}` ending a struct, union, enum, "
            "initializer, compound literal or a block this module does not "
            "recognise -- which the call contract refuses rather than reads"
            % what)


def _not_a_call(view, call, lo):
    """None if `call` -- a `_call_re` match on a tracked name, inside the
    function body whose `{` is at `lo` -- meets the CALL CONTRACT; otherwise
    why not, as a phrase completing "it ...". `view` is the `_View`; what is
    read is its code view (`view.code`: no directive lines, no digraphs).

    The contract (module docstring, "THE CALL RECOGNISER IS A BOUNDED STYLE
    CONTRACT"): the bare name and its argument list are the ENTIRE
    expression of one of five statement shapes --

        F(args);   (void)F(args);   if (F(args))   if (!F(args))
        T v = F(args);   v = F(args);   return F(args);

    -- and that statement is at BLOCK level: it begins after a `;`, a
    compound statement's `{` or the `}` that ends one, and every bracket
    around it, down from the body's `{`, opens a compound statement.

        if (!F(ctx, "CRC",           a call: shape 2, its arguments over two
               cfg)) {               lines, in the body or any block below
        bool ok = F(cfg);            a call: shape 3
        if (x) { return F(1); }      a call: shape 5, in an `if` block
        if (x) return F(1);          REFUSED: a braceless if body
        x = a * F(1);                REFUSED: F(1) is an operand, not the
                                     whole right-hand side
        g(a, F(1));                  REFUSED: an argument of another call,
                                     whatever `g(...)` itself is
        (F)(1);                      REFUSED: a wrapped callee is not `F(`
        v = F(1), w;                 REFUSED: a comma suffix
        (void)sizeof ({ F(1); 0; }); REFUSED: shape 1, but inside a `(`
        extern void h(int a[({ if (F(1)) {} 1; })]);
                                     REFUSED: shape 2, but inside `h(`, a
                                     `[` and a `({`

    It is a contract about how a tracked call is WRITTEN. It does not claim
    that every call C evaluates is recognised -- `x = a * F(1);` is one, and
    is refused -- nor that every accepted call runs (see the module
    docstring's out-of-scope list: `if (0) { F(1); }` is accepted).
    """
    mm = view.code
    j = call.start(3)
    if call.start() != j:
        return ("is a PARENTHESIZED callee (`(F)(x)` or `((F))(x)`): C calls "
                "F there exactly as it does for `F(x)`, but the call contract "
                "accepts only the bare `F(args)`, so it is refused rather "
                "than read -- write it `F(args)`")
    open_args = call.end() - 1
    end = _group_end(mm, open_args) if mm[open_args] == "(" else None
    if end is None:
        return ("has an argument list whose closing `)` this module cannot "
                "pair with its `(`, so where the call ends cannot be read")
    s = _statement_start(mm, j, end)
    if s is None:
        p, a = _prev(mm, j), _skip_ws(mm, end)
        before = (_word_at(mm, p) or mm[p]) if p >= 0 else ""
        after = mm[a] if a < len(mm) else "the end of the text"
        return ("is not the ENTIRE expression of any statement shape the "
                "call contract accepts -- `F(args);`, `(void)F(args);`, "
                "`if (F(args))`, `if (!F(args))`, `T v = F(args);`, "
                "`v = F(args);` or `return F(args);` -- since `%s` stands "
                "directly before it and `%s` directly after its arguments. An "
                "operand, an argument, a declaration, an unevaluated context "
                "and a macro's argument all look like this, and the contract "
                "refuses them all rather than telling them apart"
                % (before, after))
    return _block_level(view, lo, s)


def _in_directive(matchmask, j):
    """True iff offset `j` lies on a preprocessor directive's logical line.

    `matchmask` is joined (so a continued `#define` is one line) and has
    comment newlines blanked (so `#define X /*\\n*/ F` -- one directive to
    the compiler, a comment being one space -- is one line here too). A
    directive begins with `#` or with its digraph `%:`, which GCC accepts in
    every GNU mode: with `#` alone, `%:define ARM() F(x)` inside a body was
    one "call" however many times it expanded (#976 review).
    """
    line_start = matchmask.rfind("\n", 0, j) + 1
    return matchmask[line_start:j].lstrip(" \t\f\v").startswith(("#", "%:"))


def _outside_brackets(code, j):
    """True iff offset `j` of the code view (`_View.code`: no comments, no
    literals, no directive lines, digraphs spelled as brackets) lies outside
    every `{`, `(` and `[` -- at FILE scope, where a prototype stands.

    "No function body this module can find" is NOT that: inside the body of
    a function the declarator grammar cannot read, `fn is None` too. Counted
    rather than paired, so it costs a C-speed `str.count`; brackets that do
    not balance before `j` leave it inside something, and it is refused."""
    return all(code.count(o, 0, j) == code.count(c, 0, j)
               for o, c in (("{", "}"), ("(", ")"), ("[", "]")))


Occurrence = namedtuple("Occurrence", "kind name line offset function why")
# kind is one of: "definition", "prototype", "call", "unclassified",
# "multiply-claimed". `line` is PHYSICAL, in the original text; `offset` is
# the original-text offset of the name token; `function` is the function
# whose body contains it (None at file scope); `why` says why an
# unclassified or multiply-claimed occurrence is one.


def classify_occurrences(src, name):
    """Every raw occurrence of `name`, each classified exactly once.

    A PARTITION, not an allowlist. Each occurrence is claimed by the
    definition recogniser, the (file-scope) prototype recogniser or the call
    recogniser -- or by none, and is then "unclassified", or by more than
    one, and is then "multiply-claimed". Nothing is special-cased as
    "expected, ignore": `#define ALIAS F` (or `%:define`), `&F`, `p = F;`,
    `foo(F)`, a call-shaped occurrence with no enclosing function this module
    can find, a block-scope declaration (with or without an
    `__attribute__((...))` or `__typeof__(...)` in its specifiers), an
    unevaluated operand (`sizeof F(x)`), a member access (`p->F(x)`), and
    any construct nobody has thought of yet all land in "unclassified" by
    the same route -- no recogniser explained them. A call-shaped occurrence
    inside a body is a call only where it meets the call contract
    (`_not_a_call`: the entire expression of one of five statement shapes,
    at block level); every other one is unclassified, including some calls
    the compiler does make (`x = a * F(1);`) -- refused, never guessed.

    Raises `AmbiguousDefinition` on two or more definitions of `name` in the
    compiler's view, exactly as `one_definition` does in the as-written one.
    """
    view = _view(src)
    mm = view.matchmask
    defs = {m.start(3) for m in _named_def_re(name).finditer(mm)}
    if len(defs) > 1:
        raise AmbiguousDefinition(
            "%d definitions answer to %r; refusing to choose" % (len(defs), name))
    protos = {m.start(3) for m in _named_proto_re(name).finditer(mm)}
    call_re = _call_re(name)
    out = []
    for raw in raw_occurrences(src, name):
        j = raw.joined_offset
        fn = enclosing(view.spans, j)

        def emit(kind, why=None):
            out.append(Occurrence(kind, name, raw.line, raw.offset, fn, why))

        if _in_directive(mm, j):
            # Checked FIRST, and never claimable: a name in a macro's
            # replacement list is counted once here however many times the
            # macro is expanded, and an alias hides every later use of it.
            emit("unclassified", "sits in a preprocessor directive (an alias, "
                 "a macro body or a conditional), where how often -- or "
                 "whether -- it reaches the compiler is not visible")
            continue
        claims, why = [], None
        if j in defs:
            claims.append("definition")
        # A prototype only at FILE scope -- outside every bracket, not merely
        # outside every body this module found. Inside the body of a function
        # whose definition the declarator grammar cannot read (K&R, a
        # leading attribute, an anonymous struct parameter), `fn` is None as
        # well, and a line there that begins `return F(`, `do F(` or
        # `__extension__ F(` is prototype-SHAPED: the prefix grammar takes the
        # keyword for a type word. It was claimed as a prototype, so an arm
        # inside such a wrapper was accounted clean, counted by no census and
        # passed `check()` and `check_stream()` (#976 audit round 9 review).
        if (j in protos and fn is None
                and _outside_brackets(view.code, j)):
            claims.append("prototype")
        call = _call_at(call_re, mm, j)
        if call is None:
            why = ("is not followed by `(`, so it is not a call: a bare "
                   "reference, an address-of, an argument or an alias, none of "
                   "which this module can count")
        elif fn is None:
            # NOT "a call outside any function": a declaration this module's
            # declarator grammar does not read lands here too, and saying
            # only "no function body" sent a reader looking for a stray call
            # when the line was an ordinary prototype (#976 review).
            why = ("is call-shaped but lies inside no function body this "
                   "module can find, and is neither a definition nor a "
                   "prototype its declarator grammar reads at FILE scope "
                   "(outside every bracket) -- a declaration with a LEADING "
                   "`__attribute__((...))`, two attribute prefixes or three "
                   "wrapping parens; a file-scope initializer that calls it; "
                   "or a statement inside a function whose own definition "
                   "that grammar cannot read (a K&R definition, say, where "
                   "`return F(x);` is prototype-shaped) -- so there is "
                   "nothing to attribute it to")
        else:
            # The body's own `{`: the root of the bracket chain the call
            # contract walks down to this statement (`_brace_scan`).
            lo = next((s for _, s, e in view.spans if s < j < e), 0)
            why = _not_a_call(view, call, lo)
            if why is None:
                claims.append("call")
        if len(claims) == 1:
            emit(claims[0])
        elif claims:
            emit("multiply-claimed", "claimed as %s at once" % " AND ".join(claims))
        else:
            emit("unclassified", why)
    return out


def _lines(view, offsets):
    return ", ".join("line %d" % view.line_of_orig(o) for o in offsets) or "nowhere"


def _definition_view_problems(view, name, occurrences):
    """The as-written definition matcher and the compiler's view must agree.

    Every caller locates a definition with `find_definitions`/
    `one_definition`, which read the text AS WRITTEN. If a splice-created
    comment disables the copy they find while a copy with a spliced name is
    the live one, they report ONE unambiguous definition -- the dead one --
    and every occurrence in the compiler's view is still classified, because
    the dead one's name is inside a comment there. Only comparing the two
    views catches that, so it is compared: same definitions, same name-token
    offsets, or a problem.
    """
    seen = sorted(o.offset for o in occurrences if o.kind == "definition")
    named = _named_def_re(name)
    as_written = []
    for m in definition_re(name).finditer(view.raw_matchmask()):
        n = named.match(view.raw_matchmask(), m.start())
        as_written.append(n.start(3) if n else m.start())
    as_written.sort()
    if seen == as_written:
        return []
    return ["%s: the definition matcher reading the text AS WRITTEN (what "
            "find_definitions/one_definition answer) finds it at %s, but the "
            "compiler's view -- backslash-newline splices joined first -- finds "
            "it at %s. Whichever a caller trusts may be text the compiler never "
            "builds, so this refuses to choose (#1066)."
            % (name, _lines(view, as_written), _lines(view, seen))]


def account_occurrences(src, name):
    """-> [problem, ...] for `name` in `src`; EMPTY means every occurrence
    the compiler would see is explained by exactly one recogniser, and the
    as-written definition matcher agrees with the compiler's view.

    Problems are REPORTED, not raised, so callers can accumulate them the way
    `scpi_sd_arm_path.py` accumulates everything else. The one exception is
    two or more definitions, which raises `AmbiguousDefinition` exactly as
    `one_definition` always has -- callers already catch it.

    Each problem names `name` and a PHYSICAL line in `src`.
    """
    view = _view(src)
    occurrences = classify_occurrences(src, name)
    problems = []
    for o in occurrences:
        where = " (in %s())" % o.function if o.function else ""
        if o.kind == "unclassified":
            problems.append(
                "%s: line %d%s: an occurrence no recogniser accounts for -- it "
                "%s. Refusing rather than reading past it: an unrecognised "
                "spelling is how live code escaped this check in every audit "
                "round so far (#976)." % (name, o.line, where, o.why))
        elif o.kind == "multiply-claimed":
            problems.append(
                "%s: line %d%s: MULTIPLY CLAIMED -- %s. The categories are a "
                "partition by design, so this is a defect in the classifier, "
                "and an occurrence two recognisers both explain is one neither "
                "can be trusted about." % (name, o.line, where, o.why))
    problems.extend(_definition_view_problems(view, name, occurrences))
    return problems


_CASES = [
    # (name, source, expect) where expect is "one", "none" or "ambiguous"
    ("the ordinary one-line definition", "static bool F(void)\n{\n}\n", "one"),
    ("the return type on its own line", "static bool\nF(void)\n{\n}\n", "one"),
    ("an __attribute__ between type and name -- live on shipped source as "
     "`bool __attribute__((weak)) DRV_SDSPI_GetCID(...)`",
     "bool __attribute__((weak)) F(int a)\n{\n}\n", "one"),
    ("a comment WITH NEWLINES inside the signature (#976 round 4): the "
     "compiler sees whitespace, so this matcher must too",
     "static bool /*\n * why\n */ F(void)\n{\n}\n", "one"),
    ("an INDENTED definition is still a definition -- C does not care about "
     "the column, and anchoring at zero hid a live replacement from every "
     "caller at once (#976 round 5)",
     "  static bool F(void)\n{\n}\n", "one"),
    ("an __attribute__ on its OWN line, between the type and the name",
     "static bool\n__attribute__((weak))\nF(void)\n{\n}\n", "one"),
    ("an indented disabled original plus an indented live copy is still TWO",
     "#if 0\n  static bool F(void)\n{\n}\n#endif\n"
     "  static bool F(void)\n{\n}\n", "ambiguous"),
    ("a COLUMN-ZERO original plus an INDENTED live copy is TWO -- the exact "
     "round-5 bypass",
     "#if 0\nstatic bool F(void)\n{\n}\n#endif\n"
     " static bool F(void)\n{\n}\n", "ambiguous"),
    ("a prototype is not a definition", "static bool F(void);\n", "none"),
    ("a prototype followed by its definition is still ONE",
     "static bool F(void);\nstatic bool F(void)\n{\n}\n", "one"),
    ("a call inside a condition is not a definition",
     "void b(void)\n{\n    if (F(x)) {\n    }\n}\n", "none"),
    ("the name mentioned in a comment is not a definition",
     "/* see F( below */\nstatic bool G(void)\n{\n}\n", "none"),
    ("two definitions are REFUSED, not resolved",
     "#if 0\nstatic bool F(void)\n{\n}\n#endif\nstatic bool F(void)\n{\n}\n",
     "ambiguous"),
    ("...in MIXED styles too, which is how round 2's fix was evaded",
     "#if 0\nstatic bool F(void)\n{\n}\n#endif\nstatic bool\nF(void)\n{\n}\n",
     "ambiguous"),
    ("...and with a comment hiding one of them, which is round 4's",
     "#if 0\nstatic bool F(void)\n{\n}\n#endif\n"
     "static bool /*\n * why\n */ F(void)\n{\n}\n", "ambiguous"),
    ("the name wrapped in one level of redundant parentheses -- "
     "`static bool (F)(void)` declares the same function as "
     "`static bool F(void)` (C11 6.7.6) -- the shape the #976 round-6 audit "
     "used to reproduce the bypass, by rewriting the real "
     "`SD_ArmOrRefuseWithCleanup(scpi_t *ctx)` declarator this way (not how "
     "shipped source is actually written; the compiler accepts either)",
     "static bool (F)(void)\n{\n}\n", "one"),
    ("a disabled original plus a PARENTHESIZED live replacement is still "
     "TWO -- the exact bypass this rule was missing (#976 audit round 6): "
     "`_PREFIX` cannot span a `(`, so without name-wrap support only the "
     "disabled copy matched and AmbiguousDefinition never fired",
     "#if 0\nstatic bool F(void)\n{\n}\n#endif\n"
     "static bool (F)(void)\n{\n}\n", "ambiguous"),
    ("a parenthesized CALL inside a condition is still not a definition -- "
     "the name-wrap addition must not turn a call into one",
     "void b(void)\n{\n    if ((F)(x)) {\n    }\n}\n", "none"),
    ("a DOUBLY-parenthesized CALL inside a condition is still not a "
     "definition -- the two-level wrap must not widen the false-positive "
     "window the single-level one already had to avoid",
     "void b(void)\n{\n    if (((F))(x)) {\n    }\n}\n", "none"),
    ("a `//` comment spanning TWO physical lines via a trailing backslash, "
     "sitting between the return type and the name, is still ONE break to "
     "the compiler -- the round-4 shape reached through a spliced line "
     "comment instead of a block comment (#976 audit round 6)",
     "static bool // why \\\n1;\nF(void)\n{\n}\n", "one"),
    # #976 audit round 6 review: the FIRST version of this rule supported
    # only one level of wrapping parens, which left `((F))` reproducing the
    # exact bypass it was meant to close -- an ambiguous pair silently bound
    # to the dead copy, one paren away. Two levels are supported now (see
    # `_NAME_WRAP_OPEN`'s comment); these two rows pin that the SECOND level
    # is not itself narrow the same way the first was.
    ("the name wrapped in TWO levels of redundant parentheses is still ONE "
     "legal way to declare it -- `((F))(void)` (C11 6.7.6 applies "
     "recursively: a parenthesized declarator is itself a declarator)",
     "static bool ((F))(void)\n{\n}\n", "one"),
    ("a disabled original plus a DOUBLY-parenthesized live replacement is "
     "still TWO -- the one-level fix's own bypass, one paren deeper",
     "#if 0\nstatic bool F(void)\n{\n}\n#endif\n"
     "static bool ((F))(void)\n{\n}\n", "ambiguous"),
    ("a single newline is allowed inside the wrap, on either side of the "
     "name, matching `_BREAK`'s tolerance everywhere else in this file -- "
     "refusing a definition over formatting is how a live replacement gets "
     "silently outvoted by a disabled one",
     "static bool (\nF\n)(void)\n{\n}\n", "one"),
    # THREE levels are explicitly UNSUPPORTED BY THE MATCHER -- recorded
    # here rather than left an unstated assumption. The single-definition
    # case fails CLOSED (safe: "not found", not a wrong answer). The
    # ambiguous-pair case, AT THIS LEVEL, still does not: `one_definition`
    # alone rebinds it to the disabled copy. The fail-closed refusal this
    # comment used to say was missing now exists one layer up: every caller
    # runs `account_occurrences` before trusting `one_definition`, and the
    # live `(((F)))` token is claimed by no recogniser, so it is refused --
    # pinned by `_accounting_self_test`'s row 2, not by this table.
    ("three levels of wrapping parentheses are NOT supported -- fails "
     "closed (none), which is safe for a single definition",
     "static bool (((F)))(void)\n{\n}\n", "none"),
]


def _account(src, name="F"):
    """-> (occurrences or None, problems), `AmbiguousDefinition` folded in."""
    try:
        return classify_occurrences(src, name), account_occurrences(src, name)
    except AmbiguousDefinition as exc:
        return None, ["AmbiguousDefinition: %s" % exc]


def _kinds(occurrences):
    if occurrences is None:
        return None
    return [(o.kind, o.line) for o in occurrences]


# The real files every caller accounts, and the names each accounts. Read
# only if present (this module's self-test must not need the repo root);
# no COUNTS are pinned -- a new, ordinary call site is not a failure -- only
# that everything there is explained, and that there is something to explain.
_HERE = os.path.dirname(os.path.abspath(__file__))
_REAL_CONTROL = (
    ("firmware/src/services/SCPI/SCPIStorageSD.c",
     ("SD_ArmOrRefuseWithCleanup", "SD_ArmOrRefuse", "SD_ClaimOrRefuse",
      "SCPI_StorageSDFormat", "sd_card_manager_SetFormatPending")),
    ("firmware/src/services/SCPI/SCPIInterface.c",
     ("SCPI_StartStreamingClaimed",
      "sd_card_manager_UpdateSettingsForStreamingLog",
      "sd_card_manager_TryClaim")),
)

# Shared prologue: F defined at lines 1-4, a function g opening at 5-6, so
# the statement under test is always PHYSICAL line 7.
_F_THEN_G = "static bool F(int x)\n{\n    return x;\n}\nvoid g(void)\n{\n"


def _accounting_self_test():
    """Rows for occurrence accounting. -> ([failure, ...], checks run)."""
    bad = []
    ran = [0]

    def expect(why, cond):
        ran[0] += 1
        if not cond:
            bad.append(why)

    # ---- splice(): the phase-2 contract the rest stands on ----------------
    src = "a\\\nb \\\\\nc\\ \t\nd"
    joined, omap = splice(src)
    expect("splice() joined %r, expected 'ab \\\\cd' (ISO splice, one "
           "backslash left of a doubled pair, GCC's backslash-SPACE form)"
           % joined, joined == "ab \\cd")
    expect("splice()'s map must have len(joined)+1 entries, the last being "
           "len(src)", len(omap) == len(joined) + 1 and omap[-1] == len(src))
    expect("splice()'s map must point each kept character at itself",
           all(src[omap[j]] == joined[j] for j in range(len(joined))))

    # ---- 1. the live hole: #if 0 original + DOUBLE-attribute replacement --
    two_attr = ("#if 0\nstatic bool F(void)\n{\n    return 0;\n}\n#endif\n"
                "static bool __attribute__((noinline)) __attribute__((unused)) "
                "F(void)\n{\n    return 1;\n}\n")
    try:
        fooled = one_definition(two_attr, "F") is not None
    except AmbiguousDefinition:
        fooled = False
    expect("VACUITY: the definition matcher ALONE must still read the "
           "double-attribute pair as one (dead) definition -- otherwise the "
           "next row proves nothing about accounting", fooled)
    occs, probs = _account(two_attr)
    expect("an #if 0 original plus a live DOUBLE-__attribute__ replacement "
           "must never account clean; got %r" % probs, probs != [])
    expect("...and the refusal must point at the LIVE copy's physical line "
           "7; got %r" % probs, any("line 7" in p for p in probs))

    # ---- 2. #if 0 original + TRIPLE-parenthesized replacement --------------
    three = ("#if 0\nstatic bool F(void)\n{\n    return 0;\n}\n#endif\n"
             "static bool (((F)))(void)\n{\n    return 1;\n}\n")
    occs, probs = _account(three)
    expect("an #if 0 original plus a live (((F))) replacement must never "
           "account clean; got %r" % probs, probs != [])

    # ---- 3. a parenthesized callee is REFUSED (was: IS a call) ------------
    # FLIPPED by the call contract (#976 audit round 9). `(F)(x)` calls F
    # exactly as `F(x)` does, and this row used to pin both occurrences as
    # calls. The contract accepts only the bare `F(args)`: a wrapped callee
    # is the entire expression of none of the five shapes, so both are
    # refused -- still FOUND as call-shaped (`_call_at`), still attributed to
    # g, and reported for what they are, not as "not followed by `(`".
    occs, probs = _account(_F_THEN_G + "    (F)(1);\n    if (((F))(2)) {\n"
                           "    }\n}\n")
    expect("(F)(1) and ((F))(2) inside g() are REFUSED as parenthesized "
           "callees, attributed to g; got %r / %r" % (_kinds(occs), probs),
           _kinds(occs) == [("definition", 1), ("unclassified", 7),
                            ("unclassified", 8)]
           and all(o.function == "g" for o in occs[1:])
           and len(probs) == 2 and all("PARENTHESIZED" in p for p in probs))

    # ---- 4. an alias fails closed, at file scope or inside a body ---------
    for label, src in (
            ("at file scope", "static bool F(int x)\n{\n    return x;\n}\n"
             "#define ALIAS F\nvoid g(void)\n{\n    ALIAS(1);\n}\n"),
            ("inside a body", _F_THEN_G + "#define ALIAS F\n    ALIAS(1);\n}\n"),
    ):
        occs, probs = _account(src)
        expect("`#define ALIAS F` %s must leave F's definition classified and "
               "the alias line UNCLASSIFIED; got %r" % (label, _kinds(occs)),
               _kinds(occs) is not None and _kinds(occs)[0] == ("definition", 1)
               and [k for k, _ in _kinds(occs)[1:]] == ["unclassified"]
               and any("preprocessor directive" in p for p in probs))
    # The row the directive check is the ONLY defence for: a macro whose
    # body CALLS F, defined inside a function. Call-shaped, inside a body --
    # without the check it is one "call", however many times it expands.
    occs, probs = _account(_F_THEN_G + "#define ARM() F(1)\n    ARM();\n"
                           "    ARM();\n}\n")
    expect("a macro BODY calling F is unclassified, never one call standing "
           "for every expansion; got %r" % _kinds(occs),
           _kinds(occs) == [("definition", 1), ("unclassified", 7)])

    # ---- 5. references that are not calls fail closed ---------------------
    for label, stmt in (("a bare reference", "p = F;"),
                        ("an address-of", "p = &F;"),
                        ("a bare argument", "foo(F);"),
                        ("a call through a dereference", "(*F)(1);"),
                        ("sizeof of the name", "n = sizeof F;"),
                        ("a block-scope declaration", "extern bool F(int x);")):
        occs, probs = _account(_F_THEN_G + "    %s\n}\n" % stmt)
        expect("%s (`%s`) must be UNCLASSIFIED at line 7; got %r"
               % (label, stmt, _kinds(occs)),
               _kinds(occs) == [("definition", 1), ("unclassified", 7)]
               and len(probs) == 1 and "line 7" in probs[0])

    # A call-shaped occurrence with no enclosing function is not auto-claimed.
    occs, probs = _account("static bool F(int x)\n{\n    return x;\n}\n"
                           "static int v = F(1);\n")
    expect("a call-shaped occurrence at FILE scope must be unclassified; got "
           "%r" % _kinds(occs),
           _kinds(occs) == [("definition", 1), ("unclassified", 5)]
           and any("no function body" in p for p in probs))

    # ...and the statement keywords that may precede a real call. FLIPPED by
    # the call contract (#976 audit round 9): all four were pinned as calls,
    # and all four are refused now. `return F(1);` is shape 5 but a
    # BRACELESS `if` body -- it follows `)`, not a statement boundary;
    # `else F(2);` and `do F(3);` are braceless else and do bodies; and
    # `return (F)(4);` has a wrapped callee. The same statements inside
    # `{ }` are calls: see the block-level vacuity rows in "12. (d)".
    occs, probs = _account(_F_THEN_G.replace("void g(void)", "bool g(int y)")
                           + "    if (y)\n        return F(1);\n    else\n"
                           "        F(2);\n    do F(3); while (0);\n"
                           "    return (F)(4);\n}\n")
    expect("a braceless `if (y) return F()`, `else F()`, `do F()` and "
           "`return (F)()` are all REFUSED (were: calls); got %r / %r"
           % (_kinds(occs), probs),
           _kinds(occs) == [("definition", 1), ("unclassified", 8),
                            ("unclassified", 10), ("unclassified", 11),
                            ("unclassified", 12)] and len(probs) == 4)

    # A file-scope prototype is a prototype.
    occs, probs = _account("static bool F(int x);\n"
                           "static bool F(int x)\n{\n    return x;\n}\n")
    expect("a prototype plus its definition account clean; got %r"
           % _kinds(occs),
           _kinds(occs) == [("prototype", 1), ("definition", 2)]
           and probs == [])
    # ...and ONLY at file scope (#976 audit round 9 review). `fn is None`
    # also holds inside the body of a function whose definition the
    # declarator grammar cannot read, and a line there beginning
    # `__extension__ F(`, `do F(` or `return F(` is prototype-SHAPED. Claimed
    # as prototypes, an arm inside such a wrapper was accounted clean and
    # counted by no census. The VACUITY half proves the prototype grammar
    # still matches all three, so it is the file-scope rule that refuses them.
    for label, head in (
            ("a K&R definition", "static bool K(y)\n    int y;\n{\n"),
            ("a leading attribute",
             "__attribute__((unused)) static bool K(int y)\n{\n"),
            ("an anonymous struct parameter",
             "static bool K(struct { int a; } *y)\n{\n")):
        src = ("static bool F(int x)\n{\n    return x;\n}\n" + head
               + "    __extension__ F(2);\n    do F(3); while (0);\n"
               "    return F(1);\n}\n")
        shaped = len(_named_proto_re("F").findall(compiler_view(src).matchmask))
        expect("VACUITY: inside a body behind %s the prototype grammar must "
               "still read all three statements as prototype-shaped; got %d"
               % (label, shaped), shaped == 3)
        occs, probs = _account(src)
        expect("inside a body behind %s, `__extension__ F()`, `do F()` and "
               "`return F()` are UNCLASSIFIED, never prototypes; got %r / %r"
               % (label, _kinds(occs), probs),
               _kinds(occs) is not None
               and [k for k, _ in _kinds(occs)] == ["definition"]
               + ["unclassified"] * 3 and len(probs) == 3)

    # `$` is an identifier character to GCC: `my$F(1)` calls `my$F`, and
    # `F$x` is not F either. Neither is an occurrence of F.
    occs, probs = _account(_F_THEN_G + "    my$F(1);\n    p = F$x;\n}\n")
    expect("`my$F` and `F$x` are other identifiers to GCC, not occurrences "
           "of F; got %r" % _kinds(occs),
           _kinds(occs) == [("definition", 1)] and probs == [])

    # ---- 6. comments and literals are not occurrences at all --------------
    occs, probs = _account(_F_THEN_G + "    /* F(1); */\n    // F(2);\n"
                           "    log(\"F(3)\");\n    c = 'F';\n}\n")
    expect("F inside a comment, a string or a char literal is no occurrence; "
           "got %r" % _kinds(occs),
           _kinds(occs) == [("definition", 1)] and probs == [])

    # ---- 7. physical lines, across and inside a splice --------------------
    occs, probs = _account(_F_THEN_G + "    int a = 1 + \\\n        2;\n"
                           "    p = F;\n}\n")
    expect("an occurrence AFTER a spliced line is reported at its PHYSICAL "
           "line 9, not the joined line 8; got %r" % probs,
           _kinds(occs) == [("definition", 1), ("unclassified", 9)]
           and "line 9" in probs[0])
    occs, probs = _account("static bool FOO(int x)\n{\n    return x;\n}\n"
                           "void g(void)\n{\n    p = FO\\\nO;\n    FO\\\nO(1);\n"
                           "}\n", "FOO")
    expect("an identifier SPLIT by a backslash-newline is one token to the "
           "compiler -- a bare reference at physical line 7, a call at 9; "
           "got %r" % _kinds(occs),
           _kinds(occs) == [("definition", 1), ("unclassified", 7),
                            ("call", 9)])

    # ---- splice-created comments (#1066) and GCC's backslash-SPACE --------
    created = _F_THEN_G + "    /\\\n/ F(1);\n}\n"
    expect("VACUITY: the AS-WRITTEN mask must still show the call a splice "
           "comments out -- that is the #1066 gap this view closes",
           "F(1)" in mask(created))
    occs, probs = _account(created)
    expect("a `//` CREATED by a splice swallows the call in the compiler's "
           "view; got %r" % _kinds(occs),
           _kinds(occs) == [("definition", 1)] and probs == [])
    occs, probs = _account(_F_THEN_G + "    // why \\ \n    F(1);\n}\n")
    expect("GCC joins backslash-SPACE-newline too, so the `//` comment "
           "swallows the next line; got %r" % _kinds(occs),
           _kinds(occs) == [("definition", 1)] and probs == [])

    # ...and the dead-copy shape that ONLY the two-view comparison catches:
    # as written, the one definition is the copy a splice-created comment
    # disables; the live copy's declarator is split, so as written it is no
    # definition at all. Every occurrence in the compiler's view is claimed.
    dead_copy = ("/\\\n/ old: \\\nstatic bool F(void)\n{\n    return 0;\n}\n"
                 "static bool \\\nF(void)\n{\n    return 1;\n}\n")
    as_written = one_definition(dead_copy, "F")
    expect("VACUITY: as written, one_definition must answer the DEAD copy "
           "(line 3) -- otherwise this row is not the hole",
           as_written is not None
           and dead_copy.count("\n", 0, as_written.start()) + 1 == 3)
    occs, probs = _account(dead_copy)
    expect("the as-written and compiler views disagreeing about WHICH text "
           "defines F must be reported; got %r" % probs,
           _kinds(occs) == [("definition", 8)] and len(probs) == 1
           and "line 3" in probs[0] and "line 8" in probs[0])

    # ---- the multiply-claimed guard: now UNREACHABLE by construction ------
    # CHANGED by the call contract (#976 audit round 9), from
    # "multiply-claimed" to "definition". `else F(x) {` (not valid C) is
    # definition-shaped to the declarator grammar, and the recogniser the
    # contract replaced ALSO read it as a call after `else`, so this row
    # pinned the partition guard firing. Under the contract no token can be
    # claimed twice: a definition needs `{` right after its parameter list,
    # every call shape needs `;` or the `if`'s `)` right after its
    # arguments, and a prototype is claimed only at file scope, where no
    # call is. So only the definition claims it now: the partition holds, and
    # the multiply-claimed branch stays as a defensive guard no input here
    # reaches. The line is still refused where it matters -- the arm-path
    # census counts `F(` there and accounting does not, and the two must
    # agree; and a second "definition" of a pinned name raises
    # AmbiguousDefinition.
    occs, probs = _account("void g(void)\n{\n    else F(x) {\n    }\n}\n")
    expect("`else F(x) {` is claimed by exactly ONE recogniser, the "
           "definition grammar -- the call contract no longer also claims it "
           "(was: MULTIPLY CLAIMED); got %r / %r" % (_kinds(occs), probs),
           _kinds(occs) == [("definition", 3)])

    # Two definitions still RAISE, as `one_definition` always has.
    occs, probs = _account("#if 0\nstatic bool F(void)\n{\n}\n#endif\n"
                           "static bool F(void)\n{\n}\n")
    expect("two definitions must still raise AmbiguousDefinition",
           occs is None and "refusing to choose" in probs[0])

    # ---- 9. #976 review: a call is recognised; everything else is not ----
    # Every row in this first list was classified AS A CALL by the
    # `_declaration_shaped` that preceded the round-8 recogniser (it
    # recognised declarations and called everything else a call) -- or, for
    # `%:`, by `_in_directive`, which knew only `#`. All stay refused under
    # the call contract: none is the entire expression of an accepted shape.
    for label, stmt in (
            ("an attribute-prefixed block-scope declaration",
             "__attribute__((unused)) bool F(int x);"),
            ("a __typeof__-prefixed block-scope declaration",
             "__typeof__(bool) F(int x);"),
            ("a declarator list after a function-pointer declarator",
             "bool (*p)(void), F(int x);"),
            ("an unevaluated sizeof operand", "(void)sizeof(F(1));"),
            ("a bare sizeof operand", "n = sizeof F(1);"),
            ("a member call through `.`", "hooks.F(1);"),
            ("a member call through `->`", "p->F(1);"),
            ("a member call through ` -> `", "p -> F(1);"),
            ("a macro body behind the `%:` digraph", "%:define ARM() F(1)")):
        occs, probs = _account(_F_THEN_G + "    %s\n}\n" % stmt)
        expect("%s (`%s`) must be UNCLASSIFIED at line 7; got %r"
               % (label, stmt, _kinds(occs)),
               _kinds(occs) == [("definition", 1), ("unclassified", 7)]
               and len(probs) == 1 and "line 7" in probs[0])
    # ...and the calls the round-8 recogniser had to recognise, one per
    # context it named, as its vacuity guard. Under the call contract only
    # `(void)F(1);` is still one of the five shapes, and it stays a call.
    # Every other row FLIPPED from "call" to "unclassified" (#976 audit round
    # 9) and is kept, with its reason, rather than deleted: each is a call
    # GCC makes, written in a style the contract does not accept. The
    # contract's own vacuity guard is "12. (d)" below.
    for stmt in ("(void)F(1);",):
        occs, probs = _account(_F_THEN_G + "    %s\n}\n" % stmt)
        expect("`%s` is a call at line 7 (shape 1); got %r / %r"
               % (stmt, _kinds(occs), probs),
               _kinds(occs) == [("definition", 1), ("call", 7)]
               and probs == [])
    for stmt, reason in (
            ("ok = (bool)F(1);", "a cast other than `(void)` is no shape"),
            ("if (x) F(1);", "a braceless if body is not at block level"),
            ("g(a, F(1));", "an argument of another call"),
            ("x = a * F(1);", "an operand, not the whole right-hand side"),
            ("y = c ? 0 : F(1);", "an operand of `?:`"),
            ("ok = !F(1) && x;", "an operand of `&&`"),
            ("while (!F(1)) { }",
             "a `while` condition is no shape; only an `if` condition is"),
            ("ok = __builtin_expect(F(1), 1);",
             "an argument of another call")):
        occs, probs = _account(_F_THEN_G + "    %s\n}\n" % stmt)
        expect("`%s` is REFUSED at line 7 (was: a call) -- %s; got %r / %r"
               % (stmt, reason, _kinds(occs), probs),
               _kinds(occs) == [("definition", 1), ("unclassified", 7)]
               and len(probs) == 1 and "line 7" in probs[0])
    # A directive line is removed by the preprocessor, so it is never "the
    # word before" a call: `#endif` above `(void)F(1);` once made `endif`
    # the word before the cast and a plain call read as a declaration.
    occs, probs = _account(_F_THEN_G + "#ifdef TRACE\n    trace(1);\n#endif\n"
                           "    (void)F(1);\n}\n")
    expect("a call on the line after `#endif` is still a call; got %r / %r"
           % (_kinds(occs), probs),
           _kinds(occs) == [("definition", 1), ("call", 10)] and probs == [])
    # An apostrophe in `#if 0` prose must not mask the code after it: GCC
    # ends the unterminated literal at the end of its line (a warning), and
    # `mask()` used to run on to the next `'` in the file.
    occs, probs = _account(_F_THEN_G + "#if 0\nit's the old path\n;\n#endif\n"
                           "    F(1);\n}\n")
    expect("a call after an apostrophe in `#if 0` prose is SEEN, at its "
           "line 11; got %r / %r" % (_kinds(occs), probs),
           _kinds(occs) == [("definition", 1), ("call", 11)] and probs == [])
    masked = mask("x = 'a;\ny = 1;\n")
    expect("mask() ends an unterminated literal at its newline, keeping "
           "length and lines; got %r" % masked,
           masked == "x =    \ny = 1;\n")

    # ---- 10. #976 audit round 8 -------------------------------------------
    # (a) An unevaluated operand the BACKWARD walk before round 8 could not
    #     reach. It examined only the word directly before the call or
    #     before an enclosing `(`, so anything else between `sizeof` and the
    #     call hid it -- a prefix operator, a compound literal's `{` (the walk
    #     stopped there as if a block began), an enclosing call, a subscript
    #     -- and each read as the claim it replaced. GCC makes none of these
    #     calls (measured: 0 each). Round 8 read the operand forward; the call
    #     contract (round 9) refuses every one, because none is the entire
    #     expression of an accepted shape.
    # (b) A parenthesized declarator. A `(` directly before the name was
    #     proof of a call whatever stood before the `(`, so
    #     `extern bool (F(int));` -- a block-scope DECLARATION, C11 6.7.6's
    #     parenthesized declarator, the shape `_NAME_WRAP_OPEN` accepts for
    #     definitions -- read as the claim. Refused, with its `*` and
    #     parameter twins: no accepted shape has a `(`, a `*` or a type word
    #     directly before the callee.
    for label, stmt in (
            ("sizeof through `!`, no parentheses at all", "(void)sizeof !F(1);"),
            ("sizeof through a compound literal's `{`",
             "(void)sizeof((bool[]){F(1)});"),
            ("sizeof of a compound literal", "(void)sizeof (bool[]){F(1)};"),
            ("sizeof through `!` and a cast", "n = sizeof !(bool)F(1);"),
            ("sizeof of an enclosing call", "n = sizeof g(F(1));"),
            ("sizeof of a subscript", "n = sizeof arr[F(1)];"),
            ("sizeof of a parenthesized array's subscript",
             "n = sizeof (arr)[F(1)];"),
            ("a word directly before `!` -- a macro, `#define SZ sizeof`",
             "(void)SZ !F(1);"),
            ("a parenthesized block-scope declarator", "extern bool (F(int x));"),
            ("a parenthesized pointer declarator",
             "extern bool (*(F(int x)));"),
            ("a parenthesized declarator after an attribute",
             "__attribute__((unused)) bool (F(int x));"),
            ("a pointer declarator after `__typeof__(T)`",
             "__typeof__(bool) *F(int x);"),
            ("a parameter declarator in a block-scope prototype",
             "extern void h(int a, bool (F(int x)));"),
            # ...and the same construction's two remaining blind spots: an
            # attribute or `__typeof__` holding an operator, and a struct
            # body whose `}` read as a statement's end.
            ("a parenthesized declarator after an attribute holding `+`",
             "__attribute__((aligned(8 + 8))) bool (F(int x));"),
            ("a pointer declarator after `__typeof__` holding `+`",
             "__typeof__(1 + 1) *F(int x);"),
            ("a declaration after a struct body", "struct s { int a; } F(int x);"),
            ("a parenthesized declarator after a struct body",
             "struct s { int a; } (F(int x));"),
            ("a pointer declarator after a struct body",
             "struct s { int a; } *F(int x);")):
        occs, probs = _account(_F_THEN_G + "    %s\n}\n" % stmt)
        expect("%s (`%s`) must be UNCLASSIFIED at line 7; got %r"
               % (label, stmt, _kinds(occs)),
               _kinds(occs) == [("definition", 1), ("unclassified", 7)]
               and len(probs) == 1 and "line 7" in probs[0])
    # ...and their vacuity guard, which was: an operand ends where C ends it
    # (`sizeof(int) - F(1)` calls F -- measured), a `(` or `!` before the
    # callee is still a call wherever the statement cannot be a declaration,
    # and a `}` that ends a compound statement still ends the statement. The
    # last four rows -- a `}` ending an if, else, bare or switch block, then
    # `F(1);` -- are still calls under the contract (shape 1 after a `}`
    # that ends a compound statement). The first nine FLIPPED from "call" to
    # "unclassified" (#976 audit round 9): each is a call GCC makes, and
    # none is the entire expression of an accepted shape.
    for stmt in ("if (x) { y = 1; } F(1);",
                 "if (x) { } else { y = 1; } F(1);", "{ y = 1; } F(1);",
                 "switch (x) { default: break; } F(1);"):
        occs, probs = _account(_F_THEN_G + "    %s\n}\n" % stmt)
        expect("`%s` is a call at line 7 (shape 1 after a compound "
               "statement's `}`); got %r / %r" % (stmt, _kinds(occs), probs),
               _kinds(occs) == [("definition", 1), ("call", 7)]
               and probs == [])
    for stmt, reason in (
            ("n = sizeof(int) - F(1);", "an operand of `-`"),
            ("n = sizeof x + F(1);", "an operand of `+`"),
            ("ok = g(!F(1));", "an argument of another call"),
            ("x = g(F(1));", "an argument of another call"),
            ("return g(F(1));", "an argument of another call"),
            ("if (g(F(1))) { }",
             "an argument of the call that IS the condition"),
            ("(void)(bool[]){F(1)};", "inside a compound literal's `{`"),
            ("x = (F(1));",
             "a parenthesized right-hand side is not `v = F(args);`"),
            ("x = a - -F(1);", "an operand of unary `-`")):
        occs, probs = _account(_F_THEN_G + "    %s\n}\n" % stmt)
        expect("`%s` is REFUSED at line 7 (was: a call) -- %s; got %r / %r"
               % (stmt, reason, _kinds(occs), probs),
               _kinds(occs) == [("definition", 1), ("unclassified", 7)]
               and len(probs) == 1 and "line 7" in probs[0])
    # FLIPPED too (#976 audit round 9): F(1) was a call, as the operand of
    # the binary `+` at which `sizeof !F(2)` ends. It is still an operand --
    # of that `+` -- so the contract refuses it along with F(2).
    occs, probs = _account(_F_THEN_G + "    n = sizeof !F(2) + F(1);\n}\n")
    expect("in `sizeof !F(2) + F(1)` BOTH are refused: F(2) is sizeof's "
           "operand and F(1) the binary `+`'s (was: F(1) a call); got %r"
           % _kinds(occs),
           _kinds(occs) == [("definition", 1), ("unclassified", 7),
                            ("unclassified", 7)])
    # (c) Phase 2 applied TWICE. `splice()` joins `\\` + newline ONCE and
    #     leaves one backslash; the compiler's view then masked the joined
    #     text as if it were raw, read that backslash as a splice again, and
    #     ran the comment -- or an unterminated literal -- on over the next
    #     line: code GCC builds (measured), gone from accounting.
    for label, mid in (("`// x \\\\`", "    // x \\\\\n"),
                       ("a Windows path, `// see C:\\temp\\\\`",
                        "    // see C:\\temp\\\\\n"),
                       ("an unterminated literal, `#define Q 'x\\\\`",
                        "#define Q 'x\\\\\n")):
        occs, probs = _account(_F_THEN_G + mid + "\n    p = F;\n}\n")
        expect("%s above a BLANK line must not swallow the line after it -- "
               "`p = F;` is live at line 9; got %r" % (label, _kinds(occs)),
               _kinds(occs) == [("definition", 1), ("unclassified", 9)])
    joined = "// x \\\n    code;\n"
    expect("line_comment_end(already_spliced=True) must end the comment at "
           "the newline after a leftover backslash",
           line_comment_end(joined, 0, already_spliced=True)
           == joined.index("\n"))
    expect("...while on RAW text that backslash IS a splice and the comment "
           "runs on -- the mode is what differs, not the characters",
           line_comment_end(joined, 0) == len(joined) - 1)
    lit = "'x\\\n    code;\n"
    expect("literal_end(already_spliced=True) must end an unterminated "
           "literal at the newline after a leftover backslash",
           literal_end(lit, 0, already_spliced=True) == lit.index("\n"))

    # ---- 11. the round-8 fix's own review (#976) ---------------------------
    # (a) A directive line inside an unevaluated operand. The first round-8
    #     forward reader ran on the matchmask, where the `)` of `#define RP )`
    #     closed the operand early and the claim passed; the backward walk it
    #     replaced skipped directive lines and had REFUSED it -- a regression
    #     this fix introduced. The code view (`_View.code`) blanks every
    #     directive line; the call contract reads that view, and refuses
    #     these regardless (each F(1) is an operand). GCC makes none of these
    #     calls.
    for label, body in (
            ("a `)` on a directive line inside the operand",
             "    n = sizeof (0 +\n#define RP )\n        F(1));\n"),
            ("a `;` on a directive line between `sizeof` and its operand",
             "    (void)sizeof\n#define SEMI ;\n        (0 + F(1));\n"),
            ("a `]` on a directive line inside a subscript operand",
             "    n = sizeof arr[0 +\n#define RB ]\n        F(1)];\n")):
        occs, probs = _account(_F_THEN_G + body + "}\n")
        expect("%s must leave the call UNCLASSIFIED at line 9; got %r"
               % (label, _kinds(occs)),
               _kinds(occs) == [("definition", 1), ("unclassified", 9)])
    # (b) GCC's prefix keywords take a cast-expression as `!` does, and a
    #     digraph is a bracket: neither may end an operand early (GCC makes
    #     none of these calls, measured). The last two already held.
    for label, stmt in (
            ("`__extension__` before a unary `-`",
             "n = sizeof __extension__ -F(1);"),
            ("`__real__` before a unary `+`", "n = sizeof __real__ +F(1);"),
            ("`__imag` before a unary `-`", "n = sizeof __imag -F(1);"),
            ("`__extension__` after `__alignof__`",
             "n = __alignof__ __extension__ -F(1);"),
            ("`__extension__` before a cast",
             "n = sizeof __extension__ (int) - F(1);"),
            ("a digraph subscript", "n = sizeof arr<:F(1):>;"),
            ("a digraph compound literal", "n = sizeof (bool<:1:>)<%F(1)%>;"),
            ("`__extension__` before `!`", "n = sizeof __extension__ !F(1);"),
            ("`__extension__` before `(`",
             "n = sizeof __extension__ (F(1));")):
        occs, probs = _account(_F_THEN_G + "    %s\n}\n" % stmt)
        expect("%s (`%s`) must be UNCLASSIFIED at line 7; got %r"
               % (label, stmt, _kinds(occs)),
               _kinds(occs) == [("definition", 1), ("unclassified", 7)]
               and len(probs) == 1 and "line 7" in probs[0])
    # ...and the calls GCC DOES make in those neighbourhoods (measured).
    # FLIPPED from "call" to "unclassified" by the call contract (#976 audit
    # round 9): each F(1) is an operand or a subscript, not the entire
    # expression of an accepted shape -- evaluated, and refused all the same.
    for stmt, reason in (
            ("x = __extension__ -F(1);", "an operand of unary `-`"),
            ("n = sizeof sizeof (int) - F(1);", "an operand of `-`"),
            ("x = arr<:F(1):>;", "a subscript")):
        occs, probs = _account(_F_THEN_G + "    %s\n}\n" % stmt)
        expect("`%s` is REFUSED at line 7 (was: a call) -- %s; got %r / %r"
               % (stmt, reason, _kinds(occs), probs),
               _kinds(occs) == [("definition", 1), ("unclassified", 7)]
               and len(probs) == 1 and "line 7" in probs[0])
    # (c) No depth of nesting may crash the reader -- it refuses, it does not
    #     raise. The first round-8 reader recursed, and raised at ~1000.
    try:
        occs, probs = _account(_F_THEN_G + "    n = " + "sizeof " * 3000
                               + "F(1);\n}\n")
        deep = _kinds(occs)
    except RecursionError:
        deep = "RecursionError"
    expect("3000 nested `sizeof` must be read without raising, and refused; "
           "got %r" % (deep,),
           deep == [("definition", 1), ("unclassified", 7)])

    # ---- 12. #976 audit round 9: the call contract --------------------------
    # The recogniser rounds 8 and 9 kept patching inferred "is this call
    # evaluated?" from local punctuation and subtracted the exceptions it
    # knew. Round 9 found two more, and an external review of the design
    # found no completion criterion for it, so it was replaced by a bounded
    # contract (module docstring): five accepted statement shapes, at block
    # level, and everything else refused.
    #
    # (a) Round 9's live finding: the claim replaced by a prototype whose
    #     parameter's array bound CALLS it, then `if (0) {` where the claim's
    #     own `{` was. A VLA bound at function-prototype scope is never
    #     evaluated (C11 6.7.6.2p5), so the claim is never made -- and the
    #     recogniser this replaced read it as a call (the operand of `+`).
    # (b) The other classes the external review verified against GCC: in
    #     each the call is never executed, or is not the entire expression of
    #     an accepted shape. `half` says which half of the contract must
    #     refuse it -- "ENTIRE" the shape, "BLOCK level" the bracket chain.
    #     The last three are shape 1 or 2 EXACTLY, inside a GNU statement
    #     expression: only the forward bracket scan (`_brace_scan`) can
    #     refuse those, and pinning the half proves it is the one that does.
    for label, stmt, line, half in (
            ("a VLA bound at function-prototype scope is never evaluated -- "
             "C11 6.7.6.2p5 (round 9's live finding)",
             "extern void claim_contract(int a[1 + F(1)]); if (0) {\n    }",
             7, "ENTIRE"),
            ("a parenthesized parameter declarator after an array parameter",
             "extern void h(int a[1], int (F(int)));", 7, "ENTIRE"),
            ("`__builtin_classify_type`'s operand",
             "int x = __builtin_classify_type(F(1));", 7, "ENTIRE"),
            ("`__builtin_has_attribute`'s operand",
             "int x = __builtin_has_attribute(F(1), aligned);", 7, "ENTIRE"),
            ("`__builtin_choose_expr`'s unchosen operand",
             "int x = __builtin_choose_expr(1, 0, F(1));", 7, "ENTIRE"),
            ("a comma expression inside `__builtin_object_size`",
             "int x = __builtin_object_size((F(1), (void *)0), 0);", 7,
             "ENTIRE"),
            ("a designated initializer the next one overrides",
             "int a[1] = { [0] = F(1), [0] = 0 };", 7, "ENTIRE"),
            ("a macro spelling `sizeof`, then `SZ(F(1))`",
             "#define SZ sizeof\n    int x = SZ(F(1));", 8, "ENTIRE"),
            ("a short-circuited operand", "int x = 0 && F(1);", 7, "ENTIRE"),
            ("a statement expression inside `sizeof`",
             "(void)sizeof ({ F(1); 0; });", 7, "BLOCK level"),
            ("a statement expression inside `_Generic`",
             "int x = _Generic(0, int: 0, default: ({ F(1); 0; }));", 7,
             "BLOCK level"),
            ("`if (F(1)) {}` inside a statement expression inside an array "
             "bound inside a declarator",
             "extern void h(int a[({ if (F(1)) {} 1; })]);", 7,
             "BLOCK level")):
        occs, probs = _account(_F_THEN_G + "    %s\n}\n" % stmt)
        expect("%s (`%s`) must be UNCLASSIFIED at line %d, refused by the "
               "%s half of the contract; got %r / %r"
               % (label, stmt, line, half, _kinds(occs), probs),
               _kinds(occs) == [("definition", 1), ("unclassified", line)]
               and len(probs) == 1 and "line %d" % line in probs[0]
               and half in probs[0])
    # (c) The contract's own refusals. Each is a statement a compiler runs,
    #     refused because it is not written in an accepted shape at block
    #     level -- which is the design: five accepted shapes, and a human for
    #     everything else. A comma suffix; a comparison after the call; a
    #     store to anything but one plain variable; a compound assignment; a
    #     condition that is more than the call; a braceless body; a label or
    #     `case`; `else if`; a statement expression GCC does run; and a block
    #     a macro or a label opens (the last two accepted by `_closes_block`,
    #     refused now that one rule decides every brace).
    bool_g = _F_THEN_G.replace("void g(void)", "bool g(int y)")
    for label, stmt in (
            ("a comma suffix after an assignment", "v = F(1), w;"),
            ("a comma suffix after a return", "return F(1), w;"),
            ("a comma suffix after a bare call", "F(1), w;"),
            ("a comparison after the call", "v = F(1) == 0;"),
            ("a declarator list", "int a, v = F(1);"),
            ("a store through a pointer", "*p = F(1);"),
            ("a store to a member", "s.v = F(1);"),
            ("a store to an element", "a[0] = F(1);"),
            ("a chained assignment", "x = v = F(1);"),
            ("a compound assignment", "v += F(1);"),
            ("a doubly parenthesized condition", "if ((F(1))) { }"),
            ("a compound condition", "if (F(1) && v) { }"),
            ("a braceless `for` body", "for (;;) F(1);"),
            # Shape 1's own characters (`;` before, `;` after) inside the
            # header's `(`: only the block-level half's `(` refuses it, and
            # no other row pinned that -- a mutation treating `(` as compound
            # passed every row here (#976 audit round 9 review).
            ("a `for` header's condition", "for (;F(1);) { }"),
            ("a label", "lbl: F(1);"),
            ("a `case` label", "switch (v) { case 1: F(1); }"),
            ("a block after a `case` label",
             "switch (v) { case 1: { F(1); } }"),
            ("`else if` -- a braceless else body",
             "if (v) { } else if (F(1)) { }"),
            ("a GNU statement expression GCC does run", "v = ({ F(1); 0; });"),
            ("a block a macro opens", "FOREACH(v) { F(1); }"),
            ("a statement after a block a macro opens",
             "FOREACH(v) { } F(1);"),
            ("a statement after a block a label opens", "lbl: { } F(1);")):
        occs, probs = _account(bool_g + "    %s\n}\n" % stmt)
        expect("%s (`%s`) must be UNCLASSIFIED at line 7; got %r / %r"
               % (label, stmt, _kinds(occs), probs),
               _kinds(occs) == [("definition", 1), ("unclassified", 7)]
               and len(probs) == 1 and "line 7" in probs[0])
    # (d) VACUITY -- refusing everything would satisfy every row above. Each
    #     shape (both spellings of 1 and of 2, a pointer type in 3, and
    #     arguments split over lines the way `SCPIStorageSD.c` writes
    #     FORmat's arm) is a call at the top of the body AND one block down
    #     in every compound-statement context the contract names -- an `if`
    #     block, an `else` block, a `do` block, a bare block, a `while` and a
    #     `for` block -- two blocks down, and after a sibling block's `}`.
    #     Block level is not "the top of the function".
    shapes = ("F(1);", "(void)F(1);", "if (F(1)) {\n    }",
              "if (!F(1)) {\n    }", "bool v = F(1);",
              "sd_card_manager_settings_t *p = F(1);", "v = F(1);",
              "return F(1);",
              "if (!F(1, \"FORmat\",\n           2)) {\n    }",
              "bool v = F(1,\n             2);")
    contexts = (("at the top of the body", "    %s\n", 7),
                ("in an `if` block", "    if (y) {\n        %s\n    }\n", 8),
                ("in an `else` block",
                 "    if (y) {\n    } else {\n        %s\n    }\n", 9),
                ("in a `do` block",
                 "    do {\n        %s\n    } while (0);\n", 8),
                ("in a bare block", "    {\n        %s\n    }\n", 8),
                ("in a `while` block",
                 "    while (y) {\n        %s\n    }\n", 8),
                ("in a `for` block", "    for (;;) {\n        %s\n    }\n", 8),
                ("two blocks down",
                 "    if (y) {\n        {\n            %s\n        }\n    }\n",
                 9),
                ("after a sibling block's `}`",
                 "    if (y) {\n    }\n    %s\n", 9))
    for shape in shapes:
        for where, frame, line in contexts:
            occs, probs = _account(bool_g + frame % shape + "}\n")
            expect("VACUITY: `%s` %s is a call at line %d; got %r / %r"
                   % (shape.replace("\n", " "), where, line, _kinds(occs),
                      probs),
                   _kinds(occs) == [("definition", 1), ("call", line)]
                   and probs == [])
    # (e) No depth of nesting may crash the bracket scan: it is a loop.
    try:
        occs, probs = _account(_F_THEN_G + "    " + "{ " * 3000 + "F(1); "
                               + "} " * 3000 + "\n}\n")
        deep = _kinds(occs)
    except RecursionError:
        deep = "RecursionError"
    expect("VACUITY: `F(1);` 3000 bare blocks down is still a call, read "
           "without raising; got %r" % (deep,),
           deep == [("definition", 1), ("call", 7)])

    # ---- 8. control: the real files are explained in full -----------------
    for rel, names in _REAL_CONTROL:
        path = os.path.join(_HERE, os.pardir, os.pardir, *rel.split("/"))
        if not os.path.isfile(path):
            continue
        with open(path, "r", encoding="utf-8") as fh:
            text = fh.read()
        for name in names:
            occs, probs = _account(text, name)
            expect("CONTROL: %s in %s must account clean; got %r"
                   % (name, rel, probs), probs == [])
            expect("CONTROL: %s in %s has no occurrences at all -- the control "
                   "is vacuous" % (name, rel), bool(occs))
    return bad, ran[0]


def self_test():
    """Prove the rule on canned sources. Device-free, import-free.

    Every case is a shape that produced a real defect, or that a fix for one
    of those defects could plausibly have broken. The negative cases matter
    as much as the positive ones: a matcher that refuses everything would
    satisfy the ambiguity rows and be useless.
    """
    bad = []
    for why, src, expect in _CASES:
        try:
            got = "one" if one_definition(src, "F") is not None else "none"
        except AmbiguousDefinition:
            got = "ambiguous"
        if got != expect:
            bad.append("%s: expected %s, got %s" % (why, expect, got))

    # The generic scanner must agree with the named one, or the census and
    # the pin part company again. GROUP 3 is the name -- groups 1 and 2 are
    # `_NAME_WRAP_OPEN`'s own paren-seen markers.
    names = [m.group(3) for m in
             ANY_DEF.finditer(mask_for_match(
                 "bool __attribute__((weak)) A(int x)\n{\n}\n"
                 "static bool\nB(void)\n{\n}\n"))]
    if names != ["A", "B"]:
        bad.append("the generic scanner read %r, expected ['A', 'B']" % names)

    # The generic scanner must ALSO recognise a parenthesized declarator, or
    # ANY_DEF and the named matcher disagree about what a definition is --
    # precisely the class of drift this module exists to end (#976 round 6).
    wrapped_names = [m.group(3) for m in
                      ANY_DEF.finditer(mask_for_match(
                          "static bool (C)(void)\n{\n}\n"))]
    if wrapped_names != ["C"]:
        bad.append("the generic scanner read %r for a parenthesized "
                   "declarator, expected ['C']" % wrapped_names)

    # Comment splicing (#976 round 6): phase 2 deletes a backslash
    # immediately before a newline BEFORE phase 3 recognises comments, so a
    # `//` comment ending in one continues onto the next physical line. A
    # brace that lands there must stay hidden from brace counting, and
    # mask()'s own contract -- length and every newline preserved -- must
    # still hold across the (now multi-physical-line) comment span.
    spliced = "// keep \\\n} tail\n"
    masked_spliced = mask(spliced)
    if len(masked_spliced) != len(spliced):
        bad.append("mask() changed length across a spliced // comment")
    if masked_spliced.count("\n") != spliced.count("\n"):
        bad.append("mask() lost a newline across a spliced // comment")
    if "}" in masked_spliced:
        bad.append("mask() left a brace inside a spliced // comment unmasked")

    # Vacuity guard: a matcher that never matched would pass every "none"
    # row above. One positive row asserted directly, against the real shape.
    if one_definition("static bool F(void)\n{\n    return 1;\n}\n",
                      "F") is None:
        bad.append("the matcher found nothing in an ordinary definition")

    accounting_bad, accounting_ran = _accounting_self_test()
    bad.extend(accounting_bad)

    for b in bad:
        print("  - %s" % b)
    print("cdef self-test: %s (%d failure(s); %d definition-matcher rows, "
          "%d occurrence-accounting checks, plus the scanner and mask checks)"
          % ("FAILED" if bad else "all cases pass", len(bad), len(_CASES),
             accounting_ran))
    return 1 if bad else 0


if __name__ == "__main__":
    import sys
    sys.exit(self_test())
