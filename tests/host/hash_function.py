#!/usr/bin/env python3
"""sha256 of one C function's CODE, comments removed, whitespace collapsed.

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
import re
import sys


def mask(src):
    """`src` with every comment and literal blanked, LENGTH PRESERVED.

    Length preservation is the point: offsets into the mask are offsets into
    the original, so braces can be counted on text where no brace inside a
    comment or a string can be mistaken for code.
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


class AmbiguousDefinition(Exception):
    """Two or more definitions answer to the same signature.

    Refusing is the only safe answer. Taking the FIRST one lets an ordinary
    arrangement -- the live definition placed after an `#if 0`-disabled
    original, or the `#if defined(NQ3)` board-variant pair this codebase
    already uses elsewhere -- pin the digest to text the compiler never
    builds, after which every edit to the ACTIVE helper is invisible to the
    guard whose whole job is to see edits (#976 pre-merge audit, reproduced
    against the real source: the digest stayed unchanged while the live code
    was replaced wholesale). Taking the LAST is no better; which one is live
    depends on preprocessor state this hasher does not evaluate.
    """


def extract(text, signature):
    """The function's full body, by BALANCED BRACES over masked text.

    Not "up to the first line starting with `}`", which is what this did until
    the #976 audit pointed out that a nested block, a comment or a literal
    carrying such a line truncates the digest to a PREFIX -- after which every
    later edit to the function is invisible to the guard that exists to see
    edits. The function pinned today does not contain one, so the old form was
    right by luck about this input and wrong about its own claim.

    Braces are counted on `mask()`ed text so a brace inside a comment or string
    cannot open or close the body, and the ORIGINAL text is what gets returned
    and hashed.
    """
    masked = mask(text)
    definitions = []
    at = 0
    while True:
        start = text.find(signature, at)
        if start == -1:
            if not definitions:
                return None   # no DEFINITION anywhere: fail closed
            if len(definitions) > 1:
                raise AmbiguousDefinition(
                    "%d definitions answer to %r; refusing to choose"
                    % (len(definitions), signature))
            start, open_at = definitions[0]
            break
        # A match inside a comment or a string literal is not a declaration of
        # anything. `mask()` blanks both, length-preservingly, so a blanked
        # slice is exactly that case -- and binding it to the next `{` would
        # hash whichever function happens to follow the mention.
        if not masked[start:start + len(signature)].strip():
            at = start + 1
            continue
        open_at = masked.find("{", start)
        if open_at == -1:
            return None
        # A FORWARD DECLARATION, not a definition: the signature is terminated
        # by `;` before any body opens. Taking the next `{` anyway binds the
        # pin to an UNRELATED function, and the damage is silent and permanent
        # -- once the pin is updated to that stranger's digest, every later
        # edit to the real helper is invisible to the guard whose entire job is
        # to see edits (#976 review). A parameter list cannot contain `;` in C,
        # so the first one after the signature settles which this is.
        semi = masked.find(";", start)
        if semi != -1 and semi < open_at:
            at = start + len(signature)
            continue
        # A DEFINITION. Do NOT stop here: another may follow, and choosing
        # between two is exactly what this hasher must not do silently.
        definitions.append((start, open_at))
        at = open_at + 1
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


def strip_comments(src):
    """Remove C comments, leaving string and char literals intact."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            quote = c
            out.append(c)
            i += 1
            while i < n:
                out.append(src[i])
                if src[i] == "\\":          # escape: take the next char too
                    if i + 1 < n:
                        out.append(src[i + 1])
                        i += 2
                        continue
                elif src[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            end = src.find("*/", i + 2)
            i = n if end < 0 else end + 2
            out.append(" ")
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            end = src.find("\n", i)
            i = n if end < 0 else end
            out.append(" ")
            continue
        out.append(c)
        i += 1
    return "".join(out)


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
        code = re.sub(r"\s+", " ", strip_comments(body)).strip()
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
    # ...and one definition preceded by a PROTOTYPE is still unambiguous, so
    # the refusal above must not have been bought by refusing everything.
    one = ('static bool F(void);\n'
           'static bool F(void)\n{\n    int live = 1;\n    return live;\n}\n')
    body = extract(one, "static bool F(")
    if body is None or "live" not in body:
        bad.append("a prototype plus ONE definition must still extract it")

    for b in bad:
        print("  - %s" % b)
    print("self-test: %s (%d failure(s))"
          % ("FAILED" if bad else "all cases pass", len(bad)))
    return 1 if bad else 0


def main(argv):
    if len(argv) == 2 and argv[1] == "--self-test":
        return self_test()
    if len(argv) != 3:
        sys.exit("usage: hash_function.py <source> <signature-prefix>\n"
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
    if body is None:
        sys.exit("error: no line starting with %r in %s" % (signature, path))
    code = re.sub(r"\s+", " ", strip_comments(body)).strip()
    if not code:
        sys.exit("error: %r extracted to nothing -- refusing to hash it" % signature)
    print(hashlib.sha256(code.encode("utf-8")).hexdigest())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
