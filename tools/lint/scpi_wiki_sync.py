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

HOUSE STYLE (#907) -- WHY TWO PROPERTIES, NOT THE THREE THE TICKET NAMED
    Every registered node must accept a short form of at least TWO
    characters (S1), and no two registered patterns may accept the same
    spelling (S2). Both are checked over the WHOLE live table, every run --
    see `style_violations` for the reasoning, including why a per-node
    "caps prefix is a genuine prefix" check is vacuous (it can never fail,
    `_short_form` is a prefix by construction) and why an upper bound on
    short-form length is deliberately not enforced (ten pre-existing,
    defect-free mnemonics -- BENCHmark, LOADFcal, TRANSparent, etc. -- would
    need allowlisting for no behavioural gain).

    NO DIFF-AWARE "new/changed rows only" MECHANISM. The scheduled weekly
    run against `main` (`.github/workflows/scpi-wiki-sync.yml`) has no PR
    diff to compare against, so a diff-aware gate would silently check
    nothing there -- the exact "gate that ran and established nothing"
    failure this repo has already hit twice (#863/#864, #899). Checking the
    whole table, like every other check in this file, has nothing to
    grandfather once the eight #907 patterns are fixed: `STYLE_ALLOWLIST`
    below is the complete list of pre-existing violators, all four of them
    the #324 legacy camelCase aliases, and it stays a short, named,
    understood set rather than growing invisibly.

    S3 (EXACT-DUPLICATE PATTERN STRINGS) -- ADDED after PR #1036's own
    adversarial audit found S1/S2 blind to it. `registered_patterns()`
    collapses all registrations into the `live` SET, so two rows whose
    `.pattern` string is byte-identical but whose callbacks differ become
    ONE set element -- S2's `spelling_owners` then sees a single owner for
    every spelling that pattern produces, and reports nothing, even though
    `findCommandHeader` (libscpi/src/parser.c:177) returns on the FIRST
    table match and the second row's callback is silently unreachable no
    matter what it does. S3 is checked on the RAW registration list, before
    that dedup, and needs no allowlist and no callback comparison -- the
    second row is dead regardless of what it would have done. See
    `duplicate_pattern_violations`.

    DOMAIN GUARD -- WHAT S1/S2/S3 DO NOT MODEL, REFUSED RATHER THAN
    SILENTLY MIS-READ. S1/S2/S3 above tokenize a pattern by splitting on
    ':' and treating every other character as an ordinary letter. libscpi
    does not: `matchPattern` (libraries/scpi/libscpi/src/utils.c:478)
    strips a trailing '#' repeat-count suffix and defers to
    `compareStrAndNum`, which ALSO accepts the spelling with that suffix
    omitted, and `matchCommand` expands a '[...]' optional node into
    multiple accepted spellings. A pattern using either construct --
    `SYSTem:DEVice:NAME#` or `SYSTem:DEVice[:NAME]` -- could collide with
    another registration at the PARSER, winning or losing in
    `findCommandHeader` by table order, while S1/S2/S3 print clean, because
    they would treat '#'/'['/']' as ordinary characters rather than the
    special syntax libscpi gives them. Found by PR #1036's own second
    adversarial audit round.

    Expanding '[...]' combinatorially, or modelling '#' plus
    `compareStrAndNum`, is a materially larger change than this fix, so
    `domain_violations` does neither: it REFUSES any live pattern outside
    the domain S1/S2/S3 actually reason about -- letters and digits, ':'
    separators, '?' only as the FINAL character, '*' only as the FIRST
    character -- failing the run loudly, with the offending pattern and
    construct named, rather than silently treating '#'/'['/']' as ordinary
    text. No allowlist, for the same reason S3 has none: there is no
    acceptable instance of a construct the checker cannot parse. Measured
    against the live table (294 registrations, 2026-09): the guard is a
    no-op there -- every registered pattern already stays inside the
    modelled domain.

EXIT
    0 = in sync. 1 = drift, with each offending command named.
"""

import argparse
import glob
import itertools
import os
import re
import sys

NOT_IMPLEMENTED_MARK = "not implemented"

# The #324 legacy camelCase aliases (CLAUDE.md "Stream-control namespace
# migration"). Each has a ONE-character short form ("S" -- `_short_form`
# truncates at the first lowercase letter, and only the leading capital
# survives), so all four fail S1. `StartStreamData` and `StopStreamData`
# additionally COLLIDE on the shared short form "SYST:S" (S2): both are
# registered (SCPIInterface.c, in that order), `findCommandHeader`
# (libscpi/src/parser.c:177) takes the FIRST match in table order, so
# "SYST:S" silently STARTS a stream rather than stopping one -- no -113, no
# error, just the wrong command. Hardware-reproduced 2026-09-11 (NQ1
# 7E2898F46200E8A7): SYST:S 1 with a channel enabled returned 0,"No error"
# and set STATus:OPERation:CONDition? bit 4 (Measuring). This is
# pre-existing, not introduced by #907, and is tracked as its own defect
# rather than fixed here -- #1035 (respelling any of the four changes which
# abbreviations existing client libraries can rely on, which needs a
# deprecation cycle -- see CLAUDE.md's "SCPI Abbreviation Rule" house-style
# note). Their canonical replacements
# (`SYST:STR:START`/`STOP`/`DATA?`, `SYST:USB:TRANS:MODE`) are unambiguous
# and are what new code should use.
STYLE_ALLOWLIST = frozenset({
    "SYSTem:StartStreamData",
    "SYSTem:StopStreamData",
    "SYSTem:StreamData?",
    "SYSTem:USB:SetTransparentMode",
})


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
    """(live, commented_out, live_list) for the .pattern strings in the table.

    `live` and `commented_out` are sets, as before. `live_list` is the RAW,
    non-deduplicated list of every live (non-commented) `.pattern` string, in
    table order, with repeats intact -- `live = set(live_list)` collapses two
    registrations sharing one pattern string into a single element, which is
    exactly the blind spot `duplicate_pattern_violations` exists to see
    through: S1/S2 (`style_violations`) only ever receive `live`, so they can
    never observe a duplicate that `live_list` still carries.

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
    live_list = [_joined(g) for g, _ in _REGISTRATION.findall(live_src)]
    live = set(live_list)

    declared = (len(_PATTERN_FIELD.findall(live_src))
                - len(_NULL_SENTINEL.findall(live_src)))
    parsed = len(live_list)
    if declared != parsed:
        sys.exit(f"error: {scpi_c!r} has {declared} live '.pattern =' command "
                 f"fields but only {parsed} parsed as registrations. An entry "
                 f"uses a form this checker does not understand, and would be "
                 f"silently omitted from the check -- so a command missing "
                 f"from the wiki could never be reported. Extend "
                 f"_REGISTRATION in tools/lint/scpi_wiki_sync.py.")
    return live, every - live, live_list


def _short_form(node):
    """The node truncated at its first lowercase letter -- libscpi's short form."""
    for i, c in enumerate(node):
        if c.islower():
            return node[:i]
    return node


def style_violations(live):
    """(short_violations, ambiguous_pairs) -- the #907 house-style gate.

    short_violations: sorted [(pattern, node)] where `node`'s short form is
    under 2 characters -- 0 (the node starts lowercase, e.g. the pre-#907
    `chanCALM`: `matchPattern`'s short arm can never match, only the full
    spelling is ever legal, silently) or 1 (typeable but identifies
    nothing, and see below -- on this table it already collides). An empty
    node (an `A::B` typo) is reported with node='' rather than crashing.

    ambiguous_pairs: sorted [((pattern, pattern, ...), [spelling, ...])] --
    every set of >=2 registered patterns that accept at least one identical
    spelling, with every such shared spelling listed. A pattern's accepted
    spellings are the cartesian product, across its `:`-separated nodes, of
    {node.upper(), short_form(node).upper() if non-empty}, `?` kept
    significant, matching `compareStr`'s case-insensitive, equal-length
    comparison (libscpi/src/utils.c:347). Two patterns sharing a spelling is
    a SILENT collision: `findCommandHeader` (libscpi/src/parser.c:177)
    returns the FIRST match in table order, so every pattern but the winner
    is simply unreachable by that spelling -- no -113, no error, just the
    wrong command running. A node already reported by short_violations
    contributes no spelling (an empty short form is untypeable), so its
    pattern is skipped for S2 rather than false-flagged again there.

    Deliberately NOT checked: an upper bound on short-form length. See the
    module docstring's HOUSE STYLE section.
    """
    short_violations = []
    spelling_owners = {}  # spelling -> set of patterns claiming it
    for pat in sorted(live):
        base = pat.rstrip("?")
        is_query = pat.endswith("?")
        node_forms = []
        unmatchable = False
        for node in base.split(":"):
            if not node:
                short_violations.append((pat, node))
                unmatchable = True
                continue
            sf = _short_form(node)
            if len(sf) < 2:
                short_violations.append((pat, node))
            forms = {node.upper()}
            if sf:
                forms.add(sf.upper())
            node_forms.append(sorted(forms))
        if unmatchable:
            continue
        for combo in itertools.product(*node_forms):
            spelling = ":".join(combo) + ("?" if is_query else "")
            spelling_owners.setdefault(spelling, set()).add(pat)

    by_pair = {}
    for spelling, owners in spelling_owners.items():
        if len(owners) > 1:
            by_pair.setdefault(tuple(sorted(owners)), set()).add(spelling)
    ambiguous = sorted((pair, sorted(spellings))
                        for pair, spellings in by_pair.items())
    return sorted(set(short_violations)), ambiguous


def apply_style_allowlist(short_violations, ambiguous, allowlist):
    """Drop entries fully explained by `allowlist` (see STYLE_ALLOWLIST).

    A short_violations entry is dropped when its OWN pattern is allowlisted.
    An ambiguous_pairs entry is dropped only when EVERY pattern in the
    colliding set is allowlisted -- a collision between one allowlisted
    legacy alias and one ordinary command would still be a live, unexplained
    defect and must not be silenced by the alias's presence alone.
    """
    kept_short = [(p, n) for p, n in short_violations if p not in allowlist]
    kept_ambig = [(pair, spellings) for pair, spellings in ambiguous
                  if not all(p in allowlist for p in pair)]
    return kept_short, kept_ambig


def duplicate_pattern_violations(live_list):
    """Sorted list of `.pattern` strings registered more than once (S3).

    Takes the RAW, non-deduplicated registration list (`registered_patterns`'s
    `live_list`) rather than the `live` set S1/S2 use -- `live = set(live_list)`
    is exactly where a duplicate disappears, so a checker fed `live` can never
    see one no matter how it compares spellings.

    Deliberately no allowlist and no callback comparison. Two registrations
    sharing one exact pattern string are indistinguishable to `matchPattern`
    regardless of what their callbacks do: `findCommandHeader`
    (libscpi/src/parser.c:177) returns on the FIRST table match, so every
    later row with the identical string is silently unreachable, full stop --
    there is no "acceptable" duplicate to allowlist, unlike S1/S2 where a
    pre-existing alias can be a known, named, deliberate exception.
    """
    counts = {}
    for pat in live_list:
        counts[pat] = counts.get(pat, 0) + 1
    return sorted(pat for pat, n in counts.items() if n > 1)


# A node in the checker's modelled domain is one or more letters/digits --
# nothing else. '#' and '[...]' are never legal here; '?' and '*' are valid
# ONLY at the whole-pattern boundary (trailing / leading respectively) and
# are stripped from the pattern before nodes are checked against this.
_DOMAIN_NODE = re.compile(r'^[A-Za-z0-9]+$')


def domain_violations(live):
    """Sorted [(pattern, construct)] for live patterns using SCPI syntax
    S1/S2/S3 above do not model -- see the module docstring's DOMAIN GUARD
    section for the full rationale.

    S1/S2/S3 (`style_violations`, `duplicate_pattern_violations`) split a
    pattern on ':' and treat every other character as an ordinary letter.
    libscpi's own parser does not: `matchPattern` (utils.c:478) strips a
    trailing '#' repeat-count suffix and accepts the spelling without it too,
    and `matchCommand` expands a '[...]' optional node into multiple
    accepted spellings. Neither is a character the tokenizer above
    understands, so a live pattern using either could collide with another
    registration at the parser while every check above reports clean.

    Rather than extend the tokenizer to model '#'/'[...]' (combinatorial for
    '[...]', a second matcher for '#'), this refuses any pattern outside the
    domain the existing checks actually reason about: letters and digits,
    ':' node separators, a trailing '?' (and ONLY trailing), a leading '*'
    (and ONLY leading, restricted to the IEEE common commands on the real
    table). The leading '*' and trailing '?' are stripped from the pattern
    before the remaining nodes are checked against `_DOMAIN_NODE`, so a '*'
    or '?' anywhere else in the pattern also fails here (caught by the node
    scan below, since neither survives inside a node once the boundary
    characters are gone).

    No allowlist, matching S3's own reasoning: there is no acceptable
    instance of a construct this checker cannot parse -- the fix for a real
    '#' or '[...]' pattern is to extend the checker, not to except it.
    """
    violations = []
    for pat in sorted(live):
        core = pat[1:] if pat.startswith("*") else pat
        core = core[:-1] if core.endswith("?") else core
        construct = None
        if "#" in core:
            construct = "'#' repeat-count suffix (matchPattern strips it " \
                        "and also accepts the spelling without it)"
        elif "[" in core or "]" in core:
            construct = "'[...]' optional node (matchCommand expands it " \
                        "into multiple accepted spellings)"
        else:
            for node in core.split(":"):
                if _DOMAIN_NODE.match(node):
                    continue
                if "?" in node:
                    construct = "'?' outside the final position"
                elif "*" in node:
                    construct = "'*' outside the first position"
                else:
                    construct = f"non-alphanumeric node {node!r}"
                break
        if construct:
            violations.append((pat, construct))
    return violations


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


def wiki_rows(wiki_dir):
    """[(command, whole row)] for the rows of the COMMAND tables.

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
    rows = []
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
            # Cells of NON-command tables are not consulted at all (#807). They
            # used to be, as a weaker kind of mention, because the legacy-alias
            # table names its commands in the SECOND column and four shipped
            # aliases would otherwise read as undocumented. But an audit
            # mutation-proved that any coincidental cell anywhere in the wiki
            # could then vouch for a command whose real row had been deleted --
            # green, red, green, with the unrelated cell carrying the verdict.
            # Those four are explicit allowlist entries now, which is strictly
            # tighter and says out loud what the fallback only implied.
            if not in_command_table:
                continue
            if set(first) <= set("-: "):          # the |---|---| separator
                continue
            first = clean_cell(first)
            if first:
                rows.append((first, line))
    return rows


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


# (pattern, expect_short_violation, why). Each `pattern` is fed to
# `style_violations` as a singleton set, exactly as a real live-pattern set
# would be -- a colon-free entry exercises S1 on one bare node, a colon-full
# one exercises it inside a real multi-node shape.
STYLE_S1_CASES = [
    ("CONFigure", False, "short CONF, the ordinary mixed-case shape"),
    ("ADC", False, "all-caps, short form is the node itself -- must not "
     "fire just because there is no lowercase tail"),
    ("BQ", False, "shortest legal all-caps node on the real table; pins "
     "that a 2-character short form is INCLUSIVE, not a strict '>'"),
    ("*IDN", False, "IEEE common command -- the leading '*' needs no "
     "special-case handling, `_short_form` already treats it as any other "
     "non-lowercase character"),
    ("chanCALM", True, "the #907 defect itself: starts lowercase, short "
     "form is empty, only the 8-character full spelling is ever legal"),
    ("CHANCALM", False, "the #907 fix: all-caps, one legal spelling, "
     "honestly declared as such"),
    ("Foobar", True, "1-character short form 'F' -- typeable but "
     "identifies nothing; the ticket's own red-case example"),
    ("A::B", True, "an `A::B` typo yields an empty middle node; must be "
     "reported, not crash and not silently pass"),
    ("SYSTem:StartStreamData", True, "the #324 legacy alias -- its "
     "1-character short form 'S' is a real violation that S1 must see "
     "BEFORE the allowlist is applied by the caller"),
]

# (pattern_a, pattern_b, expect_ambiguous, why). Each pair is fed to
# `style_violations` as a two-element set.
STYLE_S2_CASES = [
    ("SYSTem:StartStreamData", "SYSTem:StopStreamData", True,
     "both accept the shared short form SYST:S; findCommandHeader "
     "(parser.c:177) takes the first table match, so SYST:S silently "
     "STARTS a stream rather than stopping one -- pre-existing, #324, "
     "tracked as #1035"),
    ("SYSTem:STReam:START", "SYSTem:STReam:STOP", False,
     "the canonical replacement pair -- START/STOP are both all-caps with "
     "distinct spellings, must NOT be flagged"),
    ("CONFigure:ADC:CHANnel", "CONFigure:ADC:CHANcalm", True,
     "pins that S2 would have caught the #907 ticket's OWN proposed "
     "respelling (CHANcalm collides with CHANnel's short form CHAN) -- the "
     "reason that shape was rejected in favour of all-caps CHANCALM"),
    ("SYSTem:DEVice:NAME", "SYSTem:DEVice:NAME?", False,
     "a setter and its query must never be treated as sharing a spelling"),
]


def _self_test_style():
    """Check S1/S2 against their known cases, then a vacuity guard.

    The vacuity guard is required, not optional: without it, a typo that
    silently widens `STYLE_ALLOWLIST`, or an S1/S2 that never fires at all,
    would leave `--style-only` green on the real table while establishing
    nothing -- the same failure class `scpi_claim_path.py --self-test`
    guards against for its own gate.
    """
    failures = 0
    for pattern, expected, why in STYLE_S1_CASES:
        short_v, _ = style_violations({pattern})
        got = any(p == pattern for p, _n in short_v)
        if got != expected:
            failures += 1
            print(f"  FAIL style S1({pattern!r}) violation={got}, "
                  f"expected {expected} -- {why}")
    for a, b, expected, why in STYLE_S2_CASES:
        _, ambig = style_violations({a, b})
        got = any(a in pair and b in pair for pair, _sp in ambig)
        if got != expected:
            failures += 1
            print(f"  FAIL style S2({a!r}, {b!r}) ambiguous={got}, "
                  f"expected {expected} -- {why}")

    cases = len(STYLE_S1_CASES) + len(STYLE_S2_CASES)

    # Vacuity guard: over the REAL current table, with the allowlist
    # EMPTIED, S1/S2 must report EXACTLY the four known #324 aliases and
    # the one known ambiguous pair between them -- no more, no less. This
    # is skipped (not failed) when no real command table is available,
    # e.g. --self-test invoked with a --scpi path that does not exist.
    scpi_c = "firmware/src/services/SCPI/SCPIInterface.c"
    if os.path.exists(scpi_c):
        cases += 1
        live, _commented, _live_list = registered_patterns(scpi_c)
        short_v, ambig = style_violations(live)
        short_pats = {p for p, _n in short_v}
        if short_pats != STYLE_ALLOWLIST:
            failures += 1
            extra = short_pats - STYLE_ALLOWLIST
            missing = STYLE_ALLOWLIST - short_pats
            print(f"  FAIL style vacuity guard: unfiltered S1 violations on "
                  f"the real table are {sorted(short_pats)}, expected "
                  f"exactly the 4 allowlisted aliases. Unexpected: "
                  f"{sorted(extra)}. Missing: {sorted(missing)}.")
        ambig_pairs_only = {pair for pair, _sp in ambig}
        expected_pair = tuple(sorted({"SYSTem:StartStreamData",
                                       "SYSTem:StopStreamData"}))
        if ambig_pairs_only != {expected_pair}:
            failures += 1
            print(f"  FAIL style vacuity guard: unfiltered S2 ambiguous "
                  f"pairs on the real table are {sorted(ambig_pairs_only)}, "
                  f"expected exactly {{{expected_pair}}}.")
    return cases, failures


def _self_test_duplicate_patterns():
    """Pin the S3 finding from PR #1036's own adversarial audit.

    `registered_patterns()` used to hand S1/S2 only the deduplicated `live`
    SET, so two registrations sharing one exact `.pattern` string -- with
    DIFFERENT callbacks -- collapsed into a single element before either
    check ever saw the table. `spelling_owners` (`style_violations`) then
    counted one owner for every spelling that pattern produces, and S2
    reported nothing, even though `findCommandHeader` (parser.c:177) returns
    on the FIRST table match and the second row is silently dead regardless
    of what its callback does.

    Case 1 drives the real pipeline -- `registered_patterns()` ->
    `duplicate_pattern_violations()` -- over a synthetic table where the same
    pattern string is registered under two DIFFERENT callbacks, proving the
    detector needs (and uses) no callback comparison to catch it: this is
    the exact shape the audit's finding described, run end to end rather than
    against a hand-built list.

    Case 2 mirrors `_self_test_style`'s real-table vacuity guard: on the live
    SCPIInterface.c table the detector must run to completion and report
    ZERO duplicates (294 registrations, `sort | uniq -d` over `.pattern`
    fields is empty, verified 2026-09-11). Unlike S1/S2's vacuity guard --
    which pins four KNOWN pre-existing violations, so a detector gone silent
    changes the count -- there is no live duplicate to demand here, so this
    case alone cannot prove the detector still fires; that proof is case 1.
    What this case pins is that the real `registered_patterns()` ->
    `duplicate_pattern_violations()` path executes against the real table
    without raising and agrees with the known-clean state, so a future
    regression that leaves duplicates in the real table does not go
    unnoticed either. Skipped (not failed) when no real command table is
    available, matching `_self_test_style`'s own vacuity guard.

    Case 3 pins that the S3 verdict actually SURVIVES to `main()`'s exit
    code -- not just that `duplicate_pattern_violations()` computes the
    right list (case 1 already proves that). An opus review of this fix
    found that cases 1/2 alone leave a hole exactly one frame out: nothing
    stopped `print_style_violations`'s `return bool(short_v or ambig or dup)`
    from being written as `return bool(short_v or ambig)` (silently
    dropping `dup`), or `load_and_check_style`'s `style_dup` from being
    computed and never returned -- either mutation leaves case 1 and case 2
    both green while `--style-only` prints the S3 error block and still
    exits 0. That is the exact "called but the result is discarded" class
    `_self_test_style_only_entry` already exists to catch for `self_test()`
    itself, one channel over. This drives `main()` itself with
    `--style-only` on the duplicate-pattern table from case 1 and asserts a
    non-zero exit, using the same module-level `self_test` spy
    `_self_test_style_only_entry` uses -- required so `main()`'s own
    unconditional `self_test()` call does not recurse back into this
    function.
    """
    import contextlib
    import io
    import tempfile
    cases, failures = 2, 0  # case 1 (detector) + case 3 (verdict wiring)

    src = ('const scpi_command_t scpi_commands[] = {\n'
           '    {.pattern = "SYSTem:DEVice:NAME", .callback = SCPI_A,},\n'
           '    {.pattern = "SYSTem:DEVice:NAME", .callback = SCPI_B,},\n'
           '    {.pattern = NULL, .callback = SCPI_NotImplemented,},\n};\n')
    with tempfile.TemporaryDirectory() as d:
        c = os.path.join(d, "scpi.c")
        with open(c, "w", encoding="utf-8") as fh:
            fh.write(src)
        _live, _commented, live_list = registered_patterns(c)
        dup = duplicate_pattern_violations(live_list)
        if dup != ["SYSTem:DEVice:NAME"]:
            failures += 1
            print(f"  FAIL S3 duplicate-pattern: a pattern registered twice "
                  f"under two DIFFERENT callbacks reported {dup!r}, expected "
                  f"['SYSTem:DEVice:NAME'] -- the second registration is "
                  f"unreachable (findCommandHeader takes the first table "
                  f"match) regardless of what its callback does")

        # Case 3: the verdict must reach main()'s exit code, not just the
        # printed diagnostic.
        global self_test
        real_self_test = self_test
        argv_saved = sys.argv

        def _spy_pass():
            return 0

        try:
            self_test = _spy_pass
            sys.argv = ["scpi_wiki_sync.py", "--style-only", "--scpi", c]
            with contextlib.redirect_stdout(io.StringIO()):
                rc = main()
        finally:
            self_test = real_self_test
            sys.argv = argv_saved
        if rc == 0:
            failures += 1
            print("  FAIL S3 verdict wiring: main() --style-only returned 0 "
                  "on a table with a duplicate pattern -- the S3 finding was "
                  "computed but discarded somewhere between "
                  "duplicate_pattern_violations() and the exit code")

    scpi_c = "firmware/src/services/SCPI/SCPIInterface.c"
    if os.path.exists(scpi_c):
        cases += 1
        _live, _commented, live_list = registered_patterns(scpi_c)
        dup = duplicate_pattern_violations(live_list)
        if dup:
            failures += 1
            print(f"  FAIL S3 vacuity guard: the real command table has "
                  f"duplicate pattern string(s) {dup!r} -- "
                  f"findCommandHeader silently drops every registration "
                  f"after the first one.")
    return cases, failures


# (pattern, expect_violation, why). Each `pattern` is fed to
# `domain_violations` as a singleton set, exactly as a real live-pattern set
# would be.
DOMAIN_CASES = [
    ("SYSTem:DEVice:NAME", False,
     "the ordinary shape -- letters, digits, ':' separators only"),
    ("*IDN?", False,
     "leading '*' and trailing '?' are both inside the modelled domain -- "
     "one of the real table's own IEEE common commands"),
    ("SYSTem:DEVice:NAME#", True,
     "libscpi's matchPattern strips a trailing '#' repeat-count suffix and "
     "accepts the spelling without it too (utils.c:478) -- S1/S2/S3 "
     "split on ':' only and would treat '#' as an ordinary letter"),
    ("SYSTem:DEVice[:NAME]", True,
     "libscpi's matchCommand expands a '[...]' optional node into multiple "
     "accepted spellings -- S1/S2/S3 would treat '[' and ']' as ordinary "
     "letters"),
]


def _self_test_domain_guard():
    """Pin the domain-guard finding from PR #1036's SECOND adversarial audit
    round -- one level over S1/S2/S3.

    S1/S2/S3 (`style_violations`, `duplicate_pattern_violations`) tokenize a
    pattern by splitting on ':' and treating every other character as an
    ordinary letter. libscpi's own parser does not: `matchPattern`
    (utils.c:478) strips a trailing '#' repeat-count suffix and defers
    to `compareStrAndNum`, which ALSO accepts the spelling with that suffix
    omitted, and `matchCommand` expands a '[...]' optional node into
    multiple accepted spellings. A pattern using either construct could
    collide with another registration at the parser -- winning or losing in
    `findCommandHeader` by table order -- while every check above prints
    clean, because none of them knows '#'/'['/']' is anything other than an
    ordinary character.

    Case 1/2 (via `DOMAIN_CASES`) drive `domain_violations` directly against
    synthetic single-pattern sets carrying each construct, proving the
    detector fires on both -- and does NOT fire on the ordinary shapes
    (plain node, and the real table's own leading-'*'/trailing-'?' IEEE
    common-command shape), which is the vacuity check for false positives.

    Case 3 is the real-table vacuity guard mirroring `_self_test_style`'s
    and `_self_test_duplicate_patterns`'s own: on the live SCPIInterface.c
    table, `domain_violations` must report NOTHING (294 registrations,
    characters in use are letters, digits, ':', a trailing '?', and a
    leading '*' on 13 IEEE common commands -- `*CLS *ESE *ESE? *ESR? *IDN?
    *OPC *OPC? *RST *SRE *SRE? *STB? *TST? *WAI`, all verified individually
    clean; measured 2026-09-11. An earlier count of 8 missed the five
    trailing entries -- corrected here after an opus review caught it, and
    noted since the guard's own vacuity claim must not repeat a miscount).
    If this ever fires, the domain was mis-specified against what actually
    ships and must be widened to match it, not loosened until it passes.

    Case 4 pins that the finding actually SURVIVES to `main()`'s exit code,
    not just that `domain_violations()` computes the right list (case 1
    already proves that) -- the exact "computed but discarded before the
    exit code" hole an opus review found one round earlier on this same
    file, for S3's own verdict wiring (see `_self_test_duplicate_patterns`'s
    case 3). This drives `main()` itself with `--style-only` on a table
    carrying the '#' pattern from case 1 and asserts a NON-ZERO exit, using
    the same module-level `self_test` spy `_self_test_duplicate_patterns`
    and `_self_test_style_only_entry` use -- required so `main()`'s own
    unconditional `self_test()` call does not recurse back into this
    function.
    """
    import contextlib
    import io
    import tempfile
    cases, failures = len(DOMAIN_CASES), 0  # cases 1/2 folded into the table

    for pattern, expected, why in DOMAIN_CASES:
        v = domain_violations({pattern})
        got = any(p == pattern for p, _c in v)
        if got != expected:
            failures += 1
            print(f"  FAIL domain guard({pattern!r}) violation={got}, "
                  f"expected {expected} -- {why}")

    # Case 3: real-table vacuity guard.
    scpi_c = "firmware/src/services/SCPI/SCPIInterface.c"
    if os.path.exists(scpi_c):
        cases += 1
        live, _commented, _live_list = registered_patterns(scpi_c)
        v = domain_violations(live)
        if v:
            failures += 1
            print(f"  FAIL domain guard vacuity: the real command table has "
                  f"pattern(s) outside the checker's modelled domain "
                  f"{v!r} -- if this syntax is genuinely shipping, S1/S2/S3 "
                  f"need a real extension, not a loosened guard.")

    # Case 4: the verdict must reach main()'s exit code under --style-only.
    cases += 1
    src = ('const scpi_command_t scpi_commands[] = {\n'
           '    {.pattern = "SYSTem:DEVice:NAME#", .callback = SCPI_A,},\n'
           '    {.pattern = NULL, .callback = SCPI_NotImplemented,},\n};\n')
    with tempfile.TemporaryDirectory() as d:
        c = os.path.join(d, "scpi.c")
        with open(c, "w", encoding="utf-8") as fh:
            fh.write(src)
        global self_test
        real_self_test = self_test
        argv_saved = sys.argv

        def _spy_pass():
            return 0

        try:
            self_test = _spy_pass
            sys.argv = ["scpi_wiki_sync.py", "--style-only", "--scpi", c]
            with contextlib.redirect_stdout(io.StringIO()):
                rc = main()
        finally:
            self_test = real_self_test
            sys.argv = argv_saved
        if rc == 0:
            failures += 1
            print("  FAIL domain guard verdict wiring: main() --style-only "
                  "returned 0 on a table with a '#' pattern -- the domain "
                  "guard finding was computed but discarded somewhere "
                  "between domain_violations() and the exit code")
    return cases, failures


def _self_test_style_only_entry():
    """Pin that `main()`'s `--style-only` branch runs `self_test()` first.

    Qodo review on #907 (PR #1036) found `main()` returned through the
    `args.style_only` branch straight into `load_and_check_style()` --
    never reaching `self_test()`, so the vacuity guard above (and every
    other self-test case) was silently skipped in this mode even though the
    `--wiki` path's `if self_test() != 0: return 1` runs it unconditionally.
    A regression that made S1/S2 stop reporting anything would leave
    `--style-only` green -- exactly the "check that cannot fail" class this
    guard is supposed to prevent, just one call frame further out.

    This does not read the source for the fix (that is what let the bug
    ship in the first place); it drives `main()` itself with `--style-only`
    on `sys.argv`, with the module-level `self_test` name swapped for a spy,
    and checks two things a bare "was it called" assertion would not:

    1. `self_test()` (the spy) is actually invoked from the `--style-only`
       branch, not only from the `--wiki` branch.
    2. A non-zero `self_test()` result actually stops `main()` from
       returning 0 -- so a future regression that calls `self_test()` but
       discards its return value (as easy a mistake as never calling it)
       still fails this case instead of reading as fixed.
    """
    import contextlib
    import io
    import tempfile
    global self_test
    real_self_test = self_test
    argv_saved = sys.argv
    failures = 0
    cases = 2

    # A minimal but valid table -- real enough that main()'s style scan
    # (which runs after the spy, on a genuine self_test() pass) has
    # something to load without touching the real firmware source or cwd.
    src = ('const scpi_command_t scpi_commands[] = {\n'
           '    {.pattern = "SYSTem:DEVice:NAME", .callback = SCPI_A,},\n'
           '    {.pattern = NULL, .callback = SCPI_NotImplemented,},\n};\n')
    with tempfile.TemporaryDirectory() as d:
        c = os.path.join(d, "scpi.c")
        with open(c, "w", encoding="utf-8") as fh:
            fh.write(src)

        # (1) self_test() must be CALLED from the --style-only branch.
        calls = []

        def _spy_pass():
            calls.append(True)
            return 0

        try:
            self_test = _spy_pass
            sys.argv = ["scpi_wiki_sync.py", "--style-only", "--scpi", c]
            with contextlib.redirect_stdout(io.StringIO()):
                main()
        finally:
            self_test = real_self_test
            sys.argv = argv_saved
        if not calls:
            failures += 1
            print("  FAIL style-only entry: main() did not call self_test() "
                  "from the --style-only branch -- the --wiki path's "
                  "`if self_test() != 0: return 1` guard is skipped here, "
                  "so a broken S1/S2 detector would leave --style-only "
                  "green")

        # (2) a non-zero self_test() result must stop main() from
        # returning 0 -- catches "called but ignored", not just "never
        # called".
        def _spy_fail():
            return 1

        try:
            self_test = _spy_fail
            sys.argv = ["scpi_wiki_sync.py", "--style-only", "--scpi", c]
            with contextlib.redirect_stdout(io.StringIO()):
                rc = main()
        finally:
            self_test = real_self_test
            sys.argv = argv_saved
        if rc == 0:
            failures += 1
            print("  FAIL style-only entry: main() returned 0 from "
                  "--style-only even though self_test() reported failure -- "
                  "the return value must gate execution, not just be "
                  "called")

    return cases, failures


def self_test():
    """Check the abbreviation rule and the #907 house-style gate.

    Kept in the tool rather than a side file so it cannot drift away from the
    functions it covers, and so CI runs it for free.
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
    style_cases, style_failures = _self_test_style()
    failures += style_failures
    dup_cases, dup_failures = _self_test_duplicate_patterns()
    failures += dup_failures
    domain_cases, domain_failures = _self_test_domain_guard()
    failures += domain_failures
    entry_cases, entry_failures = _self_test_style_only_entry()
    failures += entry_failures
    if failures:
        print(f"\n::error::{failures} self-test(s) failed")
        return 1
    print(f"self-test: {len(SELF_TEST_CASES)}/{len(SELF_TEST_CASES)} matcher "
          f"cases + {e2e_cases}/{e2e_cases} end-to-end cases + "
          f"{style_cases}/{style_cases} house-style cases + "
          f"{dup_cases}/{dup_cases} duplicate-pattern cases + "
          f"{domain_cases}/{domain_cases} domain-guard cases + "
          f"{entry_cases}/{entry_cases} style-only entry cases pass")
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
        live, _, _live_list = registered_patterns(c)
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
        rows = wiki_rows(wiki)
        written = [cmd for cmd, _ in rows]
        if any(is_form_of(w, "SYSTem:POWer:OTG") for w in written):
            print("  FAIL end-to-end: a command mentioned only in prose counted"
                  " as documented -- the gate can be satisfied without writing"
                  " a row")
            failures += 1

        # (3) A cell in an UNRELATED table must not vouch for a missing command
        # row -- including when it spells the command EXACTLY. An audit
        # reproduced both forms on the real wiki: deleting the DIO:COUNter? row
        # left the checker green while another table happened to contain the
        # abbreviation DIO:COUN? (fixed in #805), and again while another table
        # contained the exact text DIO:COUNter? (#807, fixed here). Both cells
        # below are present at once, so this fails if either path returns.
        wiki2 = os.path.join(d, "wiki2")
        os.makedirs(wiki2)
        with open(os.path.join(wiki2, "01.md"), "w", encoding="utf-8") as fh:
            fh.write("| SCPI Command | Description |\n| -- | -- |\n"
                     "| SYSTem:REboot | x |\n\n"
                     "| Signal | Example |\n| -- | -- |\n"
                     "| counter note | DIO:COUN? |\n"
                     "| exact note | DIO:COUNter? |\n")
        rows2 = wiki_rows(wiki2)
        written2 = [cmd for cmd, _ in rows2]
        if any(is_form_of(w, "DIO:COUNter?") for w in written2):
            print("  FAIL end-to-end: a cell in an unrelated table vouched for "
                  "a command with no row of its own")
            failures += 1
    return cases, failures


def load_and_check_style(scpi_c):
    """(live, commented, style_short, style_ambig, style_dup, style_domain)
    for --scpi's table.

    `style_short`/`style_ambig` already have `STYLE_ALLOWLIST` applied.
    `style_dup` and `style_domain` do NOT -- see `duplicate_pattern_violations`
    and `domain_violations` for why neither an exact-duplicate pattern string
    nor a pattern outside the checker's modelled syntax has an allowlistable
    case. Shared by `--style-only` and the normal wiki-comparison flow so the
    two do not diverge on how the table is loaded or the allowlist is
    applied.
    """
    live, commented, live_list = registered_patterns(scpi_c)
    if not live:
        sys.exit(f"error: no .pattern entries found in {scpi_c!r} -- "
                 f"has the command table moved?")
    style_short, style_ambig = style_violations(live)
    style_short, style_ambig = apply_style_allowlist(
        style_short, style_ambig, STYLE_ALLOWLIST)
    style_dup = duplicate_pattern_violations(live_list)
    style_domain = domain_violations(live)
    return live, commented, style_short, style_ambig, style_dup, style_domain


def print_style_violations(short_v, ambig, dup, domain):
    """Print #907 house-style findings (S1, S2, S3, domain guard); return
    True if any remain."""
    if short_v:
        print(f"\n::error::{len(short_v)} SCPI node(s) have a short form "
              f"under 2 characters:")
        for pat, node in short_v:
            print(f"    {pat}  (node {node!r})")
        print("\n  A node starting lowercase has an EMPTY short form -- only")
        print("  the full spelling is ever legal, silently. Respell the node")
        print("  so its caps-prefix run is at least 2 characters (an")
        print("  all-caps node, one legal spelling, is always fine). See")
        print("  CLAUDE.md's SCPI Abbreviation Rule house-style note.")
    if ambig:
        print(f"\n::error::{len(ambig)} pair(s) of registered patterns "
              f"accept the SAME spelling:")
        for pair, spellings in ambig:
            print(f"    {' <-> '.join(pair)}  via {', '.join(spellings)}")
        print("\n  findCommandHeader takes the FIRST table match, so every")
        print("  pattern but the winner is silently unreachable by that")
        print("  spelling -- no error, just the wrong command. Respell one")
        print("  side so their short forms diverge.")
    if dup:
        print(f"\n::error::{len(dup)} pattern string(s) are registered more "
              f"than once:")
        for pat in dup:
            print(f"    {pat}")
        print("\n  findCommandHeader takes the FIRST table match, so every")
        print("  registration after the first with this EXACT pattern string")
        print("  is silently unreachable no matter what its callback does --")
        print("  no error, just dead code. Give it a distinct pattern string,")
        print("  or remove the duplicate registration.")
    if domain:
        print(f"\n::error::{len(domain)} pattern(s) use SCPI syntax this "
              f"checker cannot model:")
        for pat, construct in domain:
            print(f"    {pat}  ({construct})")
        print("\n  S1/S2/S3 above split a pattern on ':' only and treat")
        print("  every other character as an ordinary letter -- they do not")
        print("  know libscpi's own special syntax: matchPattern")
        print("  (utils.c:478) strips a trailing '#' repeat-count suffix")
        print("  and also accepts the spelling with it omitted, and")
        print("  matchCommand expands a '[...]' optional node into multiple")
        print("  accepted spellings. A pattern using either construct could")
        print("  collide with another registration at the PARSER while")
        print("  every check above reports clean, because they would treat")
        print("  '#'/'['/']' as ordinary characters instead. Respell the")
        print("  pattern within the modelled domain (letters and digits,")
        print("  ':' separators, a trailing '?', a leading '*'), or extend")
        print("  this checker to understand the construct before")
        print("  registering it.")
    return bool(short_v or ambig or dup or domain)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scpi", default="firmware/src/services/SCPI/SCPIInterface.c")
    ap.add_argument("--self-test", action="store_true",
                    help="check the abbreviation matcher and exit")
    ap.add_argument("--style-only", action="store_true",
                    help="check the #907 house-style gate (S1/S2/S3 + domain "
                         "guard) over --scpi and exit -- no --wiki clone "
                         "needed")
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

    if args.style_only:
        if self_test() != 0:  # a broken matcher/detector makes this verdict junk --
            return 1           # same guard the --wiki path takes below, not skipped here
        live, commented, style_short, style_ambig, style_dup, style_domain = \
            load_and_check_style(args.scpi)
        print(f"registered SCPI commands : {len(live)}")
        print(f"commented-out patterns   : {len(commented)} (not shipped, ignored)")
        print(f"house-style allowlist    : {len(STYLE_ALLOWLIST)} pattern(s)")
        has_violations = print_style_violations(
            style_short, style_ambig, style_dup, style_domain)
        if not has_violations:
            print("\nOK: no house-style violations (#907).")
            return 0
        return 1

    if not args.wiki:
        ap.error("--wiki is required (or use --self-test / --style-only)")

    if self_test() != 0:      # a broken matcher makes every verdict below junk
        return 1

    # #907 house style. Checked over the WHOLE live table on every run (see
    # the module docstring's HOUSE STYLE section for why there is no
    # diff-aware mechanism), and FATAL unconditionally -- unlike `ghosts`
    # below, there is no scheduled-run ordering trap to excuse it.
    live, commented, style_short, style_ambig, style_dup, style_domain = \
        load_and_check_style(args.scpi)
    rows = wiki_rows(args.wiki)
    allow = load_allowlist(args.allow)
    style_violated = print_style_violations(
        style_short, style_ambig, style_dup, style_domain)

    written = [cmd for cmd, _ in rows]
    # Documented means a COMMAND-TABLE ROW names it. Nothing else counts.
    #
    # Two looser rules were tried, and each was mutation-proved to be carrying
    # the verdict itself. "Appears anywhere on the page" let a single prose
    # sentence vouch for a missing row -- deleting the sentence flipped the
    # checker red. "Appears in any table cell" let a coincidental cell in an
    # unrelated table do the same (#807): narrower, but still a false pass, and
    # still invisible to whoever deleted the row.
    #
    # A command that genuinely has no row of its own belongs in the allowlist,
    # where the entry must state why -- which turns an implicit rule into a
    # decision someone wrote down.
    undocumented = sorted(
        p for p in live
        if p not in allow
        and not any(is_form_of(w, p) for w in written))

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
    print(f"house-style allowlist    : {len(STYLE_ALLOWLIST)} pattern(s)")

    fatal_ghosts = ghosts and not args.ghosts_warn_only
    if not undocumented and not ghosts and not style_violated:
        print("\nOK: the wiki and the command table agree, and the #907 "
              "house-style gate is clean.")
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

    return 1 if (undocumented or fatal_ghosts or style_violated) else 0


if __name__ == "__main__":
    sys.exit(main())
