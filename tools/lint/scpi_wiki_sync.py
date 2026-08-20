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


def strip_c_comments(text):
    """Remove /* */ then // comments. Block first -- see module docstring."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def registered_patterns(scpi_c):
    """(live, commented_out) sets of .pattern strings in the command table."""
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
    """
    nodes = []
    for node in pattern.rstrip("?").split(":"):
        caps = "".join(c for c in node if c.isupper() or c.isdigit())
        nodes.append(caps if caps else node)
    return ":".join(nodes)


def wiki_text_and_rows(wiki_dir):
    """(all wiki text lowercased, [(command, whole row)] for table rows)."""
    blobs, rows = [], []
    files = sorted(glob.glob(os.path.join(wiki_dir, "*.md")))
    if not files:
        sys.exit(f"error: no .md files under {wiki_dir!r} -- is the wiki cloned?")
    for path in files:
        with open(path, encoding="utf-8", errors="replace") as fh:
            body = fh.read()
        blobs.append(body)
        for line in body.split("\n"):
            if not line.lstrip().startswith("|"):
                continue
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if cells and re.fullmatch(r"[A-Za-z][A-Za-z0-9]*(:[A-Za-z0-9]+)+\??",
                                      cells[0]):
                rows.append((cells[0], line))
    return "\n".join(blobs).lower(), rows


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


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scpi", default="firmware/src/services/SCPI/SCPIInterface.c")
    ap.add_argument("--wiki", required=True,
                    help="path to a clone of the daqifi-nyquist-firmware.wiki repo")
    ap.add_argument("--allow", default="tools/lint/scpi-wiki-allow.txt",
                    help="commands intentionally left out of the wiki")
    args = ap.parse_args()

    live, commented = registered_patterns(args.scpi)
    if not live:
        sys.exit(f"error: no .pattern entries found in {args.scpi!r} -- "
                 f"has the command table moved?")
    wiki_lower, rows = wiki_text_and_rows(args.wiki)
    allow = load_allowlist(args.allow)

    undocumented = sorted(
        p for p in live
        if p not in allow
        and p.rstrip("?").lower() not in wiki_lower
        and abbreviate(p).lower() not in wiki_lower)

    live_forms = {p.rstrip("?").lower() for p in live}
    live_forms |= {abbreviate(p).lower() for p in live}
    ghosts = sorted(
        {cmd for cmd, row in rows
         if cmd.rstrip("?").lower() not in live_forms
         and abbreviate(cmd).lower() not in live_forms
         and NOT_IMPLEMENTED_MARK not in row.lower()})

    print(f"registered SCPI commands : {len(live)}")
    print(f"commented-out patterns   : {len(commented)} (not shipped, ignored)")
    print(f"wiki command rows        : {len(rows)}")

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
        print(f"\n::error::{len(ghosts)} wiki command(s) are NOT registered in "
              f"the firmware -- calling them returns -113:")
        for cmd in ghosts:
            print(f"    {cmd}")
        print("\n  Either remove the row, or say 'NOT IMPLEMENTED' in it along")
        print("  with the reason. If the command was RENAMED, update the row to")
        print("  the new name instead of leaving the old one behind.")

    return 1


if __name__ == "__main__":
    sys.exit(main())
