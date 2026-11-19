#!/usr/bin/env python3
"""A SCPI command without `?` must not write to the response stream (#1274).

## The hazard

In IEEE 488.2 a program message without `?` produces no response, so a client
that sends one does not read a reply. Any text the device writes therefore
stays in the input buffer and **the next query reads it as its own answer**.
Measured on hardware at `4ec4f91c`: `SYSTem:LOG:CLEar` then `*IDN?`, written
back to back, puts `Log cleared` where the identity belongs; `SD:CRC` with the
card disabled puts two lines there. Nothing is lost -- the reply is
MISATTRIBUTED, and the number of stray lines varies with which command ran
before it, so no fixed skip count on the client side can fix it.

This gate is **stage 1** of #1274: stop new instances. It changes no firmware
behaviour. Removing the existing texts is stage 3 and waits on daqifi-core,
which parses two of them (`LOG:LEVel`'s acknowledgement is its return value,
and the SD paths match `No SD Card Detected`), so the allowlist below is the
population at the time of writing, not an endorsement of it.

## What this gate claims

For every entry in the command table whose pattern has no `?`, the registered
callback must not reach a response-stream write -- `scpi_printf`,
`SCPI_Result*`, `SCPI_Write*`, or a `->write(...)` transport call -- directly
or through any call chain inside our own sources.

The allowlist is **shrink-only**: an entry that no longer writes FAILS the
gate. Otherwise the list rots quietly as stage 3 lands, and a file of stale
exemptions reads exactly like a file of live ones. Same both-directions rule
as `log_budget.py`.

## What it does NOT claim

- **Nothing about reachability of a branch.** A write behind a condition that
  can never be true still counts. This is a textual call graph with no control
  flow, the same stated limit as `scpi_claim_path.py` (#896).
- **Nothing about the preprocessor.** `cdef`'s view blanks directives but does
  not evaluate them, so a call inside `#if 0` still counts. Over-approximating
  here produces a FALSE POSITIVE, which costs an argument; the opposite costs
  a missed defect.
- **Nothing about `**ERROR` lines.** Every queued error also prints one, from
  the transport error callbacks (`SCPI_USB_Error`, `SCPI_TCP_Error`). That is
  broader than these commands: it fires for every queued error, on any
  command, query or not. It IS part of #1274 (the operator's scope call:
  "stay true and consistent to scpi"), but it is stage 3's removal, not
  stage 1's gate -- the error callbacks are not table callbacks, so this
  gate cannot see them and must not be widened to guess at them.
- **Nothing about whether a `?` command writes.** A query SHOULD write. Those
  are reported only under `--list` as context.

## Why libscpi is opaque

`scpi_printf` and the `SCPI_Result*` family are DEFINED in
`firmware/src/libraries/scpi/libscpi/src/`. Following calls into that
directory makes every caller of a libscpi helper a "writer" -- in an earlier
throwaway scan `SCPI_ParamCopyText` reached `SCPI_Result*` and dragged
innocent callbacks in with it. So the scan set excludes `libraries/`, exactly
as `cppcheck.sh` does, and the sinks are matched as CALLS rather than followed
into.

⛔ An earlier revision of this paragraph justified that exclusion by claiming
"a vendor or PLIB function cannot write to the SCPI stream because it has no
access to the context, so treating an unresolved callee as a leaf is sound".
**That was FALSE in the one case that matters, and the audit caught it.** A
registered table callback is HANDED the context by libscpi, and eight live
non-query registrations (`*CLS`, `*ESE`, `*OPC`, `*SRE`, `*WAI`,
`STATus:PRESet`, `STATus:OPERation:ENABle`, `STATus:QUEStionable:ENABle`)
resolve into exactly that excluded code. They were being SKIPPED while the
gate reported clean -- a silent pass on commands it never looked at.

Two things make the exclusion sound now instead of merely convenient:

* an unresolved NON-QUERY callback is REFUSED, and the eight are declared in
  `scpi_nonquery_write-unresolved.txt` against a reason that was CHECKED --
  the same reachability built over `libscpi/src` (228 functions, 21 of them
  direct writers) finds no sink from any of the eight, with a control proving
  that analysis does detect libscpi's real writers (every `*Q` query reaches
  one);
* an ordinary unresolved CALLEE deeper in a chain is still treated as a leaf,
  which is sound for a vendor/PLIB function that genuinely never receives the
  context -- and only while every one of our own sources is in the scan set,
  which `_require_scan_set` enforces and which now includes HEADERS.

## Why a function-pointer argument must be followed or refused

`SCPI_RunSessionStartClaimed(context, SCPI_WifiFindRateClaimed, ...)` passes a
callback that writes. An edge rule keyed on "identifier followed by `(`" never
sees it, and the resulting miss is a FALSE NEGATIVE -- the gate reports a pass
on a command that writes. So an argument that resolves to a known function
becomes an edge, and an indirection this file cannot resolve is REFUSED
(reported, gate fails) rather than assumed harmless. A refusal costs a human
five minutes; a silent miss is the defect shipping.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from cdef import (AmbiguousDefinition, compiler_view,  # noqa: E402
                  line_comment_end, literal_end, splice)

# ---------------------------------------------------------------------------
# The command table.
#
# Copied in SHAPE from scpi_wiki_sync.registered_patterns, not imported: the
# repo's convention is that the shared primitives live in cdef.py and the
# gates do not depend on each other (scpi_sd_arm_path.py says the same of
# scpi_claim_path.py). What is deliberately carried over is the
# PARSE-COMPLETENESS GUARD -- an entry spelled in a form the regex does not
# understand must fail the gate, not be dropped. A checker that silently skips
# the command it was written to catch reports a pass.
# ---------------------------------------------------------------------------
_REGISTRATION = re.compile(
    r'\.pattern\s*=\s*((?:"[^"]*"\s*)+),\s*\.callback\s*=\s*([A-Za-z_]\w*)')
_PATTERN_FIELD = re.compile(r"\.pattern\s*=")
_NULL_SENTINEL = re.compile(r"\.pattern\s*=\s*NULL\b")
_ONE_LITERAL = re.compile(r'"([^"]*)"')

# The response-stream sinks. `SCPI_Result` and `SCPI_Write` are matched as
# PREFIXES so a newly added result function is covered the day it lands,
# rather than the day someone remembers to extend a list -- the gate's failure
# co-occurs with the thing it guards.
_SINK = re.compile(r"\bSCPI_Result\w*\s*\(|\bSCPI_Write\w*\s*\(|"
                   r"\bscpi_printf\s*\(|(?:->|\.)\s*write\s*\(")

# An indirect call through a member. `->write(`/`.write(` is the transport
# sink above; any OTHER member call inside the examined closure is an
# indirection this file cannot resolve, and is refused.
_MEMBER_CALL = re.compile(r"(?:->|\.)\s*([A-Za-z_]\w*)\s*\(")
_DIRECT_CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
# `.flush = SCPI_USB_Flush` / `ctx.active = MainState` -- a function assigned
# to a struct field, which is how a vtable call becomes resolvable.
_MEMBER_ASSIGN = re.compile(
    r"(?:\.|->)\s*([A-Za-z_]\w*)\s*=\s*&?\s*([A-Za-z_]\w*)\s*[,;}]")

_EXCLUDED = ("third_party", "libraries", "config")


def joined_text(raw):
    """The splice-joined text, comments blanked, LITERALS KEPT.

    The table parse needs the `.pattern` strings, so it cannot read cdef's
    `mask()`/matchmask -- those blank literals too, which erases the very
    thing being parsed. (My first version did exactly that and the parse
    guard caught it: 1 declared field, 0 parsed.) It still must not read RAW
    text: a backslash-newline can CREATE a comment marker, and #1066 is open
    against `scpi_wiki_sync.strip_c_comments` for precisely that. So splice
    first -- cdef's own phase 2 -- then blank comments only, ending each
    literal and line comment exactly where cdef ends it, so this view and the
    code view cannot disagree about where either begins.
    """
    text = splice(raw)[0]
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch in ('"', "'"):
            i = literal_end(text, i, already_spliced=True)   # kept verbatim
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            end = line_comment_end(text, i, already_spliced=True)
        elif ch == "/" and i + 1 < n and text[i + 1] == "*":
            found = text.find("*/", i + 2)
            end = n if found < 0 else found + 2
        else:
            i += 1
            continue
        for j in range(i, end):
            if out[j] != "\n":
                out[j] = " "                 # length and lines preserved
        i = end
    return "".join(out)


def registered(scpi_c):
    """[(pattern, callback)] in table order, with a completeness guard."""
    if not os.path.exists(scpi_c):
        sys.exit("error: %r not found. If the SCPI command table moved, pass "
                 "--scpi <path> and update "
                 ".github/workflows/scpi-nonquery-write.yml." % scpi_c)
    with open(scpi_c, encoding="utf-8", errors="replace") as fh:
        raw = fh.read()
    live_src = joined_text(raw)
    rows = [(_joined_literal(g), cb)
            for g, cb in _REGISTRATION.findall(live_src)]
    declared = (len(_PATTERN_FIELD.findall(live_src))
                - len(_NULL_SENTINEL.findall(live_src)))
    if declared != len(rows):
        sys.exit("error: %r has %d live '.pattern =' command fields but only "
                 "%d parsed as registrations. An entry uses a form this "
                 "checker does not understand and would be silently omitted "
                 "-- so a non-query command that writes could never be "
                 "reported. Extend _REGISTRATION in "
                 "tools/lint/scpi_nonquery_write.py."
                 % (scpi_c, declared, len(rows)))
    if not rows:
        sys.exit("error: %r parsed ZERO command registrations. The gate would "
                 "examine nothing and report a pass." % scpi_c)
    return rows


def _joined_literal(group):
    """The C value of one or more adjacent string literals."""
    return "".join(_ONE_LITERAL.findall(group))


# ---------------------------------------------------------------------------
# The call graph over OUR sources.
# ---------------------------------------------------------------------------
def source_files(root):
    """Every `.c` AND `.h` of ours under `root`, excluding the vendored trees.

    ⛔ HEADERS ARE NOT OPTIONAL (audit r1). A `.c`-only sweep made every
    header-defined helper a silent LEAF, and live non-query callbacks already
    reach into `SCPIInterface.h` -- `SCPI_RejectCfgClaim`,
    `SCPI_OptionalParamInt32`. A write behind one of those would have passed
    the gate, which is the failure direction that matters: silence.
    """
    out = []
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in _EXCLUDED]
        rel = os.path.relpath(base, root).replace(os.sep, "/")
        if any(part in _EXCLUDED for part in rel.split("/")):
            continue
        for f in sorted(files):
            if f.endswith(".c") or f.endswith(".h"):
                out.append(os.path.join(base, f))
    return sorted(out)


class Graph(object):
    """Function bodies, call edges and sink ownership across our sources."""

    def __init__(self):
        self.bodies = {}        # (relpath, name) -> body text, compiler view
        self.by_name = {}       # name -> [ (relpath, name) ]
        self.file_code = {}     # relpath -> whole-file compiler view
        self.by_member = {}     # member name -> {(relpath, name)}
        self.writes = set()     # nodes with a DIRECT sink call
        self.refusals = []      # unresolvable indirection, as messages

    def add_file(self, relpath, src):
        view = compiler_view(src)
        self.file_code[relpath] = view.code
        for name, start, end in view.spans:
            node = (relpath, name)
            # A name defined twice in one file is cdef's AmbiguousDefinition
            # case; keep both bodies so neither is silently dropped.
            self.bodies[node] = self.bodies.get(node, "") + view.code[start:end]
            if node not in self.by_name.setdefault(name, []):
                self.by_name[name].append(node)

    def resolve_all(self, name, from_file):
        """Every node a call to `name` from `from_file` could reach.

        Same-file first: a `static` definition is file-local, so a local one
        is the only candidate. Otherwise EVERY definition of the name, not
        one guessed winner. This repo really does define one name in several
        files on purpose -- `NqBoardConfig_Get` in NQ1/NQ2/NQ3BoardConfig.c,
        one per build variant -- and `wifi_tcp_server_WriteBuffer` has an
        `_mutex_example` twin. Picking one would be a coin flip whose wrong
        side is a MISSED write, and refusing would fail the gate on a
        perfectly ordinary pattern. Taking all of them is sound in the safe
        direction: if any candidate writes, the caller is reported.
        """
        cands = self.by_name.get(name, [])
        local = [n for n in cands if n[0] == from_file]
        return local if local else cands        # [] = external/vendor leaf

    def index_members(self):
        """member name -> every function assigned to a field of that name.

        A vtable field IS resolvable and so must be FOLLOWED, not refused:
        libscpi's interface is exactly this shape, and `->flush(...)` in
        SCPI_Reset reaches `SCPI_USB_Flush`/`SCPI_TCP_Flush` through
        `.flush = ...` designated initializers. Those initializers sit at
        FILE scope, not in any body, which is why this reads whole-file text.

        Deliberately coarse: keyed on the member NAME alone, so two unrelated
        structs sharing a field name merge. That over-approximates, and
        over-approximation here costs an argument rather than a missed write.
        """
        for relpath, code in self.file_code.items():
            for m in _MEMBER_ASSIGN.finditer(code):
                member, target = m.group(1), m.group(2)
                for node in self.resolve_all(target, relpath):
                    self.by_member.setdefault(member, set()).add(node)

    def edges(self, node):
        """Callees of `node`: direct calls, function-pointer arguments, and
        calls through a member whose assignments we could find."""
        body = self.bodies.get(node, "")
        relpath = node[0]
        out = set()
        for m in _DIRECT_CALL.finditer(body):
            out.update(self.resolve_all(m.group(1), relpath))
        for name in _pointer_arguments(body, self.by_name):
            out.update(self.resolve_all(name, relpath))
        for m in _MEMBER_CALL.finditer(body):
            out.update(self.by_member.get(m.group(1), ()))
        return out

    def unresolved_members(self, node):
        """Member calls in `node` that no assignment explains."""
        out = set()
        for m in _MEMBER_CALL.finditer(self.bodies.get(node, "")):
            member = m.group(1)
            if member != "write" and member not in self.by_member:
                out.add(member)
        return out


def _split_top_level(text):
    """`text` split on commas that are not inside brackets."""
    parts, depth, cur = [], 0, []
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    parts.append("".join(cur))
    return parts


_ARG_STRIP = re.compile(r"^[\s&*()]*|[\s()]*$")
_CAST = re.compile(r"^\s*\([^()]*\)\s*")


def _pointer_arguments(body, by_name):
    """Known function names passed as ARGUMENTS, not called.

    This is the hole that makes a miss a false negative: a callback handed to
    a runner is invoked, but no `(` follows its name at the pass site.
    """
    found = set()
    for m in _DIRECT_CALL.finditer(body):
        open_paren = m.end() - 1
        depth, i = 0, open_paren
        while i < len(body):
            if body[i] in "([{":
                depth += 1
            elif body[i] in ")]}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        for arg in _split_top_level(body[open_paren + 1:i]):
            cleaned = _CAST.sub("", arg)
            cleaned = _ARG_STRIP.sub("", cleaned).strip()
            if cleaned in by_name:
                found.add(cleaned)
    return found


def _require_scan_set(files, root):
    """Refuse a scan set that cannot contain the subject.

    Treating an unresolved callee as a leaf is sound ONLY while every one of
    our own sources is present. An empty or truncated set would report a
    clean pass having examined nothing.
    """
    problems = []
    if not files:
        problems.append("the scan set is EMPTY: no .c files under %r" % root)
    rels = {os.path.relpath(f, root).replace(os.sep, "/") for f in files}
    need = {"services/SCPI/SCPIInterface.c", "services/SCPI/SCPIStorageSD.c"}
    missing = sorted(need - rels)
    if missing:
        problems.append("the scan set is missing %s, so a callback defined "
                        "there would resolve to nothing and read as silent"
                        % ", ".join(missing))
    return problems


# ---------------------------------------------------------------------------
# The check.
# ---------------------------------------------------------------------------
def analyse(root, scpi_c):
    """(findings, queries_that_write, refusals, stats).

    `findings` is [(pattern, callback, route)] for non-query commands that
    reach a sink; `route` names the first sink-owning function on the path.
    """
    files = source_files(root)
    problems = _require_scan_set(files, root)
    if problems:
        return [], [], problems, {}

    g = Graph()
    for path in files:
        rel = os.path.relpath(path, root).replace(os.sep, "/")
        with open(path, encoding="utf-8", errors="replace") as fh:
            try:
                g.add_file(rel, fh.read())
            except AmbiguousDefinition as exc:
                g.refusals.append("%s: %s" % (rel, exc))

    g.index_members()           # after every file: an initializer may be anywhere

    for node, body in g.bodies.items():
        if _SINK.search(body):
            g.writes.add(node)

    # Fixed point over the edges. `route` records WHY a node is in the set, so
    # a finding can name the chain instead of asserting one.
    reaches = {n: n for n in g.writes}
    changed = True
    while changed:
        changed = False
        for node in list(g.bodies):
            if node in reaches:
                continue
            for callee in g.edges(node):
                if callee in reaches:
                    reaches[node] = reaches[callee]
                    changed = True
                    break

    rows = registered(scpi_c)
    findings, qwrites, unresolved = [], [], set()
    for pattern, cb in rows:
        nodes = g.by_name.get(cb, [])
        # ⛔ AN UNRESOLVED CALLBACK IS NOT A SILENT PASS (audit r1). This used
        # to `continue` here, so a registered callback whose definition is
        # outside the scan set was dropped and reported clean. Eight live
        # non-query registrations resolve into excluded libscpi code, and the
        # docstring's old justification -- "a vendor function cannot write,
        # it has no access to the context" -- is FALSE for exactly these: a
        # table callback is HANDED the context by libscpi. They are declared,
        # each against a checked reason, and a new one fails the gate.
        if not nodes and "?" not in pattern:
            unresolved.add((pattern, cb))
            continue
        hit = next((n for n in nodes if n in reaches), None)
        if hit is None:
            continue
        route = reaches[hit]
        where = "directly" if route == hit else "via %s" % route[1]
        if "?" in pattern:
            qwrites.append((pattern, cb, where))
        else:
            findings.append((pattern, cb, where))

    # Refusals matter only where they could HIDE a write: inside the closure
    # a non-query callback can reach. Elsewhere an unresolved indirection
    # cannot change this gate's verdict, and refusing on it would be noise.
    examined = set()
    frontier = [n for p, cb in rows if "?" not in p
                for n in g.by_name.get(cb, [])]
    while frontier:
        node = frontier.pop()
        if node in examined:
            continue
        examined.add(node)
        frontier.extend(g.edges(node))
    indirect = set()
    for node in sorted(examined):
        for member in g.unresolved_members(node):
            indirect.add((node[0], node[1], member))

    stats = {"files": len(files), "functions": len(g.bodies),
             "registrations": len(rows),
             "non_query": sum(1 for p, _ in rows if "?" not in p),
             "writers": len(g.writes), "examined": len(examined),
             "indirect": sorted(indirect), "unresolved": sorted(unresolved)}
    return findings, qwrites, sorted(set(g.refusals)), stats


# ---------------------------------------------------------------------------
# Allowlist: shrink-only, and every entry carries the issue.
# ---------------------------------------------------------------------------
def read_allow(path):
    """{(pattern, callback)} -> note, plus parse problems."""
    allow, problems = {}, []
    if not os.path.exists(path):
        return allow, ["allowlist %r not found" % path]
    with open(path, encoding="utf-8") as fh:
        for n, line in enumerate(fh, 1):
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            parts = [p.strip() for p in s.split("\t") if p.strip()]
            if len(parts) != 3:
                problems.append("%s:%d: expected "
                                "'<pattern>\\t<callback>\\t#<issue>', got %r"
                                % (path, n, s))
                continue
            pattern, cb, note = parts
            if not re.match(r"^#\d+$", note):
                problems.append("%s:%d: third field must be the issue that "
                                "tracks the exemption, e.g. '#1274', got %r"
                                % (path, n, note))
                continue
            allow[(pattern, cb)] = note
    return allow, problems


def read_indirect(path):
    """{(file, function, member)} -> note, plus parse problems.

    An indirect call this checker cannot resolve is the only place a write
    can hide from it, so each one is declared here with the issue that
    tracks it. Three exist today, none of them a response path: the
    transport's own `flush`, a CircularBuffer `process_callback`, and the
    WiFi state machine's `active`. The list is shrink-only like the other,
    so a NEW indirection inside the non-query closure fails the gate and has
    to be looked at by a person.
    """
    allow, problems = {}, []
    if not os.path.exists(path):
        return allow, ["indirection list %r not found" % path]
    with open(path, encoding="utf-8") as fh:
        for n, line in enumerate(fh, 1):
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            parts = [p.strip() for p in s.split("\t") if p.strip()]
            if len(parts) != 4:
                problems.append("%s:%d: expected '<file>\\t<function>\\t"
                                "<member>\\t#<issue>', got %r" % (path, n, s))
                continue
            f, fn, member, note = parts
            if not re.match(r"^#\d+$", note):
                problems.append("%s:%d: fourth field must be the issue that "
                                "tracks it, got %r" % (path, n, note))
                continue
            allow[(f, fn, member)] = note
    return allow, problems


def read_unresolved(path):
    """{(pattern, callback)} -> note, plus parse problems.

    A registered non-query callback whose definition is outside the scan set
    cannot be analysed, so it is DECLARED here against a reason that was
    checked, never assumed. The eight entries today are libscpi's own
    minimal-interface callbacks; each was verified by building the same
    reachability over `libscpi/src` and confirming it reaches no sink, with a
    control proving that analysis does detect libscpi's real writers (every
    `*Q` query reaches one).
    """
    allow, problems = {}, []
    if not os.path.exists(path):
        return allow, ["unresolved-callback list %r not found" % path]
    with open(path, encoding="utf-8") as fh:
        for n, line in enumerate(fh, 1):
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            parts = [p.strip() for p in s.split("\t") if p.strip()]
            if len(parts) != 3:
                problems.append("%s:%d: expected '<pattern>\\t<callback>\\t"
                                "#<issue>', got %r" % (path, n, s))
                continue
            pattern, cb, note = parts
            if not re.match(r"^#\d+$", note):
                problems.append("%s:%d: third field must be the issue that "
                                "tracks it, got %r" % (path, n, note))
                continue
            allow[(pattern, cb)] = note
    return allow, problems


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default="firmware/src",
                    help="source root to scan (default: firmware/src)")
    ap.add_argument("--scpi",
                    default="firmware/src/services/SCPI/SCPIInterface.c",
                    help="the file holding the SCPI command table")
    ap.add_argument("--allow",
                    default="tools/lint/scpi_nonquery_write-allow.txt")
    ap.add_argument("--indirect",
                    default="tools/lint/scpi_nonquery_write-indirect.txt",
                    help="declared unresolvable indirections in the closure")
    ap.add_argument("--unresolved",
                    default="tools/lint/scpi_nonquery_write-unresolved.txt",
                    help="declared callbacks defined outside the scan set")
    ap.add_argument("--list", action="store_true",
                    help="print every finding and every query that writes")
    ap.add_argument("--self-test", action="store_true",
                    help="prove the checker on synthetic sources, then exit")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    findings, qwrites, refusals, stats = analyse(args.root, args.scpi)
    allow, allow_problems = read_allow(args.allow)
    indirect_allow, indirect_problems = read_indirect(args.indirect)
    unresolved_allow, unresolved_problems = read_unresolved(args.unresolved)

    if stats:
        print("scanned %d file(s), %d function(s); %d registration(s), %d "
              "without '?'; %d function(s) write; examined %d in the "
              "non-query closure"
              % (stats["files"], stats["functions"], stats["registrations"],
                 stats["non_query"], stats["writers"], stats["examined"]))

    if args.list:
        for pattern, cb, where in findings:
            mark = "allowed" if (pattern, cb) in allow else "NEW"
            print("  [%-7s] %-42s %-28s %s" % (mark, pattern, cb, where))
        if qwrites:
            print("  -- queries that write (expected, context only) --")
            for pattern, cb, where in qwrites[:5]:
                print("  [query  ] %-42s %-28s %s" % (pattern, cb, where))
            print("  ... %d total" % len(qwrites))

    bad = []
    bad.extend(allow_problems)
    bad.extend(indirect_problems)
    bad.extend(unresolved_problems)
    bad.extend(refusals)

    # A non-query callback the gate could not analyse at all. Undeclared, this
    # used to be a silent pass -- the gate reporting clean on a command it
    # never looked at.
    seen_unresolved = set(stats.get("unresolved", []))
    for key in sorted(seen_unresolved - set(unresolved_allow)):
        bad.append("%s (%s) is registered without '?' but its callback is "
                   "defined outside the scanned sources, so this gate did NOT "
                   "analyse it. Declare it in %s against a CHECKED reason, or "
                   "bring its definition into the scan set."
                   % (key[0], key[1], args.unresolved))
    for key in sorted(set(unresolved_allow) - seen_unresolved):
        bad.append("%s (%s) now resolves inside the scanned sources. Remove "
                   "the line from %s so it is analysed rather than exempted."
                   % (key[0], key[1], args.unresolved))

    # An indirection inside the non-query closure is the one way a write can
    # hide from this gate, so an undeclared one FAILS rather than passing
    # quietly. This is the "follow it or refuse it" half of the design.
    seen_indirect = set(stats.get("indirect", []))
    for key in sorted(seen_indirect - set(indirect_allow)):
        bad.append("%s: %s() calls through member %r, which this checker "
                   "cannot resolve, so a response write behind it would be "
                   "MISSED. Declare it in %s with the issue that tracks it, "
                   "or make the call direct."
                   % (key[0], key[1], key[2], args.indirect))
    for key in sorted(set(indirect_allow) - seen_indirect):
        bad.append("%s: %s() no longer calls through member %r. Remove the "
                   "line from %s -- a stale declaration hides the next real "
                   "one." % (key[0], key[1], key[2], args.indirect))

    new = [(p, c, w) for p, c, w in findings if (p, c) not in allow]
    for pattern, cb, where in new:
        bad.append("%s (%s) writes to the response stream %s, and has no "
                   "allowlist entry. A command without '?' produces no "
                   "response, so this text lands in the NEXT query's reply."
                   % (pattern, cb, where))

    # Shrink-only. An entry that no longer writes must be REMOVED, or the
    # file becomes a list of stale exemptions indistinguishable from live
    # ones -- and the next real instance could be added to it unnoticed.
    live = {(p, c) for p, c, _ in findings}
    for key in sorted(set(allow) - live):
        bad.append("%s (%s) is on the allowlist but no longer writes. Remove "
                   "the line: a stale exemption reads exactly like a live one."
                   % key)

    if bad:
        print("")
        for msg in bad:
            print("  - %s" % msg)
        print("")
        print("FAIL: %d problem(s)" % len(bad))
        return 1
    print("OK: %d non-query command(s) write, all %d allowlisted under #1274"
          % (len(findings), len(allow)))
    return 0


# ---------------------------------------------------------------------------
# Self-test. Each synthetic case covers ONE syntactic route to a write, plus
# the vacuity cases that must FAIL -- a checker that examines nothing reports
# a pass, so "it passed" is only meaningful if these refuse.
# ---------------------------------------------------------------------------
_TABLE = '''
const scpi_command_t scpi_commands[] = {
%s
    {.pattern = NULL, .callback = NULL,},
};
'''


def _case(entries, code):
    return (_TABLE % entries) + code


_SELFTEST = [
    ("a direct write in a non-query callback is REFUSED",
     _case('    {.pattern = "SYST:THING", .callback = CbDirect,},',
           '''
static scpi_result_t CbDirect(scpi_t * context) {
    context->interface->write(context, "done\\n", 5);
    return SCPI_RES_OK;
}
'''), ["SYST:THING"]),

    ("a write through a HELPER is REFUSED",
     _case('    {.pattern = "SYST:VIA", .callback = CbVia,},',
           '''
static void HelperWrite(scpi_t * context) {
    scpi_printf("hello\\n");
}
static scpi_result_t CbVia(scpi_t * context) {
    HelperWrite(context);
    return SCPI_RES_OK;
}
'''), ["SYST:VIA"]),

    # The hole that makes a miss a FALSE NEGATIVE. The writer is never
    # called by name at the pass site.
    ("a write through a FUNCTION-POINTER ARGUMENT is REFUSED",
     _case('    {.pattern = "SYST:PTR", .callback = CbPtr,},',
           '''
static void Writer(scpi_t * context) {
    SCPI_ResultInt(context, 1);
}
static void Runner(scpi_t * context, void (*fn)(scpi_t *)) {
    fn(context);
}
static scpi_result_t CbPtr(scpi_t * context) {
    Runner(context, Writer);
    return SCPI_RES_OK;
}
'''), ["SYST:PTR"]),

    ("a write on an ERROR branch only is REFUSED",
     _case('    {.pattern = "SYST:ELSE", .callback = CbElse,},',
           '''
static scpi_result_t CbElse(scpi_t * context) {
    if (Ready()) {
        return SCPI_RES_OK;
    } else {
        context->interface->write(context, "nope\\n", 5);
        return SCPI_RES_ERR;
    }
}
'''), ["SYST:ELSE"]),

    ("a '?' command that writes PASSES",
     _case('    {.pattern = "SYST:THING?", .callback = CbQuery,},',
           '''
static scpi_result_t CbQuery(scpi_t * context) {
    SCPI_ResultInt(context, 7);
    return SCPI_RES_OK;
}
'''), []),

    ("a non-query callback that writes NOTHING passes",
     _case('    {.pattern = "SYST:QUIET", .callback = CbQuiet,},',
           '''
static scpi_result_t CbQuiet(scpi_t * context) {
    DoWork();
    return SCPI_RES_OK;
}
'''), []),

    # A write in a char literal or a comment must not count: cdef's mask
    # blanks both, and the '"' char literal is the case that swallowed code
    # in an earlier hand-rolled scanner.
    ("a sink named only in a comment or a char literal passes",
     _case('    {.pattern = "SYST:TEXT", .callback = CbText,},',
           '''
static scpi_result_t CbText(scpi_t * context) {
    char q = '"';
    /* does not scpi_printf( anything */
    return SCPI_RES_OK;
}
'''), []),

    # Audit r1, finding 1/5: a write reached through a HEADER-defined helper.
    # With a `.c`-only scan set this was a silent leaf, and live non-query
    # callbacks already reach `SCPIInterface.h` helpers today. The helper is
    # written into the fixture's header by `_case_header` below.
    ("a write through a HEADER-defined helper is REFUSED",
     _case('    {.pattern = "SYST:HDR", .callback = CbHdr,},',
           '''
static scpi_result_t CbHdr(scpi_t * context) {
    HeaderWrite(context);
    return SCPI_RES_OK;
}
'''), ["SYST:HDR"]),
]

# Fixture header for the case above: the writer lives in a `.h`, which is the
# whole point of the case.
_CASE_HEADER = '''
static inline void HeaderWrite(scpi_t * context) {
    scpi_printf(context, "from a header\\n");
}
'''


def _ck(why, got, want):
    if got == want:
        return []
    return ["%s: got %r, want %r" % (why, got, want)]


def self_test():
    import tempfile
    failures, checks = [], 0

    for why, source, expect in _SELFTEST:
        with tempfile.TemporaryDirectory() as td:
            d = os.path.join(td, "services", "SCPI")
            os.makedirs(d)
            scpi_c = os.path.join(d, "SCPIInterface.c")
            with open(scpi_c, "w") as fh:
                fh.write(source)
            # _require_scan_set insists on both real SCPI files; the
            # synthetic fixture supplies the second as an empty stub.
            with open(os.path.join(d, "SCPIStorageSD.c"), "w") as fh:
                fh.write("/* stub */\n")
            with open(os.path.join(d, "SCPIInterface.h"), "w") as fh:
                fh.write(_CASE_HEADER)
            findings, _, refusals, stats = analyse(td, scpi_c)
            checks += 1
            failures.extend(_ck(why, sorted(p for p, _, _ in findings),
                                sorted(expect)))
            if refusals:
                failures.append("%s: unexpected refusals %r" % (why, refusals))
            # No synthetic case registers a callback it does not define, so
            # any unresolved entry here is a fixture bug, not a finding.
            if stats.get("unresolved"):
                failures.append("%s: unexpected unresolved callbacks %r"
                                % (why, stats["unresolved"]))

    # Audit r1, finding 0/4: a registered non-query callback DEFINED NOWHERE
    # in the scan set must be reported as unanalysed, never skipped. This is
    # the shape of libscpi's eight table callbacks.
    with tempfile.TemporaryDirectory() as td:
        d = os.path.join(td, "services", "SCPI")
        os.makedirs(d)
        scpi_c = os.path.join(d, "SCPIInterface.c")
        with open(scpi_c, "w") as fh:
            fh.write(_TABLE % '    {.pattern = "SYST:GONE", .callback = CbElsewhere,},')
        with open(os.path.join(d, "SCPIStorageSD.c"), "w") as fh:
            fh.write("/* stub */\n")
        findings, _, _, stats = analyse(td, scpi_c)
        checks += 2
        failures.extend(_ck("a callback defined nowhere is reported unresolved",
                            stats.get("unresolved"), [("SYST:GONE", "CbElsewhere")]))
        failures.extend(_ck("...and is NOT reported as a silent pass",
                            sorted(p for p, _, _ in findings), []))

    # The same, but as a QUERY: a '?' callback we cannot analyse is not this
    # gate's business, so it must NOT be reported.
    with tempfile.TemporaryDirectory() as td:
        d = os.path.join(td, "services", "SCPI")
        os.makedirs(d)
        scpi_c = os.path.join(d, "SCPIInterface.c")
        with open(scpi_c, "w") as fh:
            fh.write(_TABLE % '    {.pattern = "SYST:GONE?", .callback = CbElsewhere,},')
        with open(os.path.join(d, "SCPIStorageSD.c"), "w") as fh:
            fh.write("/* stub */\n")
        _, _, _, stats = analyse(td, scpi_c)
        checks += 1
        failures.extend(_ck("an unresolved QUERY callback is not reported",
                            stats.get("unresolved"), []))

    # ---- vacuity: each of these MUST be refused ----
    with tempfile.TemporaryDirectory() as td:
        checks += 1
        problems = _require_scan_set([], td)
        failures.extend(_ck("an EMPTY scan set must be refused",
                            bool(problems), True))

    with tempfile.TemporaryDirectory() as td:
        d = os.path.join(td, "services", "SCPI")
        os.makedirs(d)
        only = os.path.join(d, "SCPIInterface.c")
        with open(only, "w") as fh:
            fh.write("/* no table */\n")
        checks += 1
        problems = _require_scan_set([only], td)
        failures.extend(_ck("a scan set missing SCPIStorageSD.c must be "
                            "refused", bool(problems), True))

    # A table the regex cannot read must exit, not silently drop the entry.
    with tempfile.TemporaryDirectory() as td:
        d = os.path.join(td, "services", "SCPI")
        os.makedirs(d)
        scpi_c = os.path.join(d, "SCPIInterface.c")
        with open(scpi_c, "w") as fh:
            fh.write('const scpi_command_t c[] = {\n'
                     '    {.pattern = SOME_MACRO, .callback = Cb,},\n'
                     '    {.pattern = NULL, .callback = NULL,},\n};\n')
        checks += 1
        try:
            registered(scpi_c)
            failures.append("an UNPARSEABLE table entry must exit, not be "
                            "dropped")
        except SystemExit:
            pass

    # The allowlist's own rules.
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, "allow.txt")
        with open(p, "w") as fh:
            fh.write("# comment\nSYST:A\tCbA\t#1274\nSYST:B\tCbB\tnope\n")
        allow, problems = read_allow(p)
        checks += 2
        failures.extend(_ck("a well-formed allowlist line is read",
                            ("SYST:A", "CbA") in allow, True))
        failures.extend(_ck("an allowlist line without an issue ref is a "
                            "problem", bool(problems), True))

    # The pointer-argument extractor, directly: the one rule whose absence is
    # a false negative rather than a noisy failure.
    checks += 2
    by_name = {"Writer": [("f.c", "Writer")]}
    failures.extend(_ck("a bare function argument is found",
                        _pointer_arguments("Runner(ctx, Writer);", by_name),
                        {"Writer"}))
    failures.extend(_ck("an address-of function argument is found",
                        _pointer_arguments("Runner(ctx, &Writer);", by_name),
                        {"Writer"}))

    for msg in failures:
        print("  - %s" % msg)
    print("scpi_nonquery_write self-test: %s (%d failure(s); %d check(s))"
          % ("all cases pass" if not failures else "FAILED",
             len(failures), checks))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
