#!/usr/bin/env python3
"""Fail if the SCPI command table and the wiki reference have drifted apart.

CLAUDE.md has said "When adding, modifying, or removing SCPI commands, ALWAYS
update the GitHub wiki SCPI reference page" for a long time. It was written
down and not enforced, and by 2026-08 the two had drifted in BOTH directions:

  * 37 shipped commands had no wiki entry at all -- including two entire
    families, `DIO:EVENt:*` and `DIO:COUNter:*`, that are registered, built,
    and reachable over SCPI today.
  * 11 documented commands were not registered, so following the wiki
    produced `-113 Undefined header`. Nine had their `.pattern` line
    commented out; two were a RENAME the wiki never followed
    (`SYSTem:DIOProbe:ASSign` -> `MODE` / `ROUTe`).

Both directions mislead, and the second is worse: a user who trusts the
reference gets an error from a command the page says exists.

WHY COMMENTS MUST BE STRIPPED FIRST
    `SCPIInterface.c` carries 16 commented-out `.pattern` entries. A naive
    regex over the raw file counts them as live, which during this audit
    produced three phantom "undocumented" commands -- including
    `SYSTem:NVMWrite` and `SYSTem:NVMErasePage`, raw-NVM operations that are
    deliberately NOT exposed. Documenting those would have invented a
    dangerous API. Block comments are stripped before line comments so a
    `//` inside a `/* */` cannot confuse the second pass.

WHY MATCHING ACCEPTS THE ABBREVIATED FORM
    The project's SCPI rule is that a command may be written with only the
    capitalised letters of each node (`SYSTem:DEVice:NAME` -> `SYST:DEV:NAME`),
    and the wiki legitimately uses either. Matching the full form alone
    reported `SYSTem:DIOProbe:MODE` missing while the page discussed
    `SYST:DIOP:...` two lines above. Both forms are accepted.

WHY A GHOST NEEDS THE WORDS, NOT AN ALLOWLIST
    A wiki row naming an unregistered command is allowed ONLY if the row also
    says NOT IMPLEMENTED. That keeps the rationale next to the claim -- OTG,
    for instance, is unregistered on purpose because the power system manages
    it automatically -- instead of hiding it in a separate file nobody reads
    next to the table they do.

EXIT
    0 = in sync. 1 = drift, with each offending command named.
"""

import argparse
import glob
import os
import re
import sys

NOT_IMPLEMENTED_MARK = "not implemented"


# String literals are matched FIRST so a comment marker inside one is not
# mistaken for a comment, and a quote inside a comment cannot open a string.
# Both comment forms are then consumed whole, in one pass.
_CODE_OR_COMMENT = re.compile(
    r'"(?:\\.|[^"\\])*"'      # string literal  -- kept
    r"|'(?:\\.|[^'\\])*'"     # char literal    -- kept
    r"|/\*.*?\*/"              # block comment   -- dropped
    r"|//[^\n]*",              # line comment    -- dropped
    re.S)


def strip_c_comments(text):
    """Remove C comments, leaving string and character literals intact.

    Single-pass rather than "strip block comments, then line comments". The
    two-pass form is wrong on a line comment that contains an opening block
    marker -- `// see /* note` -- where the first pass starts a block at that
    marker and deletes everything up to the next `*/`, taking real code with
    it. It is equally wrong about a comment marker inside a string literal.

    Neither case exists in SCPIInterface.c today (checked: zero occurrences,
    and both forms yield the same 292 patterns), so this is a guard against a
    future edit rather than a fix for a live miscount -- but a checker that
    silently loses commands the moment someone writes an ordinary comment is
    not worth having.
    """
    return _CODE_OR_COMMENT.sub(
        lambda m: m.group(0) if m.group(0)[0] in "\"'" else "", text)


def registered_patterns(scpi_c):
    """(live, commented_out) sets of .pattern strings in the command table."""
    if not os.path.exists(scpi_c):
        # A bare FileNotFoundError traceback in CI reads like the checker is
        # broken. It usually means the command table was moved or renamed,
        # which is a one-line fix once you know that is what happened.
        sys.exit(f"error: {scpi_c!r} not found. If the SCPI command table "
                 f"moved, pass --scpi <path> and update "
                 f".github/workflows/scpi-wiki-sync.yml.")
    with open(scpi_c, encoding="utf-8", errors="replace") as fh:
        raw = fh.read()
    rx = r'\.pattern\s*=\s*"([^"]+)"\s*,\s*\.callback\s*=\s*([A-Za-z_]\w*)'
    every = {p for p, _ in re.findall(rx, raw)}
    live = {p for p, _ in re.findall(rx, strip_c_comments(raw))}
    return live, every - live


def abbreviate(pattern):
    """SCPI short form: the capitals (and digits) of each node.

    A node with no capitals is kept verbatim rather than collapsing to an
    empty string, which would make the joined form match almost anything.

    The trailing '?' is PRESERVED. Query and setter are registered separately
    with distinct callbacks, and this codebase actually contains a split pair:
    `SYSTem:COMMunicate:LAN:DNS1` is registered while `...:DNS1?` is not. An
    earlier version stripped the '?' before comparing, which made the live
    setter vouch for the dead getter -- the checker would have reported that
    exact row as fine.
    """
    query = pattern.endswith("?")
    nodes = []
    for node in pattern.rstrip("?").split(":"):
        caps = "".join(c for c in node if c.isupper() or c.isdigit())
        nodes.append(caps if caps else node)
    return ":".join(nodes) + ("?" if query else "")


def _mandatory(node):
    """The characters an abbreviation of `node` may not drop: its capitals."""
    return "".join(c for c in node if c.isupper() or c.isdigit())


def is_form_of(written, pattern):
    """True if `written` is a legal way to write `pattern`.

    Implements the project's SCPI abbreviation rule (CLAUDE.md): a command may
    be written with any prefix of each node, provided the prefix keeps every
    capitalised letter. `CONF:ADC:OBDiag`, `CONFigure:ADC:OBD` and
    `CONFigure:ADC:OBDiag` are all the same command.

    Comparing against just two canonical forms -- full and caps-only -- is not
    enough, and missed a real case: the wiki writes `CONF:ADC:OBDiag`, which is
    neither `CONFigure:ADC:OBDiag` nor `CONF:ADC:OBD`.

    The trailing '?' must match exactly. Query and setter are registered
    separately with distinct callbacks, and this codebase contains a split
    pair -- `SYSTem:COMMunicate:LAN:DNS1` is registered while `...:DNS1?` is
    not -- so treating them as one command lets the live setter vouch for the
    dead getter.
    """
    if written.endswith("?") != pattern.endswith("?"):
        return False
    w_nodes = written.rstrip("?").split(":")
    p_nodes = pattern.rstrip("?").split(":")
    if len(w_nodes) != len(p_nodes):
        return False
    for w, n in zip(w_nodes, p_nodes):
        if not n.upper().startswith(w.upper()):
            return False
        # The prefix must CONTAIN every capital, which is not the same as being
        # at least as long as the count of them: the capitals are not always
        # contiguous at the front. `AvNETType` has capitals A,NET,T -- five --
        # so a length test accepts the five-character `AvNET`, which has
        # dropped the final T and is not a legal way to write it.
        if _mandatory(n[:len(w)]) != _mandatory(n):
            return False
    return True


def clean_cell(cell):
    """The bare command in a table's first cell.

    Rows are not uniform: some carry the argument in the command cell
    (`SYSTem:MEMory:SD:BUFfer \\<bytes\\>`), some wrap it in backticks, and one
    slipped a `<br>` in. Taking the cell verbatim reported those commands as
    BOTH undocumented and ghosts -- the same row failing in both directions,
    which is the signature of a parsing bug rather than real drift.
    """
    cell = cell.replace("`", "").replace("<br>", " ").strip()
    cell = re.split(r"[\s\\<(]", cell, 1)[0]      # drop a trailing argument
    return cell.strip().rstrip(",")


def split_row(line):
    """Cells of a markdown table row, splitting on UNESCAPED pipes only.

    The wiki writes `SYSTem:STReam:BENCHmark \\<0\\|1\\|2\\>` in a first cell.
    Splitting on every '|' truncated that command to `SYSTem:STReam:BENCHmark
    \\<0\\` and reported it as a ghost.
    """
    parts = re.split(r"(?<!\\)\|", line.strip().strip("|"))
    return [c.strip() for c in parts]


def wiki_text_and_rows(wiki_dir):
    """(all wiki text, [(command, whole row)] for rows of the COMMAND tables).

    Rows are taken from tables whose header's first column is "SCPI Command",
    rather than by pattern-matching the first cell. The first version used a
    regex requiring at least one ':', which silently ignored every IEEE common
    command (`*IDN?`, `*CLS`, `*RST`) and single-node commands like `help` --
    so a stale or invented row in those families could never be reported.
    Loosening the regex instead would have been worse: `| Probe | Stage |` and
    the various field-reference tables would start yielding "commands" like
    `Probe` and `Bit`.

    Keying on the header is exact. Every command table in the wiki uses it.
    """
    blobs, rows = [], []
    files = sorted(glob.glob(os.path.join(wiki_dir, "*.md")))
    if not files:
        sys.exit(f"error: no .md files under {wiki_dir!r} -- is the wiki cloned?")
    for path in files:
        with open(path, encoding="utf-8", errors="replace") as fh:
            body = fh.read()
        blobs.append(body)
        in_command_table = False
        for line in body.split("\n"):
            if not line.lstrip().startswith("|"):
                in_command_table = False          # any non-row ends the table
                continue
            cells = split_row(line)
            if not cells:
                continue
            first = cells[0]
            if first.lower() in ("scpi command", "command"):  # header: rows below are commands
                in_command_table = True
                continue
            if not in_command_table:
                continue
            if set(first) <= set("-: "):          # the |---|---| separator
                continue
            first = clean_cell(first)
            if first:
                rows.append((first, line))
    return "\n".join(blobs), rows


def load_allowlist(path):
    if not path or not os.path.exists(path):
        return set()
    out = set()
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if line:
                out.add(line)
    return out


SELF_TEST_CASES = [
    # (written, pattern, expected, why this case exists)
    ("SYSTem:DEVice:NAME", "SYSTem:DEVice:NAME", True, "full form"),
    ("SYST:DEV:NAME", "SYSTem:DEVice:NAME", True, "caps-only form"),
    ("CONF:ADC:OBDiag", "CONFigure:ADC:OBDiag", True,
     "mixed prefix -- the wiki writes this, and comparing against only the "
     "full and caps-only forms missed it"),
    ("SYSTem:DEVice:NAME", "SYSTem:DEVice:NAME?", False,
     "a setter must not vouch for its query: LAN:DNS1 is registered and "
     "LAN:DNS1? is not"),
    ("SYSTem:DEVice:NAME?", "SYSTem:DEVice:NAME", False, "nor the reverse"),
    ("SYSTem:DEVice:NAME", "SYSTem:DEVice:NAME:SAVE", False,
     "a parent must not be matched by its child"),
    ("SYST:DEV:NAM", "SYSTem:DEVice:NAME", False, "dropped a mandatory capital"),
    ("AvNET", "AvNETType", False,
     "capitals are not contiguous here (A, NET, T), so a length test wrongly "
     "accepted this five-character prefix"),
    ("AvNETType", "AvNETType", True, "the full node still matches"),
    ("help", "HELP", True, "case-insensitive"),
    ("*RST", "*RST", True, "IEEE common command"),
    ("*RS", "*RST", False, "dropped a mandatory capital of an IEEE command"),
]


def self_test():
    """Check the abbreviation rule against its known cases.

    Kept in the tool rather than a side file so it cannot drift away from the
    function it covers, and so CI runs it for free.
    """
    failures = 0
    for written, pattern, expected, why in SELF_TEST_CASES:
        got = is_form_of(written, pattern)
        if got != expected:
            failures += 1
            print(f"  FAIL is_form_of({written!r}, {pattern!r}) = {got}, "
                  f"expected {expected} -- {why}")
    if failures:
        print(f"\n::error::{failures} of {len(SELF_TEST_CASES)} matcher "
              f"self-tests failed")
        return 1
    print(f"self-test: {len(SELF_TEST_CASES)}/{len(SELF_TEST_CASES)} matcher "
          f"cases pass")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scpi", default="firmware/src/services/SCPI/SCPIInterface.c")
    ap.add_argument("--self-test", action="store_true",
                    help="check the abbreviation matcher and exit")
    ap.add_argument("--wiki",
                    help="path to a clone of the daqifi-nyquist-firmware.wiki repo")
    ap.add_argument("--allow", default="tools/lint/scpi-wiki-allow.txt",
                    help="commands intentionally left out of the wiki")
    ap.add_argument("--ghosts-warn-only", action="store_true",
                    help="report ghost rows without failing (for scheduled "
                         "runs -- see the note in main())")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if not args.wiki:
        ap.error("--wiki is required (or use --self-test)")

    if self_test() != 0:      # a broken matcher makes every verdict below junk
        return 1

    live, commented = registered_patterns(args.scpi)
    if not live:
        sys.exit(f"error: no .pattern entries found in {args.scpi!r} -- "
                 f"has the command table moved?")
    _wiki_text, rows = wiki_text_and_rows(args.wiki)
    allow = load_allowlist(args.allow)

    written = [cmd for cmd, _ in rows]
    # A command counts as documented if a table row names it, OR if the page
    # writes it anywhere -- some are covered by the legacy-alias table and by
    # prose rather than a row of their own. Deliberately the looser of the two
    # directions: the expensive failure is a user calling a command the wiki
    # lists and getting -113, which is the ghost check below.
    tokens = set(re.findall(r"\*?[A-Za-z][A-Za-z0-9]*(?::[A-Za-z0-9]+)*\??", _wiki_text))
    undocumented = sorted(
        p for p in live
        if p not in allow
        and not any(is_form_of(w, p) for w in written)
        # The prose fallback is deliberately limited to MULTI-NODE commands.
        # A single-node one is an ordinary English word -- `help` -- or a short
        # token, so any page of prose would vouch for it and the check would be
        # meaningless exactly where it is easiest to satisfy honestly. Those
        # must have a table row.
        and not (":" in p and any(is_form_of(t, p) for t in tokens)))

    # "Documented" means a command TABLE ROW names it. Searching the whole page
    # instead let a command pass on a passing mention inside another command's
    # prose -- which is how CONFigure:ADC:OBDiag looked documented while having
    # no row of its own.
    ghosts = sorted(
        {cmd for cmd, row in rows
         if not any(is_form_of(cmd, p) for p in live)
         and NOT_IMPLEMENTED_MARK not in row.lower()})

    print(f"registered SCPI commands : {len(live)}")
    print(f"commented-out patterns   : {len(commented)} (not shipped, ignored)")
    print(f"wiki command rows        : {len(rows)}")

    fatal_ghosts = ghosts and not args.ghosts_warn_only
    if not undocumented and not ghosts:
        print("\nOK: the wiki and the command table agree.")
        return 0

    if undocumented:
        print(f"\n::error::{len(undocumented)} shipped SCPI command(s) are "
              f"missing from the wiki:")
        for cmd in undocumented:
            print(f"    {cmd}")
        print("\n  Add a row to the matching table in 01-SCPI-Interface.md")
        print("  (clone: https://github.com/daqifi/daqifi-nyquist-firmware.wiki.git).")
        print(f"  If a command is deliberately unpublished, add it to {args.allow}")
        print("  with a comment saying why.")

    if ghosts:
        # Warn-only exists for the SCHEDULED run against main, and the reason is
        # a real ordering trap: this gate fails a PR whose new command has no
        # wiki row, so the wiki must be pushed BEFORE that PR merges -- during
        # which main legitimately has a wiki entry for a command it does not yet
        # register. Failing the weekly run for that would punish following the
        # process. On a PR the checkout contains the new command, so a genuine
        # ghost still fails there.
        level = "warning" if args.ghosts_warn_only else "error"
        print(f"\n::{level}::{len(ghosts)} wiki command(s) are NOT registered "
              f"in the firmware -- calling them returns -113:")
        for cmd in ghosts:
            print(f"    {cmd}")
        print("\n  Either remove the row, or say 'NOT IMPLEMENTED' in it along")
        print("  with the reason. If the command was RENAMED, update the row to")
        print("  the new name instead of leaving the old one behind.")

    return 1 if (undocumented or fatal_ghosts) else 0


if __name__ == "__main__":
    sys.exit(main())
