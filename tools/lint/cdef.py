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
            end = src.find("\n", i)
            end = n if end < 0 else end
            for j in range(i, end):
                out[j] = " "
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
    # a block comment. Those are the ones to blank here, so re-walk the
    # SOURCE for comment spans and flatten them; string literals cannot
    # contain a raw newline in C, so they need no such pass.
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
            end = src.find("\n", i)
            i = n if end < 0 else end
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


_PREFIX = r"[A-Za-z_][\w \t\*]*"
_ATTR = r"(?:__attribute__\s*\(\((?:[^()]|\([^()]*\))*\)\)[\w \t\*]*)?"
_BREAK = r"(?:[ \t]*\n[ \t]*)?"
HEAD = r"(?m)^" + _PREFIX + _ATTR + _BREAK


def def_pattern(name, capture_params=False):
    """The definition pattern for ONE named function, as a string."""
    inner = r"([^;{]*)" if capture_params else r"[^;{]*"
    return HEAD + r"\b" + re.escape(name) + r"\s*\(" + inner + r"\)\s*\{"


def definition_re(name, capture_params=False):
    """The compiled definition pattern for one named function."""
    return re.compile(def_pattern(name, capture_params))


ANY_DEF = re.compile(HEAD + r"\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{")


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
    # the pin part company again.
    names = [m.group(1) for m in
             ANY_DEF.finditer(mask_for_match(
                 "bool __attribute__((weak)) A(int x)\n{\n}\n"
                 "static bool\nB(void)\n{\n}\n"))]
    if names != ["A", "B"]:
        bad.append("the generic scanner read %r, expected ['A', 'B']" % names)

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
