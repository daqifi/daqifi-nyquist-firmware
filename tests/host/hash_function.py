#!/usr/bin/env python3
"""sha256 of one C function's CODE as the compiler reads it: backslash-newline
splices joined, comments removed, whitespace collapsed (`hashed_code`).

Used by the Makefile to pin a function a host test models, so that any edit to
the real code forces someone to re-read the model. The pin claims exactly one
thing -- "this text is unchanged" -- which is all a build recipe can honestly
know about code it does not execute.

WHY THIS IS A SCRIPT AND NOT A sed PIPELINE (#976 pre-merge audit). The first
version deleted any LINE STARTING WITH `/*`, `*` or `//`.
`*(&cfg->mode) = SD_CARD_MANAGER_MODE_NONE;` is an ordinary pointer-deref
assignment that starts with `*`, so it was deleted before hashing and adding one
left the pin byte-identical. Measured, not argued. A guard whose whole claim is
"any edit forces a review" must not have a spelling of edit it cannot see.

The replacement is STRING-AWARE, which a regex is not: the function being pinned
today contains `LOG_E("SD:%s - could not arm ... %s\\r\\n", ...)`, and a naive
comment strip would corrupt any literal that happened to contain `/*` or `//`.
"""
import hashlib
import os
import re
import sys


# The definition rule lives in ONE place. Five matchers used to answer "what
# is a definition" separately -- this file's, three in
# `tools/lint/scpi_sd_arm_path.py`, and a grep in this directory's Makefile --
# and every audit finding on #976 was a DISAGREEMENT between two of them, never
# a disagreement about C. `tools/lint/cdef.py` carries the rule and its own
# self-test; see its module docstring for the six defects that produced it.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                os.pardir, os.pardir, "tools", "lint"))
import cdef                                              # noqa: E402
from cdef import AmbiguousDefinition, mask, line_comment_end  # noqa: E402,F401


class UnaccountedOccurrence(Exception):
    """An occurrence of the pinned name that no recogniser in `cdef` explains.

    Raised, like `AmbiguousDefinition`, rather than resolved. The shape that
    made it necessary: an `#if 0`-disabled original plus a live replacement
    spelled in a way the definition matcher does not know (two
    `__attribute__((...))` prefixes, three wrapping parens). `one_definition`
    then sees ONE definition -- the dead one -- and reports it unambiguous,
    and the pin digests text the compiler never builds. The live copy's name
    is still there in the text, and nothing claims it, which is what this
    reports (#976).
    """

    def __init__(self, problems):
        Exception.__init__(self, "\n".join(problems))
        self.problems = problems


def require_accounted(text, name):
    """Refuse unless every occurrence of `name` in `text` is accounted for.

    Run BEFORE `cdef.one_definition` is trusted, by both the digest and
    `--find`: a single answer from a matcher that could not see every
    spelling is not a single answer. Two definitions still raise
    `AmbiguousDefinition` (from here or from `one_definition`), unchanged.
    """
    problems = cdef.account_occurrences(text, name)
    if problems:
        raise UnaccountedOccurrence(problems)


def signature_name(signature):
    """The function NAME inside a signature PREFIX like `static bool Foo(`."""
    head = signature.strip().rstrip("(").strip()
    return head.split()[-1] if head.split() else ""


def extract(text, signature):
    """The function's full body, by BALANCED BRACES over masked text.

    Not "up to the first line starting with `}`", which is what this did until
    the #976 audit pointed out that a nested block, a comment or a literal
    carrying such a line truncates the digest to a PREFIX -- after which every
    later edit to the function is invisible to the guard that exists to see
    edits.

    Locating the definition is `cdef`'s job, so this hasher cannot drift from
    the lint that reads the same file. Two definitions raise rather than
    resolve: which one the compiler builds is preprocessor state neither tool
    evaluates, and taking the first is how the pin came to digest dead code.

    Braces are counted on `cdef.mask()`ed text -- the newline-PRESERVING mask,
    because offsets and line structure both matter here -- so a brace inside a
    comment or a string cannot open or close the body. The ORIGINAL text is
    what gets returned and hashed.
    """
    name = signature_name(signature)
    if not name:
        return None
    require_accounted(text, name)
    match = cdef.one_definition(text, name)
    if match is None:
        return None
    masked = mask(text)
    start, open_at = match.start(), match.end() - 1
    depth, i, n = 0, open_at, len(text)
    while i < n:
        ch = masked[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    return None  # unbalanced: refuse rather than hash a prefix


def strip_comments(src, already_spliced=False):
    """Remove C comments, leaving string and char literals intact.

    `already_spliced` says whether `src` has been through `cdef.splice()`,
    exactly as for `cdef.mask()`, and it is passed to both of cdef's boundary
    finders. `hashed_code` joins FIRST and so passes True: phase 2 has run,
    once, and a backslash `\\\\` + newline leaves at a line's end is an
    ordinary character, not a splice to apply again. Stripping that joined
    text in raw mode let `// x \\\\` above a blank line swallow the line
    after the blank -- an early `return true;` GCC builds -- with the digest
    byte-identical to `SD_ARM_HELPER_SHA` (#976 audit round 8). The default
    stays RAW, for which everything below is written.

    A `//` comment ending in a backslash-newline does not end there: C's
    translation phase 2 deletes that backslash and its newline, splicing the
    physical lines together, BEFORE phase 3 even recognises a comment -- so
    the comment actually continues onto (and can swallow) the next physical
    line. A naive scan that stops at the first bare newline hashes the
    swallowed line as ordinary code, so an edit that turns an ordinary
    comment into a line-splicing one -- silently deleting whatever code
    follows it, e.g. a trailing backslash added to an ordinary
    `// release the claim` comment placed just above a call --
    changes what the compiler builds without moving this file's digest
    (#976 audit round 6). `cdef.line_comment_end` is the shared boundary
    finder that already accounts for this splicing for `cdef.mask()`'s own
    callers; reusing it here keeps this file's comment rule and cdef's from
    drifting apart the way the docstring above warns about.
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            # Kept verbatim, ending where `cdef.literal_end` -- the ONE
            # literal rule `cdef.mask()` also uses -- says: at the closing
            # quote, or at the newline that ends one never closed. Running
            # on to the next matching quote instead is how an apostrophe in
            # `#if 0` prose hid live code (#976 review).
            end = cdef.literal_end(src, i, already_spliced)
            out.append(src[i:end])
            i = end
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            end = src.find("*/", i + 2)
            i = n if end < 0 else end + 2
            out.append(" ")
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            i = line_comment_end(src, i, already_spliced)
            out.append(" ")
            continue
        out.append(c)
        i += 1
    return "".join(out)


def hashed_code(body):
    """The text the pin digests: `body` read the way the compiler reads it --
    backslash-newline splices joined FIRST (`cdef.splice`, translation phase
    2) -- then comments removed and whitespace collapsed.

    Joining first is the #976 review's fix. `strip_comments` on RAW text
    looks for a literal `*/`, so a block comment closed by a SPLICED one
    (`*` + backslash + newline + `/`) ran on, for this function, to the next
    literal `*/`, and code the compiler builds in between -- an early
    `return true;` ahead of the arm -- was stripped as if it were comment.
    Measured against the real helper: the digest stayed byte-identical to
    `SD_ARM_HELPER_SHA`. `splice` is applied exactly once, as phase 2 is: a
    backslash left at a line's end by a splice is not spliced again -- which
    is why `strip_comments` is told the text is `already_spliced`; without
    that it re-applied the splice this function had just finished (#976
    audit round 8).
    """
    return re.sub(r"\s+", " ", strip_comments(cdef.splice(body)[0],
                                              already_spliced=True)).strip()


_SELF_TEST_CASES = (
    # (name, source, signature, must_contain, must_not_contain)
    ("a nested brace in column 0 does not truncate the body",
     'static bool F(void)\n{\n    if (x) {\n}\n    int keep = 1;\n    return keep;\n}\n',
     "static bool F(", "keep", None),
    ("a brace inside a comment neither opens nor closes",
     'static bool F(void)\n{\n    /* } { */\n    int keep = 1;\n    return keep;\n}\n',
     "static bool F(", "keep", None),
    ("a brace inside a string literal neither opens nor closes",
     'static bool F(void)\n{\n    log("}");\n    int keep = 1;\n    return keep;\n}\n',
     "static bool F(", "keep", None),
    ("a comment containing a quote does not swallow the rest",
     'static bool F(void)\n{\n    /* don\'t */\n    int keep = 1;\n    return keep;\n}\n',
     "static bool F(", "keep", None),
    # A prototype ahead of the definition is ORDINARY C, not an exotic shape:
    # the pin binds by a signature PREFIX, so without the semicolon test it
    # would hash `Intervening` and then never see a change to the real helper
    # again (#976 review).
    ("a forward declaration does not bind the pin to the next function",
     'static bool F(void);\n'
     'static bool Intervening(void)\n{\n    int decoy = 1;\n    return decoy;\n}\n'
     'static bool F(void)\n{\n    int keep = 1;\n    return keep;\n}\n',
     "static bool F(", "keep", "decoy"),
    ("the signature mentioned in a COMMENT does not bind either",
     '/* see static bool F( below */\n'
     'static bool Intervening(void)\n{\n    int decoy = 1;\n    return decoy;\n}\n'
     'static bool F(void)\n{\n    int keep = 1;\n    return keep;\n}\n',
     "static bool F(", "keep", "decoy"),
    ("comments are removed from what is hashed",
     'static bool F(void)\n{\n    /* SECRET */\n    return 1;\n}\n',
     "static bool F(", "return", "SECRET"),
    # #976 audit round 6: a `//` comment ending in a backslash-newline is
    # spliced onto the next physical line by C's translation phase 2, BEFORE
    # phase 3 even recognises the comment -- so the call right after it is
    # REMOVED from the compiled program, not merely annotated. A stripper
    # that stops at the first bare newline hashes that call as if it were
    # still there, so an edit adding exactly this trailing backslash -- e.g.
    # `// release the claim \` placed just above
    # `sd_card_manager_ReleaseClaim();` -- changes what ships without moving
    # the digest. `return` (on the line after the swallowed call) must
    # survive, proving the splice does not eat the WHOLE rest of the
    # function -- only through its own unspliced terminating newline.
    ("a `//` comment ending in a backslash-newline swallows the next "
     "physical line, removing its call from what gets hashed",
     'static bool F(void)\n{\n    // release the claim \\\n'
     '    G();\n    return 1;\n}\n',
     "static bool F(", "return", "G()"),
    # The identical splice rule has to hold for cdef.mask()'s BRACE COUNTING
    # too, or extract() (which locates the body via cdef.mask()) truncates
    # the digest at a `}` that the compiler never treats as code -- the same
    # class of drift this file's own module docstring warns about, just
    # reached through cdef instead of strip_comments (#976 round 6 twin).
    ("a `}` inside a `//` comment spliced onto the next physical line by a "
     "trailing backslash does not close the body early",
     'static bool F(void)\n{\n    // pretend close \\\n}\n'
     '    int keep = 1;\n    return keep;\n}\n',
     "static bool F(", "keep", None),
    # #976 review: a block comment closed by a SPLICED `*/` (`*` + backslash
    # + newline + `/`) ends THERE for the compiler, and the code after it is
    # built -- here an early exit ahead of the rest of the body. Stripping
    # comments from the RAW body ran the comment on to the next literal
    # `*/` and dropped that code from the digest, which stayed identical.
    ("a block comment closed by a SPLICED `*/` ends where the compiler "
     "ends it, so the code after it is hashed",
     'static bool F(void)\n{\n    /* early out *\\\n/ early(); /* end */\n'
     '    return 1;\n}\n',
     "static bool F(", "early()", None),
    # #976 audit round 8: phase 2 applied TWICE. `hashed_code` joins splices
    # first -- `\\` + newline leaves ONE backslash, exactly as phase 2 does --
    # and then stripped comments from the joined text as if it were raw, so
    # that leftover backslash was spliced again and a `//` comment ending in
    # two backslashes swallowed the line after the blank line below it. GCC
    # builds that line (measured); against the real helper an early
    # `return true;` placed there left the digest byte-identical to
    # `SD_ARM_HELPER_SHA`, while the same return inserted bare moved it. The
    # Windows path is the spelling a person writes by accident.
    ("a `//` comment ending in TWO backslashes ends at the blank line below "
     "it, so the early return after that is hashed",
     'static bool F(void)\n{\n    // x \\\\\n\n    return early();\n'
     '    return 1;\n}\n',
     "static bool F(", "early()", None),
    ("...and so does one ending in a Windows path, `// see C:\\temp\\\\`",
     'static bool F(void)\n{\n    // see C:\\temp\\\\\n\n    return early();\n'
     '    return 1;\n}\n',
     "static bool F(", "early()", None),
    # The LITERAL twin. An unterminated literal ending in `\\` -- only a
    # warning on a directive line -- ran on over the same next line, so the
    # next quote it met was read as its end, every quote after that paired
    # wrongly, and a `/*` inside a real string opened a "comment" that ate
    # live code up to the `*/` inside another string (GCC calls `early()`
    # here: measured).
    ("an unterminated literal ending in TWO backslashes ends at its "
     "newline, so the next line's quotes pair the way the compiler pairs "
     "them and its code is hashed",
     "static bool F(void)\n{\n#define Q 'x\\\\\n\n"
     "    c = '\"'; t = \"/*\"; early(); u = \"*/\";\n    return 1;\n}\n",
     "static bool F(", "early()", None),
)


def self_test():
    """Device-free checks that this guard sees what it claims to see.

    Every case here is a shape that made the guard report a FALSE UNCHANGED
    before it was fixed, or would if the extraction regressed: the whole value
    of a drift pin is that no edit to the pinned code is invisible to it, so
    the cases are about what the extraction KEEPS rather than about the digest.
    """
    bad = []
    for name, src, sig, want, unwanted in _SELF_TEST_CASES:
        body = extract(src, sig)
        if body is None:
            bad.append("%s: extracted nothing" % name)
            continue
        code = hashed_code(body)
        if want not in code:
            bad.append("%s: %r missing from %r" % (name, want, code))
        if unwanted is not None and unwanted in code:
            bad.append("%s: %r should have been stripped" % (name, unwanted))

    if extract("static bool G(void)\n{\n    return 1;\n}\n", "static bool F(") is not None:
        bad.append("a signature that is not present must extract nothing")
    if extract("static bool F(void)\n{\n    return 1;\n", "static bool F(") is not None:
        bad.append("an unbalanced body must refuse rather than hash a prefix")
    # A DECLARATION with no definition anywhere must fail CLOSED. Returning
    # the next function's body would be worse than returning nothing: nothing
    # stops the build, a stranger's digest passes it.
    if extract("static bool F(void);\nstatic bool G(void)\n{\n    return 1;\n}\n",
               "static bool F(") is not None:
        bad.append("a prototype with no definition must extract nothing")
    # TWO definitions must REFUSE, not pick one. The shape below is ordinary:
    # the live definition placed after an `#if 0`-disabled original. Taking
    # the first pins the digest to text the compiler never builds, and every
    # later edit to the live helper is then invisible (#976 pre-merge audit).
    two = ('#if 0\n'
           'static bool F(void)\n{\n    int old = 1;\n    return old;\n}\n'
           '#endif\n'
           'static bool F(void)\n{\n    int live = 1;\n    return live;\n}\n')
    try:
        extract(two, "static bool F(")
        bad.append("two definitions must be REFUSED, not silently resolved")
    except AmbiguousDefinition:
        pass
    # The SAME evasion in the OTHER formatting. Round 1 of the audit closed
    # the one-line pair; round 2 showed the pair could simply be written in
    # two different styles, because the hasher matched a literal prefix while
    # the lint had been taught to accept a split line. One shared rule now
    # answers for both, and this row is what pins that.
    mixed = ('#if 0\n'
             'static bool F(void)\n{\n    int dead = 1;\n    return dead;\n}\n'
             '#endif\n'
             'static bool\nF(void)\n{\n    int live = 1;\n    return live;\n}\n')
    try:
        extract(mixed, "static bool F(")
        bad.append("a one-line and a split-line definition are still TWO")
    except AmbiguousDefinition:
        pass
    # An `__attribute__((...))` between the return type and the name is
    # ordinary and already present in the firmware this pins
    # (`bool __attribute__((weak)) DRV_SDSPI_GetCID(...)`). Absorbing it is
    # what stops the matcher reading `__attribute__` as the function's name.
    attr = 'bool __attribute__((weak)) F(int a)\n{\n    int keep = a;\n    return keep;\n}\n'
    body = extract(attr, "bool F(")
    if body is None or "keep" not in body:
        bad.append("an __attribute__ between type and name must not hide it")
    # ...and one definition preceded by a PROTOTYPE is still unambiguous, so
    # the refusal above must not have been bought by refusing everything.
    one = ('static bool F(void);\n'
           'static bool F(void)\n{\n    int live = 1;\n    return live;\n}\n')
    body = extract(one, "static bool F(")
    if body is None or "live" not in body:
        bad.append("a prototype plus ONE definition must still extract it")

    # OCCURRENCE ACCOUNTING (#976, after round 6). Each shape below is an
    # `#if 0`-disabled original plus a live replacement the definition
    # matcher cannot see, so `one_definition` alone answers the DEAD copy,
    # unambiguously -- the pin then digests text the compiler never builds,
    # and every later edit to the live helper is invisible. The live copy's
    # name is still in the text and no recogniser claims it; that, or the
    # two views disagreeing about which text is the definition, must refuse.
    dead = 'static bool F(void)\n{\n    int dead = 1;\n    return dead;\n}\n'
    live = '\n{\n    int live = 1;\n    return live;\n}\n'
    for why, src in (
            ("two __attribute__((...)) prefixes",
             '#if 0\n' + dead + '#endif\n'
             'static bool __attribute__((noinline)) __attribute__((unused)) '
             'F(void)' + live),
            ("THREE wrapping parens",
             '#if 0\n' + dead + '#endif\nstatic bool (((F)))(void)' + live),
            ("a splice-CREATED comment disabling the copy the matcher finds, "
             "and a live copy whose declarator a splice splits (#1066)",
             '/\\\n/ old: \\\n' + dead + 'static bool \\\nF(void)' + live),
    ):
        try:
            got = extract(src, "static bool F(")
        except (AmbiguousDefinition, UnaccountedOccurrence):
            continue
        bad.append("a dead original plus a live replacement with %s must "
                   "REFUSE; the pin extracted %r" % (why, got))

    for b in bad:
        print("  - %s" % b)
    print("self-test: %s (%d failure(s))"
          % ("FAILED" if bad else "all cases pass", len(bad)))
    return 1 if bad else 0


def main(argv):
    if len(argv) == 2 and argv[1] == "--self-test":
        return self_test()
    if len(argv) == 4 and argv[1] == "--find":
        # PRESENCE only, no digest. Exists so `tests/host/Makefile` can ask
        # "is this function there, exactly once" through the SAME rule that
        # hashes it, instead of carrying a `grep` of its own -- that grep was
        # the FIFTH matcher, and it answered the question differently from the
        # other four (it required the return type and the name to share a
        # line, so a behaviour-neutral reformat failed the build). One rule,
        # five callers.
        path, name = argv[2], argv[3]
        try:
            with open(path, "r", encoding="utf-8", errors="strict") as fh:
                text = fh.read()
        except OSError as exc:
            sys.exit("error: cannot read %s (%s)" % (path, exc))
        try:
            require_accounted(text, name)
            found = cdef.one_definition(text, name)
        except AmbiguousDefinition as exc:
            sys.exit("error: %s in %s" % (exc, path))
        except UnaccountedOccurrence as exc:
            sys.exit("error: %s in %s is not accounted for, so the one "
                     "definition found may not be the live one:\n  %s"
                     % (name, path, "\n  ".join(exc.problems)))
        if found is None:
            sys.exit("error: no definition of %r in %s" % (name, path))
        return 0
    if len(argv) != 3:
        sys.exit("usage: hash_function.py <source> <signature-prefix>\n"
                 "       hash_function.py --find <source> <function-name>\n"
                 "       hash_function.py --self-test")
    path, signature = argv[1], argv[2]
    try:
        with open(path, "r", encoding="utf-8", errors="strict") as fh:
            text = fh.read()
    except OSError as exc:
        sys.exit("error: cannot read %s (%s)" % (path, exc))
    try:
        body = extract(text, signature)
    except AmbiguousDefinition as exc:
        # Reported separately from a drift MISMATCH so the reader is not sent
        # to diff a function that did not change. Two definitions is a
        # question about which one is live, and only a human with the
        # preprocessor state can answer it.
        sys.exit("error: %s in %s -- refusing to hash either. If this is a "
                 "board-variant or #if 0 pair, pin the LIVE one by making its "
                 "signature distinct, or teach this hasher which to take."
                 % (exc, path))
    except UnaccountedOccurrence as exc:
        # Also separate from a drift MISMATCH: nothing may have changed in
        # the function at all -- what failed is this hasher's ability to say
        # WHICH text is the function.
        sys.exit("error: %s in %s is not accounted for -- refusing to hash "
                 "what may be a dead copy:\n  %s"
                 % (signature_name(signature), path,
                    "\n  ".join(exc.problems)))
    if body is None:
        sys.exit("error: no line starting with %r in %s" % (signature, path))
    code = hashed_code(body)
    if not code:
        sys.exit("error: %r extracted to nothing -- refusing to hash it" % signature)
    print(hashlib.sha256(code.encode("utf-8")).hexdigest())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
