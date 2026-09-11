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


def extract(text, signature):
    """The function from its signature line to the closing brace in column 0."""
    lines = text.splitlines(True)
    out, inside = [], False
    for line in lines:
        if not inside and line.startswith(signature):
            inside = True
        if inside:
            out.append(line)
            if line.startswith("}"):
                break
    return "".join(out) if inside else None


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


def main(argv):
    if len(argv) != 3:
        sys.exit("usage: hash_function.py <source> <signature-prefix>")
    path, signature = argv[1], argv[2]
    try:
        with open(path, "r", encoding="utf-8", errors="strict") as fh:
            text = fh.read()
    except OSError as exc:
        sys.exit("error: cannot read %s (%s)" % (path, exc))
    body = extract(text, signature)
    if body is None:
        sys.exit("error: no line starting with %r in %s" % (signature, path))
    code = re.sub(r"\s+", " ", strip_comments(body)).strip()
    if not code:
        sys.exit("error: %r extracted to nothing -- refusing to hash it" % signature)
    print(hashlib.sha256(code.encode("utf-8")).hexdigest())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
