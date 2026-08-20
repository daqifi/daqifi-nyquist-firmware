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


# `.pattern = "A" "B", .callback = X` is legal C -- adjacent string literals
# concatenate -- so the string part is one-or-more literals, joined.
_REGISTRATION = re.compile(
    r'\.pattern\s*=\s*((?:"[^"]*"\s*)+),\s*\.callback\s*=\s*([A-Za-z_]\w*)')
_PATTERN_FIELD = re.compile(r"\.pattern\s*=")
# libscpi requires the command table to end with a {NULL, ...} sentinel. It is
# a real `.pattern =` field and deliberately not a command, so the parse guard
# below counts it as understood rather than as something it failed to read.
_NULL_SENTINEL = re.compile(r"\.pattern\s*=\s*NULL\b")
_ONE_LITERAL = re.compile(r'"([^"]*)"')


def _joined(literal_group):
    """The C value of one or more adjacent string literals."""
    return "".join(_ONE_LITERAL.findall(literal_group))


def registered_patterns(scpi_c):
    """(live, commented_out) sets of .pattern strings in the command table.

    Raises SystemExit if any `.pattern =` field fails to parse. That guard is
    the point: the failure mode of a regex-based extractor is not a wrong
    answer, it is a SILENTLY SMALLER `live` set -- and a command missing from
    `live` can never be reported as undocumented, so the gate would go green
    over exactly the drift it exists to catch. Better to fail loudly on a C
    construct this does not understand than to quietly stop checking it.
    """
    if not os.path.exists(scpi_c):
        # A bare FileNotFoundError traceback in CI reads like the checker is
        # broken. It usually means the command table was moved or renamed,
        # which is a one-line fix once you know that is what happened.
        sys.exit(f"error: {scpi_c!r} not found. If the SCPI command table "
                 f"moved, pass --scpi <path> and update "
                 f".github/workflows/scpi-wiki-sync.yml.")
    with open(scpi_c, encoding="utf-8", errors="replace") as fh:
        raw = fh.read()
    live_src = strip_c_comments(raw)
    every = {_joined(g) for g, _ in _REGISTRATION.findall(raw)}
    live = {_joined(g) for g, _ in _REGISTRATION.findall(live_src)}

    declared = (len(_PATTERN_FIELD.findall(live_src))
                - len(_NULL_SENTINEL.findall(live_src)))
    parsed = len(_REGISTRATION.findall(live_src))
    if declared != parsed:
        sys.exit(f"error: {scpi_c!r} has {declared} live '.pattern =' command "
                 f"fields but only {parsed} parsed as registrations. An entry "
                 f"uses a form this checker does not understand, and would be "
                 f"silently omitted from the check -- so a command missing "
                 f"from the wiki could never be reported. Extend "
                 f"_REGISTRATION in tools/lint/scpi_wiki_sync.py.")
    return live, every - live


def _short_form(node):
    """The node truncated at its first lowercase letter -- libscpi's short form."""
    for i, c in enumerate(node):
        if c.islower():
            return node[:i]
    return node


def is_form_of(written, pattern):
    """True if `written` is a way libscpi would actually accept `pattern`.

    libscpi accepts exactly TWO spellings of each node and no others
    (`matchPattern`, libraries/scpi/libscpi/src/utils.c:478):

        compareStr(full node)            || compareStr(node truncated at the
                                            first lowercase letter)

    and `compareStr` (utils.c:347) requires the lengths to be EQUAL. So for
    `CONFigure` only `CONFigure` and `CONF` are legal -- `CONFig` is not, and
    the device answers -113 for it.

    This is stricter than CLAUDE.md's prose rule ("must contain all letters
    that are in CAPS"), which an earlier version of this function implemented
    as "any prefix keeping the capitals". That accepted `CONFig:ADC:OBDiag`,
    so a wiki row spelled that way vouched for a command the firmware would
    reject -- a false pass in both directions at once. The shipped matcher, not
    the prose, is the authority here, because the whole question this tool asks
    is what the device does.

    The trailing '?' must match exactly: query and setter are registered
    separately with distinct callbacks, and this codebase contains a split
    pair (`SYSTem:COMMunicate:LAN:DNS1` is registered, `...:DNS1?` is not).
    """
    if written.endswith("?") != pattern.endswith("?"):
        return False
    w_nodes = written.rstrip("?").split(":")
    p_nodes = pattern.rstrip("?").split(":")
    if len(w_nodes) != len(p_nodes):
        return False
    for w, n in zip(w_nodes, p_nodes):
        if w.upper() != n.upper() and w.upper() != _short_form(n).upper():
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


def _lower(values):
    return {v.lower() for v in values}


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
    table_cells, rows = set(), []
    files = sorted(glob.glob(os.path.join(wiki_dir, "*.md")))
    if not files:
        sys.exit(f"error: no .md files under {wiki_dir!r} -- is the wiki cloned?")
    for path in files:
        with open(path, encoding="utf-8", errors="replace") as fh:
            body = fh.read()

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
            # Cells of NON-command tables are a weaker kind of mention. They
            # are needed -- the legacy alias table names its commands in the
            # SECOND column ("| Canonical | Legacy alias | Migration PR |"), so
            # command-table first cells alone would report four shipped aliases
            # as undocumented -- but they are matched EXACTLY, never by
            # abbreviation. An audit showed abbreviation-matching here let a
            # coincidental cell in a completely unrelated table vouch for a
            # deleted command row (reproduced on the real wiki with
            # DIO:COUNter? and SYST:STR:START/STOP/STATS?).
            for c in cells:
                cleaned = clean_cell(c)
                if cleaned:
                    table_cells.add(cleaned)
            if not in_command_table:
                continue
            if set(first) <= set("-: "):          # the |---|---| separator
                continue
            first = clean_cell(first)
            if first:
                rows.append((first, line))
    return table_cells, rows


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
    ("CONFig:ADC:OBDiag", "CONFigure:ADC:OBDiag", False,
     "libscpi accepts only the full node or the short form -- `CONFig` is "
     "neither, and the device answers -113 for it (utils.c:478 matchPattern, "
     "compareStr requires equal lengths). The prose rule in CLAUDE.md is "
     "looser than the shipped matcher; the matcher is the authority"),
    ("SYSTe:DEVic:NAME", "SYSTem:DEVice:NAME", False,
     "same rule, every node: a mid-length prefix is not a legal spelling"),
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
    e2e_cases, e2e_failures = _self_test_end_to_end()
    failures += e2e_failures
    if failures:
        print(f"\n::error::{failures} self-test(s) failed")
        return 1
    print(f"self-test: {len(SELF_TEST_CASES)}/{len(SELF_TEST_CASES)} matcher "
          f"cases + {e2e_cases}/{e2e_cases} end-to-end cases pass")
    return 0


def _self_test_end_to_end():
    """Pin the two false-pass holes an adversarial audit found and proved.

    Both failed toward SILENCE -- the checker exited 0 while a shipped command
    had no wiki row -- which is the one failure direction that makes a gate
    worse than useless, so both are pinned here rather than trusted to stay
    fixed.
    """
    import tempfile
    cases, failures = 3, 0
    src = ('const scpi_command_t scpi_commands[] = {\n'
           '    {.pattern = "SYSTem:WIFI:" "DEBUG?", .callback = SCPI_A,},\n'
           '    {.pattern = "SYSTem:POWer:OTG", .callback = SCPI_B,},\n'
           '    {.pattern = NULL, .callback = SCPI_NotImplemented,},\n};\n')
    with tempfile.TemporaryDirectory() as d:
        c = os.path.join(d, "scpi.c")
        with open(c, "w", encoding="utf-8") as fh:
            fh.write(src)

        # (1) Adjacent string literals concatenate in C. Missing this dropped
        # the command from `live`, where it could never be reported at all.
        live, _ = registered_patterns(c)
        if "SYSTem:WIFI:DEBUG?" not in live:
            print("  FAIL end-to-end: a concatenated .pattern was not extracted"
                  " -- it would be invisible to the whole check")
            failures += 1

        # (2) A bare mention in prose must NOT stand in for a table row. The
        # audit mutation-proved the old behaviour: deleting one prose sentence
        # flipped the verdict, so the sentence was carrying it.
        wiki = os.path.join(d, "wiki")
        os.makedirs(wiki)
        with open(os.path.join(wiki, "01.md"), "w", encoding="utf-8") as fh:
            fh.write("| SCPI Command | Description | Example | Callback |\n"
                     "| -- | -- | -- | -- |\n"
                     "| SYSTem:WIFI:DEBUG? | x | x | SCPI_A |\n\n"
                     "Prose only: SYSTem:POWer:OTG is diagnostic.\n")
        cells, rows = wiki_text_and_rows(wiki)
        written = [cmd for cmd, _ in rows]
        documented = (any(is_form_of(w, "SYSTem:POWer:OTG") for w in written)
                      or "system:power:otg" in _lower(cells))
        if documented:
            print("  FAIL end-to-end: a command mentioned only in prose counted"
                  " as documented -- the gate can be satisfied without writing"
                  " a row")
            failures += 1

        # (3) A cell in an UNRELATED table must not vouch for a missing command
        # row. An audit reproduced this on the real wiki: deleting the
        # DIO:COUNter? row while some other table happened to contain the
        # abbreviation DIO:COUN? left the checker green.
        wiki2 = os.path.join(d, "wiki2")
        os.makedirs(wiki2)
        with open(os.path.join(wiki2, "01.md"), "w", encoding="utf-8") as fh:
            fh.write("| SCPI Command | Description |\n| -- | -- |\n"
                     "| SYSTem:REboot | x |\n\n"
                     "| Signal | Example |\n| -- | -- |\n"
                     "| counter note | DIO:COUN? |\n")
        cells2, rows2 = wiki_text_and_rows(wiki2)
        written2 = [cmd for cmd, _ in rows2]
        vouched = (any(is_form_of(w, "DIO:COUNter?") for w in written2)
                   or "dio:counter?" in _lower(cells2))
        if vouched:
            print("  FAIL end-to-end: an abbreviation in an unrelated table "
                  "vouched for a command with no row of its own")
            failures += 1
    return cases, failures


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
    table_cells, rows = wiki_text_and_rows(args.wiki)
    allow = load_allowlist(args.allow)

    written = [cmd for cmd, _ in rows]
    # A command counts as documented if a table row names it, OR if the page
    # writes it anywhere -- some are covered by the legacy-alias table and by
    # prose rather than a row of their own. Deliberately the looser of the two
    # directions: the expensive failure is a user calling a command the wiki
    # lists and getting -113, which is the ghost check below.
    # Documented means a TABLE names it -- either as a command row, or in any
    # cell of any table (the legacy aliases live in the migration table's
    # second column).
    #
    # It deliberately no longer means "appears anywhere on the page". That
    # fallback read every .md file whole, prose and fenced code included, so a
    # single sentence mentioning a command silently vouched for a missing row.
    # An adversarial audit mutation-proved it: deleting one prose sentence
    # flipped the checker from exit 0 to exit 1, which means the sentence --
    # not any documentation -- was carrying the verdict. Anyone who did not
    # want to write a row could satisfy the gate by mentioning the command.
    undocumented = sorted(
        p for p in live
        if p not in allow
        and not any(is_form_of(w, p) for w in written)
        # Exact, not is_form_of: see the note where table_cells is built.
        and p.lower() not in _lower(table_cells))

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
