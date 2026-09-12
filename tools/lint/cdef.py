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
"""

import re


class AmbiguousDefinition(Exception):
    """Two or more definitions answer to the same name.

    Raised rather than resolved: which one the compiler builds depends on
    preprocessor state this module does not evaluate. The caller reports it
    separately from a drift MISMATCH, so a reader is not sent to diff a
    function that did not change.
    """


def line_comment_end(src, start):
    """Index of the newline that truly ends a `//` comment beginning at
    `start` (the index of its leading `/`).

    C's translation phase 2 deletes a backslash immediately followed by a
    newline, splicing the two physical lines into one logical line -- BEFORE
    phase 3 even recognises comments. So a `//` comment does not end at a
    newline that phase 2 already erased; it continues onto the next physical
    line, and the next, for as many such splices as appear in a row (#976
    audit round 6). A comment-boundary finder that stops at the first bare
    newline is looking for a boundary the compiler never sees.

    NOT HANDLED, TRACKED SEPARATELY (#1066): a splice can also CREATE the
    `//` marker in the first place -- `/` + `\\` + newline + `/` splices into
    `//` under the same phase-2 rule, and every caller of this function (and
    `mask()`'s / `mask_for_match()`'s `/*` detection, and the equivalent
    checks in `hash_function.strip_comments()` and
    `scpi_wiki_sync._CODE_OR_COMMENT`) looks for a literal, unspliced `//` or
    `/*` at the START of a comment before this function -- or its
    equivalents -- ever runs. Verified live against `SCPIStorageSD.c`: this
    shape deletes the CRC claim guard with `scpi_sd_arm_path.py` reporting
    zero problems. The fix is almost certainly to normalize splices across
    the whole text ONCE, before any comment/token recognition -- not another
    per-call-site patch -- so it is filed rather than done here.
    """
    n = len(src)
    i = start
    while True:
        end = src.find("\n", i)
        if end < 0:
            return n
        if end > 0 and src[end - 1] == "\\":
            i = end + 1
            continue
        return end


def mask(src):
    """`src` with every comment and literal blanked, LENGTH and LINES kept.

    Length preservation is the point: offsets into the mask are offsets into
    the original, so braces can be counted on text where no brace inside a
    comment or a string can be mistaken for code. Newlines survive so `(?m)^`
    still sees the real line structure.
    """
    out = list(src)
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c in ('"', "'"):
            quote = c
            out[i] = " "
            i += 1
            while i < n:
                ch = src[i]
                if ch == "\\" and i + 1 < n:
                    out[i] = out[i + 1] = " "
                    i += 2
                    continue
                out[i] = " " if ch != "\n" else "\n"
                i += 1
                if ch == quote:
                    break
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            end = src.find("*/", i + 2)
            end = n if end < 0 else end + 2
            for j in range(i, end):
                out[j] = "\n" if src[j] == "\n" else " "
            i = end
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            end = line_comment_end(src, i)
            for j in range(i, end):
                out[j] = "\n" if src[j] == "\n" else " "
            i = end
            continue
        i += 1
    return "".join(out)


def mask_for_match(src):
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
    out = list(mask(src))
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
            end = line_comment_end(src, i)
            for j in range(i, end):
                out[j] = " "
            i = end
            continue
        if c in ('"', "'"):
            quote = c
            i += 1
            while i < n:
                if src[i] == "\\" and i + 1 < n:
                    i += 2
                    continue
                if src[i] == quote:
                    i += 1
                    break
                i += 1
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
# Three or more levels remain UNSUPPORTED -- `_CASES` records this
# explicitly rather than leaving it an unstated assumption; nothing in the
# 446-file firmware source tree uses even one level, so the boundary is
# believed inert today, not proven irrelevant. The cost of supporting two
# levels is two extra numbered groups ahead of whatever the caller captures:
# `capture_params=True`'s parameter-list group and `ANY_DEF`'s name group
# both shift from group 1 to group 3. Every reader of either has been
# updated to match.
_WRAP_SPACE = r"[ \t]*(?:\n[ \t]*)?"
_NAME_WRAP_OPEN = (r"(?:(\()" + _WRAP_SPACE
                    + r"(?:(\()" + _WRAP_SPACE + r")?)?")
_NAME_WRAP_CLOSE = (r"(?(2)" + _WRAP_SPACE + r"\))"
                     r"(?(1)" + _WRAP_SPACE + r"\))")


def def_pattern(name, capture_params=False):
    """The definition pattern for ONE named function, as a string.

    When `capture_params` is True, the parameter list is GROUP 3, not group
    1 -- groups 1 and 2 are `_NAME_WRAP_OPEN`'s own paren-seen markers (see
    its comment above).
    """
    inner = r"([^;{]*)" if capture_params else r"[^;{]*"
    return (HEAD + _NAME_WRAP_OPEN + r"\b" + re.escape(name)
            + _NAME_WRAP_CLOSE + r"\s*\(" + inner + r"\)\s*\{")


def definition_re(name, capture_params=False):
    """The compiled definition pattern for one named function."""
    return re.compile(def_pattern(name, capture_params))


# GROUP 3 is the name; groups 1 and 2 are `_NAME_WRAP_OPEN`'s markers (see
# `def_pattern`'s docstring).
ANY_DEF = re.compile(HEAD + _NAME_WRAP_OPEN + r"\b([A-Za-z_]\w*)"
                      + _NAME_WRAP_CLOSE + r"\s*\([^;{]*\)\s*\{")


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
    # THREE levels are explicitly UNSUPPORTED -- recorded here rather than
    # left an unstated assumption. The single-definition case fails CLOSED
    # (safe: "not found", not a wrong answer); the ambiguous-pair case does
    # NOT -- it silently rebinds to the disabled copy, the same class of
    # defect this rule exists to close, one level beyond what it currently
    # reaches. Nothing in the firmware source tree uses even ONE level
    # (checked: zero occurrences across `firmware/src`, third-party
    # excluded), so this boundary is believed inert today, not proven
    # irrelevant -- if a THREE-level form is ever found live, this matcher
    # needs a third nesting level or a fail-closed refusal, not neither.
    ("three levels of wrapping parentheses are NOT supported -- fails "
     "closed (none), which is safe for a single definition",
     "static bool (((F)))(void)\n{\n}\n", "none"),
]


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

    for b in bad:
        print("  - %s" % b)
    print("cdef self-test: %s (%d failure(s))"
          % ("FAILED" if bad else "all cases pass", len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    import sys
    sys.exit(self_test())
