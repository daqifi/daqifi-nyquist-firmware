#!/usr/bin/env python3
"""Extract the #1029 log-budget facts out of the REAL firmware sources.

Issue #1029 -- "Three sd_card_manager.c LOG_E messages are cut before the
operator sees the remedy". The fix shortens three messages so their
worst-case formatted length fits inside what Util/Logger.c will actually
emit. test_1029_sd_card_manager_log_budget.c proves that property by running
the real message texts through a mirror of Logger's truncation logic.

WHY A GENERATOR AND NOT HAND-COPIED CONSTANTS

Every number and every string that test depends on lives in firmware source
that can be edited without anyone thinking about this test:

    LOG_MESSAGE_SIZE                          Util/Logger.h
    vsnprintf's and the clamp's reservations   Util/Logger.c
    SD_CARD_MANAGER_CONF_DIR_NAME_LEN_MAX      sd_card_services/sd_card_manager.h
    SD_CARD_MANAGER_BUCKET_PREFIX / MAX_BUCKET sd_card_services/sd_card_manager.c
    sd_BuildBucketPath's format string          sd_card_services/sd_card_manager.c
    the three LOG_E format strings              sd_card_services/sd_card_manager.c

A hand-copied constant that has drifted keeps PASSING while checking nothing,
which is the failure mode #1000 (the sibling ticket, same defect class) had to
design around. So this script reads all of them out of the real sources at
build time and writes gen_1029_log_budget.h. Anything it cannot find, or finds
more than once, or cannot decode, or cannot ROUND-TRIP back into the exact
bytes that are in the source, is a hard error that fails the BUILD with a
named diagnostic -- never a silent fallback, never a default.

HOW THE THREE MESSAGES ARE ANCHORED

By the STATEMENT THAT FOLLOWS each LOG_E, not by any of the message's own
words:

    site 1  ... writeRefuseReason = SD_REFUSE_BUCKET_NOT_DIR;
    site 2  ... startupDirFull = true;
    site 3  ... return false;          (scoped to sd_card_manager_WaitForCompletion)

Site 3's `return false;` is not unique file-wide, so it carries a SCOPE: a
unique enclosing-function signature, whose body runs to the next `}` in column
0. The anchor must then appear exactly once INSIDE that body. (The trailing
`// Timeout` on that line is not usable as part of the anchor -- comments are
masked out before anchors are searched for, precisely so that prose quoting a
message or a call cannot be matched as code.)

That split is deliberate. Rewording a message must NOT break extraction -- the
whole point of the test is to re-measure whatever text is actually there
against the budget, so a reword that blows the budget has to come back as a
failing ASSERT, not as a build error that reads like a tooling problem. Moving
or duplicating the CALL SITE, on the other hand, invalidates the test's premise
(it no longer knows which LOG_E it is measuring), so each anchor must appear
EXACTLY ONCE or this exits non-zero.

Usage:  gen_1029_log_budget.py [--out gen_1029_log_budget.h] [--root <repo>]
"""

import argparse
import os
import re
import sys

SELF = os.path.basename(__file__)


def die(msg, *hints):
    print("ERROR (%s): %s" % (SELF, msg), file=sys.stderr)
    for h in hints:
        print("       %s" % h, file=sys.stderr)
    sys.exit(1)


# --------------------------------------------------------------------------
# Comment masking
#
# Anchors and LOG_E( are searched for in a COPY of the source whose comment
# bytes have been replaced by spaces (newlines kept, so offsets and line
# numbers are unchanged). Slices for the round-trip check are then taken from
# the ORIGINAL text. Without this, a LOG_E or an anchor mentioned in prose --
# and sd_card_manager.c's comments mention both, including the #1029 comments
# that quote the very messages being extracted -- would be matched as code.
# --------------------------------------------------------------------------
def mask_comments(src):
    out = list(src)
    i, n = 0, len(src)
    state = "code"
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and nxt == "/":
                state = "line"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == "/" and nxt == "*":
                state = "block"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == '"':
                state = "str"
            elif c == "'":
                state = "chr"
            i += 1
        elif state == "line":
            if c == "\n":
                state = "code"
            else:
                out[i] = " "
            i += 1
        elif state == "block":
            if c == "*" and nxt == "/":
                out[i] = out[i + 1] = " "
                state = "code"
                i += 2
                continue
            if c != "\n":
                out[i] = " "
            i += 1
        else:  # "str" / "chr"
            if c == "\\":
                i += 2
                continue
            if (state == "str" and c == '"') or (state == "chr" and c == "'"):
                state = "code"
            i += 1
    return "".join(out)


# --------------------------------------------------------------------------
# C string-literal decoding / encoding
# --------------------------------------------------------------------------
_SIMPLE_ESCAPES = {
    "n": "\n", "r": "\r", "t": "\t", "0": "\0", "a": "\a",
    "b": "\b", "f": "\f", "v": "\v", "\\": "\\", '"': '"', "'": "'", "?": "?",
}
_ENCODE_ESCAPES = {
    "\\": "\\\\", '"': '\\"', "\n": "\\n", "\r": "\\r", "\t": "\\t",
    "\a": "\\a", "\b": "\\b", "\f": "\\f", "\v": "\\v", "\0": "\\0",
}


def decode_c_literal(text, where):
    """`text` includes the surrounding double quotes."""
    if len(text) < 2 or text[0] != '"' or text[-1] != '"':
        die("%s: not a double-quoted literal: %r" % (where, text[:40]))
    body, out, i = text[1:-1], [], 0
    while i < len(body):
        c = body[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        if i + 1 >= len(body):
            die("%s: trailing backslash in literal" % where)
        e = body[i + 1]
        if e not in _SIMPLE_ESCAPES:
            die("%s: escape '\\%s' is not modelled by %s." % (where, e, SELF),
                "Teach decode_c_literal AND _ENCODE_ESCAPES about it (both, or",
                "the round-trip check will reject the result) and re-run.")
        out.append(_SIMPLE_ESCAPES[e])
        i += 2
    return "".join(out)


def encode_c_literal(s):
    out = ['"']
    for ch in s:
        if ch in _ENCODE_ESCAPES:
            out.append(_ENCODE_ESCAPES[ch])
        elif 0x20 <= ord(ch) < 0x7F:
            out.append(ch)
        else:
            die("cannot encode byte 0x%02X back into a C literal" % ord(ch))
    out.append('"')
    return "".join(out)


def parse_literal_group(src, start, macros, where):
    """Parse a run of adjacent string literals -- and macro names that expand to
    string literals -- beginning at `src[start]`.

    Returns (decoded_text, end_offset). Before returning, the decoded pieces are
    RE-ENCODED and compared byte-for-byte against the exact source slice they
    came from. That round trip is what makes an empty or wrong extraction
    impossible to pass silently: if decoder and encoder disagree with the file
    by one byte, no header is emitted at all.
    """
    i, n = start, len(src)
    chunks, traced = [], []
    while i < n:
        if src[i] == '"':
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == '"':
                    break
                j += 1
            if j >= n:
                die("%s: unterminated string literal" % where)
            dec = decode_c_literal(src[i:j + 1], where)
            chunks.append(dec)
            traced.append(("lit", dec))
            i = j + 1
        else:
            m = re.match(r"[A-Za-z_][A-Za-z0-9_]*", src[i:])
            if not m or m.group(0) not in macros:
                break
            name = m.group(0)
            val = macros[name]
            if not (val.startswith('"') and val.endswith('"')):
                break
            chunks.append(decode_c_literal(val, "%s (macro %s)" % (where, name)))
            traced.append(("macro", name))
            i += len(name)
        # Inter-token whitespace, then: does the literal group continue?
        j = i
        while j < n and src[j] in " \t\r\n":
            j += 1
        if j < n:
            nxt = re.match(r"[A-Za-z_][A-Za-z0-9_]*", src[j:])
            if src[j] == '"' or (nxt and nxt.group(0) in macros
                                 and macros[nxt.group(0)].startswith('"')):
                traced.append(("sep", src[i:j]))
                i = j
                continue
        break

    if not chunks:
        die("%s: no string literal found at offset %d" % (where, start))

    raw_slice = src[start:i]
    rebuilt = "".join(encode_c_literal(p) if kind == "lit" else p
                      for kind, p in traced)
    if rebuilt != raw_slice:
        die("%s: round-trip mismatch." % where,
            "source  : %r" % raw_slice[:160],
            "rebuilt : %r" % rebuilt[:160],
            "The extractor cannot faithfully reproduce this literal, so the",
            "bytes it would hand the test are not the bytes the device emits.")
    return "".join(chunks), i


# --------------------------------------------------------------------------
# #define extraction
# --------------------------------------------------------------------------
def read(path):
    if not os.path.isfile(path):
        die("cannot read %s" % path,
            "Has the file moved? Update the paths in main() of %s." % SELF)
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def defines(src):
    out = {}
    for m in re.finditer(
            r"^[ \t]*#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+(.+?)[ \t]*$",
            src, re.M):
        out.setdefault(m.group(1), m.group(2).strip())
    return out


def int_define(macros, name, where):
    if name not in macros:
        die("%s is not #defined in %s." % (name, where),
            "test_1029_sd_card_manager_log_budget.c is built against it.")
    val, seen = macros[name], set()
    while val in macros and val not in seen:      # follow alias chains
        seen.add(val)
        val = macros[val]
    m = re.fullmatch(r"\(?\s*(\d+)\s*[uU]?[lL]*\s*\)?", val)
    if not m:
        die("%s in %s is %r, which is not a plain integer." % (name, where, val),
            "The test uses it as a NUMBER; re-derive before changing it.")
    return int(m.group(1))


def str_define(macros, name, where):
    if name not in macros:
        die("%s is not #defined in %s." % (name, where))
    val = macros[name]
    if not (val.startswith('"') and val.endswith('"')):
        die("%s in %s is %r, not a string literal." % (name, where, val))
    return decode_c_literal(val, "%s in %s" % (name, where))


# --------------------------------------------------------------------------
# The three message sites
# --------------------------------------------------------------------------
SITES = [
    dict(key="BUCKET_NOT_DIR",
         anchor="gSDCardData.writeRefuseReason = SD_REFUSE_BUCKET_NOT_DIR;",
         conv="s",
         what="site 1 -- the bucket name is taken by a non-directory"),
    dict(key="BUCKET_UNUSABLE",
         anchor="gSDCardData.startupDirFull = true;",
         conv="s",
         what="site 2 -- no writable bucket remains"),
    dict(key="WRITES_HANG",
         scope="bool sd_card_manager_WaitForCompletion(uint32_t timeoutMs) {",
         anchor="return false;",
         conv="",
         what="site 3 -- writes hang while reads work"),
]

CONV_RE = re.compile(r"%(?:%|[-+ #0]*[0-9]*(?:\.[0-9]+)?"
                     r"(?:hh|h|ll|l|j|z|t|L)?([diouxXeEfgGaAcspn]))")


def conversion_spec(fmt):
    return "".join(m.group(1) for m in CONV_RE.finditer(fmt) if m.group(1))


def extract_site(raw, masked, macros, site):
    anchor = site["anchor"]
    base, region = 0, masked
    scope = site.get("scope")
    if scope:
        shits = [m.start() for m in re.finditer(re.escape(scope), masked)]
        if len(shits) != 1:
            die("scope %r occurs %d times in sd_card_manager.c (expected exactly 1)."
                % (scope, len(shits)),
                "%s needs that function to exist, once, to bound where it looks" % SELF,
                "for %s's anchor." % site["what"])
        base = shits[0]
        end = masked.find("\n}", base)          # first `}` in column 0 = body end
        if end < 0:
            die("could not find the end of %r." % scope,
                "The body is bounded by the next '}' in column 0.")
        region = masked[base:end]
    hits = [m.start() for m in re.finditer(re.escape(anchor), region)]
    if len(hits) != 1:
        die("anchor %r occurs %d times in %s (expected exactly 1)."
            % (anchor, len(hits),
               "sd_card_manager.c" if not scope else "%s's body" % scope.strip()),
            "%s anchors on the STATEMENT AFTER its LOG_E, so a moved or" % SELF,
            "duplicated call site means the test no longer knows which message it",
            "is measuring. Re-read %s and the test before changing either."
            % site["what"])
    pos = base + hits[0]
    call = masked.rfind("LOG_E(", base, pos)
    if call < 0:
        die("no LOG_E( precedes the anchor %r%s."
            % (anchor, " within %s" % scope.strip() if scope else ""),
            "The %s call site is gone or was renamed." % site["what"])
    q = call + len("LOG_E(")
    while q < len(masked) and masked[q] in " \t\r\n":
        q += 1
    if q >= len(masked) or masked[q] != '"':
        die("the LOG_E for %s no longer starts with a string literal." % site["what"],
            "Only a literal format string can be budget-checked ahead of time; a",
            "runtime format string would need a different test entirely.")
    text, _ = parse_literal_group(raw, q, macros, "LOG_E for %s" % site["what"])
    got = conversion_spec(text)
    if got != site["conv"]:
        die("the LOG_E for %s now takes conversions %r, not %r."
            % (site["what"], got, site["conv"]),
            "test_1029_sd_card_manager_log_budget.c formats this message with a",
            "fixed argument list and derives its worst case from those arguments.",
            "A changed conversion list changes BOTH -- update SITES in %s and" % SELF,
            "the matching case in the test, then re-derive the budget.")
    if any(ord(c) > 0x7E or (ord(c) < 0x20 and c not in "\r\n\t") for c in text):
        die("the LOG_E for %s contains a non-ASCII or control byte." % site["what"],
            "tools/lint/check_log_ascii.py (#787) forbids that in firmware text.")
    return text, raw.count("\n", 0, q) + 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="gen_1029_log_budget.h")
    ap.add_argument("--root",
                    default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                         "..", ".."))
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    fw = os.path.join(root, "firmware", "src")
    sd_dir = os.path.join(fw, "services", "sd_card_services")

    logger_h = read(os.path.join(fw, "Util", "Logger.h"))
    logger_c = read(os.path.join(fw, "Util", "Logger.c"))
    sd_h = read(os.path.join(sd_dir, "sd_card_manager.h"))
    sd_c = read(os.path.join(sd_dir, "sd_card_manager.c"))
    logger_c_masked = mask_comments(logger_c)
    sd_c_masked = mask_comments(sd_c)

    # ---- Logger.h ---------------------------------------------------------
    log_size = int_define(defines(logger_h), "LOG_MESSAGE_SIZE", "Util/Logger.h")

    # ---- Logger.c: both reservations, read out of the real expressions -----
    m = re.search(r"size\s*=\s*vsnprintf\s*\(\s*buffer\s*,\s*"
                  r"LOG_MESSAGE_SIZE\s*-\s*(\d+)\s*,\s*format\s*,\s*args\s*\)\s*;",
                  logger_c_masked)
    if not m:
        die("could not find LogMessageFormatImpl's vsnprintf in Util/Logger.c.",
            "The test mirrors that exact call; if its bound has changed shape,",
            "re-read LogMessageFormatImpl and re-derive the mirror rather than",
            "relaxing this pattern.")
    vsn_reserve = int(m.group(1))

    m = re.search(r"size\s*=\s*min\s*\(\s*\(\s*LOG_MESSAGE_SIZE\s*-\s*(\d+)\s*\)"
                  r"\s*,\s*size\s*\)\s*;", logger_c_masked)
    if not m:
        die("could not find LogMessageFormatImpl's clamp in Util/Logger.c.",
            "That clamp is what sets the emitted-content ceiling the test asserts",
            "against; re-derive the mirror rather than relaxing this pattern.")
    clamp_reserve = int(m.group(1))
    content_max = log_size - clamp_reserve

    # The mirror also reproduces the three-branch CRLF fixup, so pin its shape:
    # a rewrite of that block must not leave the mirror quietly modelling the old
    # one. Checked as ORDERED markers -- the technique test_953's guard uses.
    for pat in (r"if\s*\(\s*size\s*>=\s*2\s*&&\s*buffer\s*\[\s*size\s*-\s*2\s*\]\s*==\s*'\\r'",
                r"else\s+if\s*\(\s*size\s*>=\s*1\s*&&\s*buffer\s*\[\s*size\s*-\s*1\s*\]\s*==\s*'\\n'",
                r"buffer\s*\[\s*size\s*\]\s*=\s*'\\r'\s*;\s*"
                r"buffer\s*\[\s*size\s*\+\s*1\s*\]\s*=\s*'\\n'"):
        mm = re.search(pat, logger_c_masked)
        if not mm:
            die("LogMessageFormatImpl's CRLF fixup in Util/Logger.c is no longer "
                "already-CRLF -> bare-LF -> append,",
                "or one arm was reworded or moved. The test mirrors THAT cascade.",
                "Marker that failed: %s" % pat)

    # ---- sd_card_manager.{h,c} -------------------------------------------
    sd_defs_h, sd_defs_c = defines(sd_h), defines(sd_c)
    dir_len_max = int_define(sd_defs_h, "SD_CARD_MANAGER_CONF_DIR_NAME_LEN_MAX",
                             "sd_card_manager.h")
    max_bucket = int_define(sd_defs_c, "SD_CARD_MANAGER_MAX_BUCKET",
                            "sd_card_manager.c")
    max_dir_files = int_define(sd_defs_c, "SD_CARD_MANAGER_MAX_DIR_FILES",
                               "sd_card_manager.c")
    bucket_prefix = str_define(sd_defs_c, "SD_CARD_MANAGER_BUCKET_PREFIX",
                               "sd_card_manager.c")

    # sd_BuildBucketPath's two branches. The bucket != 0 branch is the one the
    # test formats; the bucket == 0 branch is pinned only to establish that it
    # still yields the directory verbatim -- i.e. is strictly shorter -- so the
    # branch the test DOES measure really is the worst case.
    if not re.search(r"written\s*=\s*snprintf\s*\(\s*out\s*,\s*outLen\s*,\s*"
                     r'"%s"\s*,\s*dir\s*\)\s*;', sd_c_masked):
        die('sd_BuildBucketPath\'s bucket==0 branch is no longer '
            'snprintf(out, outLen, "%s", dir).',
            "The test assumes that branch yields the directory verbatim and so is",
            "strictly shorter than the bucket!=0 branch it measures. Re-derive",
            "which branch is the worst case before changing this.")
    calls = list(re.finditer(r"written\s*=\s*snprintf\s*\(\s*out\s*,\s*outLen\s*,\s*",
                             sd_c_masked))
    if len(calls) != 2:
        die("sd_BuildBucketPath has %d `written = snprintf(out, outLen, ...)` calls, "
            "expected 2 (bucket==0 and bucket!=0)." % len(calls),
            "The test measures the SECOND one as the worst case; with a different",
            "number of branches that choice has to be re-derived.")
    start = sd_c_masked.find('"', calls[1].end())
    bucket_fmt, _ = parse_literal_group(sd_c, start, sd_defs_c,
                                        "sd_BuildBucketPath's bucket path format")
    if conversion_spec(bucket_fmt) != "su":
        die("sd_BuildBucketPath's bucket path format now takes %r, not 's' then 'u'."
            % conversion_spec(bucket_fmt),
            "The test formats it with (const char *dir, unsigned bucket).")
    m = re.search(r"%0(\d+)u", bucket_fmt)
    if not m:
        die("sd_BuildBucketPath's format %r no longer zero-pads the bucket number."
            % bucket_fmt,
            "The worst-case path length is derived from that field width.")
    bucket_digits = max(int(m.group(1)), len(str(max_bucket)))
    # Buffer size the test needs to hold the longest path that format can
    # produce: its literal text, plus a maximum-length directory, plus the
    # widest bucket field, plus the NUL.
    bucket_path_maxlen = (len(CONV_RE.sub("", bucket_fmt))
                          + dir_len_max + bucket_digits + 1)

    sites = [(s,) + extract_site(sd_c, sd_c_masked, sd_defs_c, s) for s in SITES]

    # ---- emit -------------------------------------------------------------
    L = ["/* GENERATED by tests/host/%s -- DO NOT EDIT, DO NOT COMMIT." % SELF,
         " *",
         " * Every value below was read out of the real firmware source at build",
         " * time. See %s for what fails the build, and why." % SELF,
         " */",
         "#ifndef GEN_1029_LOG_BUDGET_H",
         "#define GEN_1029_LOG_BUDGET_H",
         "",
         "/* firmware/src/Util/Logger.h */",
         "#define FW_LOG_MESSAGE_SIZE       %d" % log_size,
         "",
         "/* firmware/src/Util/Logger.c, LogMessageFormatImpl */",
         "#define FW_LOG_VSNPRINTF_RESERVE  %d" % vsn_reserve,
         "#define FW_LOG_CLAMP_RESERVE      %d" % clamp_reserve,
         "",
         "/* The effective content ceiling. A formatted message longer than this",
         " * loses its TAIL before it ever reaches SYST:LOG?. */",
         "#define FW_LOG_CONTENT_MAX        %d" % content_max,
         "",
         "/* firmware/src/services/sd_card_services/sd_card_manager.{h,c} */",
         "#define FW_SD_DIR_NAME_LEN_MAX    %d" % dir_len_max,
         "#define FW_SD_MAX_BUCKET          %d" % max_bucket,
         "#define FW_SD_MAX_DIR_FILES       %d" % max_dir_files,
         "#define FW_SD_BUCKET_PREFIX       %s" % encode_c_literal(bucket_prefix),
         "#define FW_SD_BUCKET_DIGITS       %d" % bucket_digits,
         "",
         "/* sd_BuildBucketPath(), bucket != 0 branch, with",
         " * SD_CARD_MANAGER_BUCKET_PREFIX already expanded. */",
         "#define FW_SD_BUCKET_PATH_FMT     %s" % encode_c_literal(bucket_fmt),
         "/* Longest path that format can produce, plus the NUL. */",
         "#define FW_SD_BUCKET_PATH_MAXLEN  %d" % bucket_path_maxlen,
         ""]
    for site, text, line in sites:
        key = site["key"]
        L += ["/* %s (sd_card_manager.c:%d) */" % (site["what"], line),
              "#define FW_MSG_%s       %s" % (key, encode_c_literal(text)),
              "#define FW_MSG_%s_CONV  %s" % (key, encode_c_literal(site["conv"])),
              "#define FW_MSG_%s_LINE  %d" % (key, line),
              ""]
    L.append("#endif /* GEN_1029_LOG_BUDGET_H */")

    tmp = args.out + ".tmp"
    with open(tmp, "w", encoding="ascii") as f:
        f.write("\n".join(L) + "\n")
    os.replace(tmp, args.out)
    print("%s: wrote %s (LOG_MESSAGE_SIZE=%d, content ceiling=%d, %d messages)"
          % (SELF, args.out, log_size, content_max, len(sites)))


if __name__ == "__main__":
    main()
