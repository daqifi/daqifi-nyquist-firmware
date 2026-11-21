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
`SCPI_Result*`, `SCPI_Write*` (called, OR named without a call, since a sink
whose address is taken is a sink that will be called), or a `->write(...)`
transport call -- directly or through any call chain inside our own sources
AND libscpi's.

The allowlist is **shrink-only**: an entry that no longer writes FAILS the
gate. Otherwise the list rots quietly as stage 3 lands, and a file of stale
exemptions reads exactly like a file of live ones. Same both-directions rule
as `log_budget.py`.

## What it does NOT claim

- **Nothing about reachability of a branch.** A write behind a condition that
  can never be true still counts. This is a textual call graph with no control
  flow, the same stated limit as `scpi_claim_path.py` (#896).
- **Nothing about the preprocessor.** `cdef`'s view blanks directives but does
  not evaluate them, so a call inside `#if 0` still counts, and a macro
  defined twice under `#if`/`#else` contributes BOTH bodies. Over-approximating
  here produces a FALSE POSITIVE, which costs an argument; the opposite costs
  a missed defect.
- **Nothing about `**ERROR` lines.** Every queued error also prints one, from
  the transport error callbacks (`SCPI_USB_Error`, `SCPI_TCP_Error`). That is
  broader than these commands: it fires for every queued error, on any
  command, query or not. It IS part of #1274 (the operator's scope call:
  "stay true and consistent to scpi"), but it is stage 3's removal, not
  stage 1's gate. libscpi reaches those callbacks through
  `context->interface->error(...)`, and those two call sites are DECLARED in
  `scpi_nonquery_write-indirect.txt` rather than followed -- see below.
- **Nothing about whether a `?` command writes.** A query SHOULD write. Those
  are reported only under `--list` as context.

## How libscpi is read (round 3)

`scpi_printf` and the `SCPI_Result*` family are DEFINED in
`firmware/src/libraries/scpi/libscpi/`. Until round 3 that directory was left
out of the graph, on the strength of a note from an earlier throwaway scan
that "`SCPI_ParamCopyText` reached `SCPI_Result*` and dragged innocent
callbacks in with it". So every libscpi callee was a silent LEAF, and the
audit reproduced the consequence: a non-query callback calling
`SCPI_CoreIdnQ(context)` -- which writes the identity -- passed the gate.

That note was MEASURED in round 3, not trusted, and it is wrong as stated.
Indexing libscpi into this graph (228 functions in `src`, 31 of which name
a sink directly, plus 141 macros, 11 of them writing), the ONLY route from the parameter/error helpers to a sink is
`SCPI_ErrorPush -> SCPI_ErrorPushEx -> SCPI_ErrorEmit ->
context->interface->error(...) -> SCPI_USB_Error`: the `**ERROR` line above,
which this gate explicitly does not claim. With those two `->error` call
sites declared, `SCPI_ParamCopyText` and every other `SCPI_Param*` reach no
sink, and the eight libscpi table callbacks (`*CLS`, `*ESE`, `*OPC`, `*SRE`,
`*WAI`, `STATus:PRESet`, `STATus:OPERation:ENABle`,
`STATus:QUEStionable:ENABle`) are ANALYSED instead of declared unanalysable
-- the hand-checked `scpi_nonquery_write-unresolved.txt` entries for them
are gone, because the gate now does what that file's comment said had been
done by hand.

So libscpi is a SECOND LAYER of the same graph (`libraries/scpi/libscpi/src`
and `.../inc/scpi`, macros included), resolved with one rule: a call from our
code resolves to our definition when there is one and to libscpi's
otherwise; a call from libscpi resolves inside libscpi only. libscpi reaches
our code back only through the transport interface, which is followed like
any other member call.

There is one more declared boundary: libscpi's PARSER ENTRY POINTS
(`SCPI_Input`, `SCPI_Parse`). They really do write -- they execute whatever
command arrives, and add the `;` and newline framing of a query's reply --
and our transports call them from the console callback
`microrl_commandComplete`. That callback is registered (passed as a pointer
argument, which this graph counts as a call) by `wifi_tcp_server_Initialize`,
which the WiFi pump reaches from `SYSTem:POWer:STATe`, so the parser's
writes would land on a command that only powers the board. The parse those
writes belong to is a LATER command on the transport, analysed under its own
registration. A call to a parser entry point may therefore be declared in
`scpi_nonquery_write-indirect.txt`; a declaration naming any other callee is
refused, so the list cannot be used to cut an ordinary writer.

## Why an unnamed callee that receives the context is REFUSED (round 3)

A call that resolves to nothing used to be a silent leaf. That is sound only
for a callee that cannot reach the stream, and the one thing that lets a
callee reach the stream is the context. So a call whose arguments mention a
context -- an identifier declared `scpi_t`, or a parameter some caller binds
to one, followed through macros and helpers -- and whose target this graph
cannot name is REFUSED. Before refusing, three things are tried, in order:

* `(f)(x)`, `(*f)(x)`, `((f))(x)` are normalised to `f(x)` (a cast such as
  `(uint32_t)(x)` is left alone), so a parenthesised callee is followed --
  `(SCPI_ResultInt32)(context, 7)` was a miss;
* a LOCAL function pointer is resolved through the assignments to it in the
  same body (`fp = Writer;`, `void (*fp)(...) = Writer;`, either arm of a
  `?:`), and an edge added to each target; any assignment it cannot name,
  or its address escaping, leaves it unresolved;
* a call through a function-pointer PARAMETER is resolved at its call sites:
  every caller must pass a function this graph knows (each of which the
  pointer-argument rule already edges from the caller), and the function's
  own address must never be taken, or its call sites are not all visible.

What survives is either a defect to fix or a callee that truly has no
source here -- the toolchain's `memset`, say. Those, and only those, are
declared one call site per line in `scpi_nonquery_write-external.txt`,
`<file>:<line>`, and a declaration cannot mask a write: it applies only to a
callee that resolves to NOTHING, so the day that name is defined in the scan
set the call is followed and the line is reported stale.

## Why a function-pointer argument must be followed or refused

`SCPI_RunSessionStartClaimed(context, SCPI_WifiFindRateClaimed, ...)` passes a
callback that writes. An edge rule keyed on "identifier followed by `(`" never
sees it, and the resulting miss is a FALSE NEGATIVE -- the gate reports a pass
on a command that writes. So an argument that resolves to a known function
becomes an edge, and an indirection this file cannot resolve is REFUSED
(reported, gate fails) rather than assumed harmless. A refusal costs a human
five minutes; a silent miss is the defect shipping.

## Round 4: accept what is understood, refuse the rest

Rounds 2 and 3 each added matchers for the shapes an audit had reproduced,
and each next audit found more (r2: 4, r3: 9 in 5 classes). So the rule is
inverted. Inside the examined closure a call is accepted only as one of the
forms this file positively understands -- a NAMED callee that resolves (into
our sources or libscpi's), a sink, a word on `_NOT_CALLEES`, a cast, a local
or parameter pointer whose every value is named, a declared sourceless
callee -- and the remaining forms are refused by CLASS, not by shape:

* **Macro uses.** Every macro a function body NAMES is followed, called or
  not: `AUDIT_ACK;` and `REPLY;` (a `do {} while (0)` body) expand in place
  exactly like `REPLY(...)`. A macro naming a macro is followed again, so
  the chain is transitive. A macro inherits its user's context names, since
  it expands in the user's scope.
* **Computed callees.** A callee that is not a plain name -- `hooks[0](...)`,
  `hooks[i](...)`, `get()(...)`, `(a ? f : g)(...)` -- is REFUSED, with or
  without a context argument (a global context needs none). The real tree
  has none.
* **Members.** A member call is followed through every function assigned to
  that member, and is resolved only when EVERY value it can hold is named:
  each store and designated initialiser names a function, NULL, a constant
  or another complete member; its address is never taken; a struct or union
  declaring it is in the scan set; and no POSITIONAL initialiser (or compound
  literal) of such a type, or of one embedding it, names anything. Otherwise
  it is an indirection, declared or refused as before.
* **Aliases.** An lvalue assigned or initialised from the context itself (a
  name, or a member/element chain ending in one, under casts, `&`, `*` and
  `?:`) carries it, to a flow-insensitive fixed point, so `c2 = c1; c1 =
  context; fp(c2);` is refused like `fp(context)`.

Measured on the real tree at 74131e64 + E, the inversion adds ZERO
refusals and the same 15 allowlisted writers remain. Its first draft added
23 (20 from treating DATA read through a context, `n = context->idx`, as an
alias; 2 from reading `&&` as address-of; 1 from a same-named `bool active`
being assigned `false`); each was a defect of the draft, not of the tree,
and was narrowed in a way that cannot hide a function value.

What it still does NOT claim (each is a stated residual, not a silent one):
a function that RETURNS its context argument is not an alias; a struct
reached through its own address (`memcpy(&s, buf, n)`, a union) is not
treated as a store to its members; and a call through a named but
unresolved pointer that receives NO context is not refused (round 3's scope,
unchanged -- computed callees are the exception above).

## Why a definition the grammar misreads is refused

`cdef.compiler_view` raises `UnreadDefinition` when a body at file scope is
not read as its own definition -- a LEADING `__attribute__((...))` lost the
function entirely, TWO stacked prefixes made it a function NAMED
`__attribute__`, and a writer defined either way was invisible to every
caller (round 3). The refusal surfaces here as a gate failure naming the
line.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from cdef import (AmbiguousDefinition, compiler_view,  # noqa: E402
                  line_comment_end, literal_end, parameters_before, splice)

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
    r'\.pattern\s*=\s*((?:"[^"]*"\s*)+),\s*\.callback\s*=\s*([A-Za-z_]\w*)'
    r'(?=\s*[,}])')   # r4: ANCHORED -- `cond ? A : B` must not parse as `cond`
_PATTERN_FIELD = re.compile(r"\.pattern\s*=")
_NULL_SENTINEL = re.compile(r"\.pattern\s*=\s*NULL\b")
_ONE_LITERAL = re.compile(r'"([^"]*)"')

# The response-stream sinks. `SCPI_Result` and `SCPI_Write` are matched as
# PREFIXES so a newly added result function is covered the day it lands,
# rather than the day someone remembers to extend a list -- the gate's failure
# co-occurs with the thing it guards. Matched as a NAME, not only as a call:
# `fp = SCPI_ResultInt32; fp(context, 7);` and `Runner(context,
# SCPI_ResultText)` name the sink without a `(` after it, and a sink whose
# address is taken is a sink that will be called (round 3).
_SINK_NAMES = r"(?:SCPI_Result\w*|SCPI_Write\w*|scpi_printf)"
_SINK = re.compile(r"\b" + _SINK_NAMES + r"\b|(?:->|\.)\s*write\s*\(")
_SINK_NAME = re.compile(_SINK_NAMES + r"\Z")

# An indirect call through a member. `->write(`/`.write(` is the transport
# sink above; any OTHER member call inside the examined closure is an
# indirection this file cannot resolve, and is refused.
_MEMBER_CALL = re.compile(r"(?:->|\.)\s*([A-Za-z_]\w*)\s*\(")
_DIRECT_CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
# `.flush = SCPI_USB_Flush` / `ctx.active = MainState` -- a function assigned
# to a struct field, which is how a vtable call becomes resolvable.
_MEMBER_ASSIGN = re.compile(
    r"(?:\.|->)\s*([A-Za-z_]\w*)\s*=\s*&?\s*([A-Za-z_]\w*)\s*[,;}]")
_IDENT = re.compile(r"[A-Za-z_]\w*")

_EXCLUDED = ("third_party", "libraries", "config")

# The second layer (see "How libscpi is read").
_LIBSCPI = "libraries/scpi/libscpi/"
_LIBSCPI_DIRS = ("libraries/scpi/libscpi/src", "libraries/scpi/libscpi/inc/scpi")
# libscpi's parser entry points: the only DIRECT callees a declaration in the
# indirect list may cut, and only where they resolve into libscpi.
_DISPATCHERS = ("SCPI_Input", "SCPI_Parse")

# Words followed by `(` that are not calls of anything this graph could name.
_NOT_CALLEES = frozenset(
    "if while for switch return sizeof defined do case else goto _Alignof "
    "alignof __alignof__ _Static_assert static_assert __attribute__ "
    "__attribute typeof __typeof__ __typeof __asm__ __asm asm _Generic "
    "__extension__".split())
_C_TYPE_WORDS = frozenset(
    "void char short int long float double signed unsigned _Bool bool const "
    "volatile struct union enum".split())
# A word that declares a context: `scpi_t * context`, `scpi_t ctx`.
_CTX_DECL = re.compile(
    r"\bscpi_t\b(?:\s|\*|\bconst\b|\bvolatile\b)*([A-Za-z_]\w*)")


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
# Small text helpers over cdef's compiler view (comments and literals blanked).
# ---------------------------------------------------------------------------
_WS = " \t\r\n\f\v"


def _match_paren(text, open_paren):
    """Index of the bracket closing the one at `open_paren` (or len)."""
    depth, i = 0, open_paren
    while i < len(text):
        if text[i] in "([{":
            depth += 1
        elif text[i] in ")]}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return i


def _split_top_level(text, seps=","):
    """`text` split on separators that are not inside brackets."""
    parts, depth, cur = [], 0, []
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch in seps and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    parts.append("".join(cur))
    return parts


_ARG_STRIP = re.compile(r"^[\s&*()]*|[\s()]*$")
_CAST = re.compile(r"^\s*\([^()]*\)\s*")


def _clean_arg(arg):
    """An argument or right-hand side as the bare name it may be."""
    return _ARG_STRIP.sub("", _CAST.sub("", arg)).strip()


def _prev_index(text, i):
    i -= 1
    while i >= 0 and text[i] in _WS:
        i -= 1
    return i


def _word_ending(text, end):
    s = end
    while s >= 0 and (text[s].isalnum() or text[s] in "_$"):
        s -= 1
    return text[s + 1:end + 1]


def _is_member(text, name_start):
    """True when the identifier at `name_start` follows `->` or `.`."""
    p = _prev_index(text, name_start)
    if p >= 1 and text[p] == ">" and text[p - 1] == "-":
        return True
    return p >= 0 and text[p] == "." and not text[max(0, p - 2):p + 1] == "..."


def _calls(body):
    """[(name, name_start, [arg text], is_member)] for every `name(`."""
    out = []
    for m in _DIRECT_CALL.finditer(body):
        open_paren = m.end() - 1
        close = _match_paren(body, open_paren)
        inner = body[open_paren + 1:close]
        args = _split_top_level(inner) if inner.strip() else []
        out.append((m.group(1), m.start(1), args, _is_member(body, m.start(1))))
    return out


def _mentions(text, names):
    return any(w in names for w in _IDENT.findall(text))


# ---------------------------------------------------------------------------
# Round 4: the INVERSION's text primitives.
#
# Rounds 2 and 3 each refused the call SHAPES an audit had reproduced, and
# each following audit found more (r2: 4, r3: 9 in 5 classes). So round 4
# stops enumerating shapes that are bad and enumerates the forms that are
# understood; every other form that can carry a context is refused. These
# helpers are what lets a body be read as a list of forms at all.
# ---------------------------------------------------------------------------
def _match_open(text, close):
    """Index of the bracket opening the one at `close` (or -1)."""
    depth, i = 0, close
    while i >= 0:
        if text[i] in ")]}":
            depth += 1
        elif text[i] in "([{":
            depth -= 1
            if depth == 0:
                return i
        i -= 1
    return -1


def _expr_end(text, i):
    """End of the expression starting at `i`: the first top-level `,` or
    `;`, or an unbalanced closing bracket."""
    depth = 0
    while i < len(text):
        c = text[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                return i
            depth -= 1
        elif c in ",;" and depth == 0:
            return i
        i += 1
    return i


def _postfix_start(text, p):
    """Start of the postfix expression that ENDS at `p` -- `tbl[i].fn`,
    `get()`, `a->b[2]` -- walked back over brackets, names, `.` and `->`."""
    start = p + 1
    while p >= 0:
        c = text[p]
        if c in ")]":
            o = _match_open(text, p)
            if o < 0:
                break
            start, p = o, _prev_index(text, o)
            continue
        if c.isalnum() or c in "_$":
            s = p
            while s >= 0 and (text[s].isalnum() or text[s] in "_$"):
                s -= 1
            start, p = s + 1, _prev_index(text, s + 1)
            if p >= 0 and text[p] == ".":
                p = _prev_index(text, p)
                continue
            if p >= 1 and text[p] == ">" and text[p - 1] == "-":
                p = _prev_index(text, p - 1)
                continue
            break
        break
    return start


def _lvalue_base(text, eq):
    """The base name of the lvalue left of the `=` at `eq`: `alias` in
    `void *alias =`, `s` in `s.ctx[2] =`, `p` in `*p =`. None for a
    parenthesised declarator (`void (*fp)(...) =`), which declares a
    function pointer, not a context."""
    p = _prev_index(text, eq)
    base = None
    while p >= 0:
        c = text[p]
        if c == "]":
            o = _match_open(text, p)
            if o < 0:
                return base
            p = _prev_index(text, o)
            continue
        if c.isalnum() or c in "_$":
            s = p
            while s >= 0 and (text[s].isalnum() or text[s] in "_$"):
                s -= 1
            base = text[s + 1:p + 1]
            p = _prev_index(text, s + 1)
            if p >= 0 and text[p] == ".":
                p = _prev_index(text, p)
                continue
            if p >= 1 and text[p] == ">" and text[p - 1] == "-":
                p = _prev_index(text, p - 1)
                continue
            return base
        return base
    return base


_ASSIGN_OP = re.compile(r"(?<![=!<>+\-*/%&|^])=(?!=)")


def _assignments(body):
    """[(lvalue base, right-hand side)] for every plain `=` in `body` --
    assignments, declarations' initialisers, designated initialisers."""
    out = []
    for m in _ASSIGN_OP.finditer(body):
        base = _lvalue_base(body, m.start())
        if base:
            out.append((base, body[m.end():_expr_end(body, m.end())]))
    return out


_DECL_INTRO = frozenset(("struct", "union", "enum"))


def _is_type_expr(inner, is_type):
    """True when a parenthesised group is a TYPE -- `(uint8_t)`, `(const
    char *)`, `(void (*)(scpi_t *))`, `(struct foo *)` -- i.e. a cast."""
    if re.search(r"[^\w\s*()\[\],]", inner):
        return False
    # `(NAME *)` cannot be an expression (`a *` has no right operand), so it
    # is a cast even when NAME is a type from a directory this graph does
    # not index (`config/`'s Harmony typedefs).
    if re.match(r"\s*(?:(?:const|volatile|struct|union|enum)\s+)*"
                r"[A-Za-z_]\w*(?:\s*\*|\s+const\b|\s+volatile\b)*\s*\*\s*\Z",
                inner):
        return True
    words = _IDENT.findall(inner)
    if not words:
        return False
    for k, w in enumerate(words):
        if not (w in _C_TYPE_WORDS or is_type(w)
                or (k and words[k - 1] in _DECL_INTRO)):
            return False
    return True


def _computed_calls(body, is_type):
    """[(callee start, callee text, [arg text])] for every call whose
    callee is NOT a plain name (round 4): `hooks[0](ctx)`, `hooks[i](ctx)`,
    `get()(ctx)`, `(a ? f : g)(ctx)`, `((cb_t)fp)(ctx)`.

    A `(` after `]` is always a call. A `(` after `)` is a call unless the
    group before it is a cast (`(uint8_t)(x)`), a statement's condition
    (`if (a) (void)x`), an operator's operand (`sizeof (x)`), or a
    declarator (`void (*fp)(scpi_t *)`). `(f)(x)` and `(*f)(x)` never get
    here: `normalise_callees` has already made them `f(x)`.
    """
    out = []
    i = body.find("(")
    while i >= 0:
        p = _prev_index(body, i)
        computed = False
        if p >= 0 and body[p] == "]":
            computed = True
        elif p >= 0 and body[p] == ")":
            o = _match_open(body, p)
            if o >= 0:
                inner = body[o + 1:p]
                if _starts_expression(body, o):
                    computed = not _is_type_expr(inner, is_type)
                else:
                    q = _prev_index(body, o)
                    if q >= 0 and body[q] == ")" and _is_type_expr(
                            body[_match_open(body, q) + 1:q], is_type):
                        # `(void)(T *)(x)`: after a cast, a group is an
                        # operand -- a cast again, or a computed callee.
                        computed = not _is_type_expr(inner, is_type)
                    elif q >= 0 and body[q] in ")]":
                        computed = True          # `f()()`, `a[0]()()`
                    elif q >= 0 and (body[q].isalnum() or body[q] in "_$"):
                        w = _word_ending(body, q)
                        k = _prev_index(body, q - len(w) + 1)
                        before = _word_ending(body, k) if k >= 0 else ""
                        computed = not (
                            w in _COND_KEYWORDS or w in _NOT_CALLEES
                            or w in _C_TYPE_WORDS or is_type(w)
                            or before in _DECL_INTRO)
        if computed:
            close = _match_paren(body, i)
            inner = body[i + 1:close]
            start = _postfix_start(body, p)
            out.append((start, body[start:p + 1],
                        _split_top_level(inner) if inner.strip() else []))
        i = body.find("(", i + 1)
    return out


# A parenthesised callee: `(f)`, `(*f)`, `(s->cb)`, the inner layer of `((f))`.
_PAREN_NAME = re.compile(
    r"\(\s*\**\s*([A-Za-z_]\w*(?:\s*(?:->|\.)\s*[A-Za-z_]\w*)*)\s*\)")
# A `(` after one of these starts a new expression, so a following `(f)` is
# not an argument list.
_EXPR_KEYWORDS = frozenset(("return", "case", "else", "do"))
_COND_KEYWORDS = frozenset(("if", "while", "for", "switch"))


def _starts_expression(text, open_paren):
    """True when the `(` at `open_paren` cannot be a call's argument list or
    a subscript's continuation -- i.e. `(f)` there is an operand."""
    p = _prev_index(text, open_paren)
    if p < 0:
        return True
    c = text[p]
    if c.isalnum() or c in "_$":
        return _word_ending(text, p) in _EXPR_KEYWORDS
    if c == ")":
        # `if (x) (f)(ctx);` -- the `)` closes a statement's condition.
        depth, i = 0, p
        while i >= 0:
            if text[i] == ")":
                depth += 1
            elif text[i] == "(":
                depth -= 1
                if depth == 0:
                    break
            i -= 1
        q = _prev_index(text, i)
        return q >= 0 and _word_ending(text, q) in _COND_KEYWORDS
    return c != "]"


def normalise_callees(code, is_type):
    """`(f)(x)`, `(*f)(x)`, `((f))(x)` -> `f(x)`, LENGTH kept (round 3).

    Only the wrapping punctuation is blanked, so offsets still map to
    physical lines. A cast -- `(uint32_t)(x)`, any name `is_type` accepts --
    is left alone, and so is `g(x)(y)`'s own argument list. Repeated to a
    fixed point, so `((f))` loses one layer per pass.
    """
    while True:
        changed = [False]

        def repl(m):
            s, inner = m.string, m.group(1)
            if not _starts_expression(s, m.start()):
                return m.group(0)
            q = m.end()
            while q < len(s) and s[q] in _WS:
                q += 1
            if q >= len(s) or s[q] not in "()":
                return m.group(0)
            if s[q] == "(" and "->" not in inner and "." not in inner \
                    and is_type(inner):
                return m.group(0)
            changed[0] = True
            g, a, b = m.group(0), m.start(1) - m.start(), m.end(1) - m.start()
            return " " * a + g[a:b] + " " * (len(g) - b)

        code = _PAREN_NAME.sub(repl, code)
        if not changed[0]:
            return code


_TYPEDEF = re.compile(r"\btypedef\b")
_FN_TYPEDEF = re.compile(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)")


def _typedef_names(code):
    """Names a `typedef` introduces, so a cast to one is not a callee."""
    names = set()
    for m in _TYPEDEF.finditer(code):
        depth, i = 0, m.end()
        while i < len(code):
            c = code[i]
            if c in "([{":
                depth += 1
            elif c in ")]}":
                depth -= 1
            elif c == ";" and depth <= 0:
                break
            i += 1
        decl = code[m.end():i]
        fn = _FN_TYPEDEF.search(decl)
        if fn:
            names.add(fn.group(1))
            continue
        flat, depth = [], 0
        for c in decl:
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
            elif depth == 0:
                flat.append(c)
        for part in _split_top_level("".join(flat)):
            ids = _IDENT.findall(re.sub(r"\[[^\]]*\]", " ", part))
            if ids:
                names.add(ids[-1])
    return names


def _param_names(text):
    """Positional parameter names from a parameter list's text; None where a
    slot has no name. `...` is `__VA_ARGS__`, so a macro's variadic tail and
    a function's line up the same way."""
    if text is None:
        return []
    out = []
    for part in _split_top_level(text):
        s = part.strip()
        if not s or s == "void":
            continue
        if s.endswith("..."):
            out.append(s[:-3].strip() or "__VA_ARGS__")
            continue
        fn = _FN_TYPEDEF.search(s)
        if fn:
            out.append(fn.group(1))
            continue
        ids = _IDENT.findall(re.sub(r"\[[^\]]*\]", " ", s))
        out.append(ids[-1] if ids else None)
    return out


_DEFINE = re.compile(
    r"(?m)^[ \t]*(?:#|%:)[ \t]*define[ \t]+([A-Za-z_]\w*)(\(([^()]*)\))?(.*)$")


# ---------------------------------------------------------------------------
# The call graph over our sources and libscpi's.
# ---------------------------------------------------------------------------
def _walk(base_dir, exclude):
    out = []
    for base, dirs, files in os.walk(base_dir):
        dirs[:] = [d for d in dirs if d not in exclude]
        for f in sorted(files):
            if f.endswith(".c") or f.endswith(".h"):
                out.append(os.path.join(base, f))
    return out


def source_files(root):
    """Every `.c` AND `.h` of ours under `root`, excluding the vendored trees.

    ⛔ HEADERS ARE NOT OPTIONAL (audit r1). A `.c`-only sweep made every
    header-defined helper a silent LEAF, and live non-query callbacks already
    reach into `SCPIInterface.h` -- `SCPI_RejectCfgClaim`,
    `SCPI_OptionalParamInt32`. A write behind one of those would have passed
    the gate, which is the failure direction that matters: silence.
    """
    out = []
    for path in _walk(root, _EXCLUDED):
        rel = os.path.relpath(os.path.dirname(path), root).replace(os.sep, "/")
        if any(part in _EXCLUDED for part in rel.split("/")):
            continue
        out.append(path)
    return sorted(out)


def libscpi_files(root):
    """libscpi's sources and public headers: the graph's second layer."""
    out = []
    for d in _LIBSCPI_DIRS:
        p = os.path.join(root, d)
        if os.path.isdir(p):
            out.extend(os.path.join(p, f) for f in sorted(os.listdir(p))
                       if f.endswith(".c") or f.endswith(".h"))
    return out


def _is_libscpi(relpath):
    return relpath.startswith(_LIBSCPI)


def _pick(cands, from_file):
    """`Graph.resolve_all`'s layer rule over a candidate list."""
    local = [n for n in cands if n[0] == from_file]
    if local:
        return local
    if _is_libscpi(from_file):
        return [n for n in cands if _is_libscpi(n[0])]
    ours = [n for n in cands if not _is_libscpi(n[0])]
    return ours if ours else cands       # [] = no source anywhere


_INIT_NEUTRAL = frozenset(("NULL", "true", "false", "TRUE", "FALSE"))
# A numeric literal, or a string/char literal (blanked to quotes and spaces in
# the compiler view): never a function.
_CONSTANT = re.compile(r"\A(?:[-+~]?\s*(?:0[xX][0-9a-fA-F]+|\d[\w.]*)"
                       r"|[\"'][\s\"']*)\Z")


def _positional(text, open_brace, is_type):
    """True when the brace initialiser at `open_brace` has a POSITIONAL
    element that could be a function -- one naming anything other than
    NULL or a type. `{0}`, `{ NULL }` and `{ .run = X }` are not; `{ Writer
    }`, `{ { Writer } }`, `{ [0] = Writer }` and `{ .h = { Writer } }` are.
    A member-designated value is checked by the member-store rule; nested
    braces are checked here, recursively."""
    close = _match_paren(text, open_brace)
    for el in _split_top_level(text[open_brace + 1:close]):
        s = el.strip()
        if not s:
            continue
        d = re.match(r"(?:\s*(?:\.\s*[A-Za-z_]\w*|\[[^\]]*\]))+\s*=(?!=)", s)
        if d:
            member_designated = "." in re.sub(r"\[[^\]]*\]", "", s[:d.end()])
            s = s[d.end():].strip()
            if member_designated and not s.startswith("{"):
                continue
        if s.startswith("{"):
            if _positional(s, 0, is_type):
                return True
            continue
        words = [w for w in _IDENT.findall(s) if not (
            w in _INIT_NEUTRAL or w in _C_TYPE_WORDS or is_type(w))]
        if words:
            return True
    return False


class Graph(object):
    """Function and macro bodies, call edges and sink ownership."""

    def __init__(self):
        self.bodies = {}        # node -> body text (callees normalised)
        self.segments = {}      # node -> [(relpath, joined offset, length)]
        self.params = {}        # node -> positional parameter names
        self.ctx_decl = {}      # node -> names declared `scpi_t` in it
        self.macros = set()
        self.views = {}         # relpath -> cdef view
        self.by_name = {}       # name -> [ (relpath, name) ]
        self.file_code = {}     # relpath -> whole-file compiler view
        self.by_member = {}     # member name -> {(relpath, name)}
        self.writes = set()     # nodes with a DIRECT sink
        self.refusals = []      # unresolvable indirection, as messages
        self.types = set(_C_TYPE_WORDS)
        self.ctx_globals = set()
        self.cuts = set()       # (relpath, function, callee) not followed
        self._edges = {}

    # -- building ----------------------------------------------------------
    def add_file(self, relpath, src):
        """Phase 1: read the file. Raises what `compiler_view` raises."""
        view = compiler_view(src)
        self.views[relpath] = view
        self.types.update(_typedef_names(view.code))
        self.ctx_globals.update(_file_scope_ctx(view))

    def build(self):
        """Phase 2, once every file is read: types are known, so a cast can
        be told from a parenthesised callee."""
        for relpath, view in self.views.items():
            code = normalise_callees(view.code, self.is_type)
            self.file_code[relpath] = code
            for name, start, end in view.spans:
                params = parameters_before(view.code, start)
                self._add(relpath, name, code[start:end], start,
                          _param_names(params),
                          set(_CTX_DECL.findall((params or "") + " "
                                                + code[start:end])))
            for m in _DEFINE.finditer(view.matchmask):
                body = normalise_callees(m.group(4), self.is_type)
                params = (_param_names(m.group(3))
                          if m.group(2) is not None else [])
                self._add(relpath, m.group(1), body, m.start(4), params, set())
                self.macros.add((relpath, m.group(1)))
        for relpath, code in self.file_code.items():
            for m in _MEMBER_ASSIGN.finditer(code):
                member, target = m.group(1), m.group(2)
                for node in self.resolve_all(target, relpath):
                    self.by_member.setdefault(member, set()).add(node)
        for node, body in self.bodies.items():
            if _SINK.search(body):
                self.writes.add(node)

    def _add(self, relpath, name, body, start, params, ctx):
        node = (relpath, name)
        # A name defined twice in one file (an #if/#else pair) keeps BOTH
        # bodies, so neither is silently dropped. They are joined by a `;`
        # (r4): run together, `isfinite(n)` + `((n) == (n))` read as one
        # call through `isfinite(n)`, and `foo` + `(x)` as a call of foo.
        if node in self.bodies:
            body, start = ";" + body, start - 1
        self.bodies[node] = self.bodies.get(node, "") + body
        self.segments.setdefault(node, []).append((relpath, start, len(body)))
        if node not in self.params or len(params) > len(self.params[node]):
            self.params[node] = params
        self.ctx_decl.setdefault(node, set()).update(ctx)
        if node not in self.by_name.setdefault(name, []):
            self.by_name[name].append(node)

    def is_type(self, name):
        return name in self.types or name.endswith("_t")

    def line_of(self, node, pos):
        """Physical line of offset `pos` into `node`'s body."""
        for relpath, start, length in self.segments[node]:
            if pos < length:
                return self.views[relpath].line(start + pos)
            pos -= length
        return 0

    # -- resolution --------------------------------------------------------
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

        Across LAYERS (round 3): libscpi calls resolve inside libscpi only
        (it does not call our code by name); our calls prefer our own
        definition and fall back to libscpi's.
        """
        return _pick(self.by_name.get(name, []), from_file)

    def resolve_macros(self, name, from_file):
        """The MACRO definitions a use of `name` from `from_file` expands."""
        return _pick([n for n in self.by_name.get(name, [])
                      if n in self.macros], from_file)

    def macro_uses(self, node):
        """Macros `node` names, called or NOT (round 4).

        `AUDIT_ACK;` and `REPLY;` expand in place exactly like `REPLY(...)`
        does, but no `(` follows them, so the call rule never saw them and a
        writing object-like macro was a silent leaf in a function body
        (audit r3, findings 0 and 5). Following them is the one case where
        following beats refusing: the body is right here, and a macro that
        names another macro is followed again, transitively, by the
        macro-body rule in `edges`."""
        relpath, fn = node
        out = set()
        for word in set(_IDENT.findall(self.bodies.get(node, ""))):
            if word != fn:
                out.update(self.resolve_macros(word, relpath))
        out.discard(node)
        return out

    def edges(self, node):
        """Callees of `node`: direct calls, function-pointer arguments, calls
        through a member whose assignments we could find, the targets of a
        local function pointer, every macro it names (round 4), and -- in a
        macro -- any function it names. Declared cuts (`set_cuts`) are not
        followed."""
        if node in self._edges:
            return self._edges[node]
        body = self.bodies.get(node, "")
        relpath, fn = node
        out = set()
        for name, _, _, _ in _calls(body):
            if (relpath, fn, name) not in self.cuts:
                out.update(self.resolve_all(name, relpath))
        for name in _pointer_arguments(body, self.by_name):
            out.update(self.resolve_all(name, relpath))
        for m in _MEMBER_CALL.finditer(body):
            if (relpath, fn, m.group(1)) not in self.cuts:
                out.update(self.by_member.get(m.group(1), ()))
        out.update(self.macro_uses(node))
        if node in self.macros:
            for word in _IDENT.findall(body):
                if word in self.by_name and word != fn:
                    out.update(self.resolve_all(word, relpath))
        for name, _, _, member in _calls(body):
            if member or name in _NOT_CALLEES or self.resolve_all(name, relpath):
                continue
            targets, _ = self.pointer_targets(node, name)
            out.update(targets)
        out.discard(node)
        self._edges[node] = out
        return out

    def unresolved_members(self, node):
        """Member calls in `node` whose every value is not accounted for
        (`member_why`)."""
        out = set()
        for m in _MEMBER_CALL.finditer(self.bodies.get(node, "")):
            member = m.group(1)
            if member != "write" and self.member_why(member):
                out.add(member)
        return out

    # -- member completeness (round 4) --------------------------------------
    def _texts(self):
        """(relpath, text) for every file view and every macro body: where a
        member can be assigned or a struct initialised."""
        for relpath, code in self.file_code.items():
            yield relpath, code
        for node in self.macros:
            yield node[0], self.bodies.get(node, "")

    def member_why(self, member, _seen=None):
        """None when EVERY value member `member` can hold is a function this
        graph names; otherwise why not (round 4).

        Round 3 counted a member resolved once ANY assignment to its name
        was found, so `{ .run = Quiet }` in one place certified
        `loud.run(context)` where `loud = { Writer }` (audit r3, findings 2
        and 7). Now each of these makes it incomplete:

        * an assignment or designated initialiser whose value is not NULL, 0,
          a sink, a known function, or another member that is itself
          complete (`.active = ctx.nextState`);
        * the member's ADDRESS taken (`&s.run`), since a store through it is
          invisible;
        * no struct or union in the scan set declaring it -- then its
          initialisers cannot be found;
        * a POSITIONAL initialiser (`{ Writer }`, brace-elided or nested) of
          any type that declares it or embeds one that does, or a compound
          literal of one.
        """
        cache = self.__dict__.setdefault("_member_cache", {})
        if member in cache:
            return cache[member]
        seen = set(_seen or ())
        if member in seen:
            return None                  # a cycle adds no new value
        seen.add(member)
        why = self._member_why(member, seen)
        if _seen is None:
            cache[member] = why
        return why

    def _member_why(self, member, seen):
        esc = re.escape(member)
        store = re.compile(r"(?:->|\.)\s*" + esc + r"\s*=(?!=)")
        addr = re.compile(r"(?<!&)&(?!&)[^;,(){}=&|]*?(?:->|\.)\s*" + esc
                          + r"\b(?!\s*\()")
        for relpath, text in self._texts():
            for m in store.finditer(text):
                rhs = text[m.end():_expr_end(text, m.end())]
                arms = _split_top_level(rhs, "?:")
                for arm in (arms[1:] if len(arms) > 1 else arms):
                    c = _clean_arg(arm)
                    # A constant cannot be a function, so it adds no target
                    # (only 0 converts to a pointer, but any constant of a
                    # SAME-NAMED data member elsewhere is equally inert).
                    if c in _INIT_NEUTRAL or _CONSTANT.match(c) \
                            or _SINK_NAME.match(c) or c in self.by_name:
                        continue
                    via = re.search(r"(?:->|\.)\s*([A-Za-z_]\w*)\Z", c)
                    if via and not self.member_why(via.group(1), seen):
                        continue
                    return ("%s assigns it %r, which is not a function this "
                            "checker can name" % (relpath, c.strip()))
            if addr.search(text):
                return "%s takes its address" % relpath
        types = self._types_declaring(member)
        if not types:
            return "no struct or union in the scan set declares it"
        hit = self._positional_init(types)
        if hit:
            return hit
        return None

    def _struct_bodies(self):
        """[(relpath, names, body)] for every struct/union definition: its
        tag and every typedef name for it."""
        if hasattr(self, "_structs"):
            return self._structs
        out = []
        head = re.compile(r"\b(?:struct|union)\s*([A-Za-z_]\w*)?\s*\{")
        alias = re.compile(r"\btypedef\s+(?:struct|union)\s+([A-Za-z_]\w*)"
                           r"\s+([^;{}]*);")
        aliases = {}
        for relpath, code in self.file_code.items():
            for m in alias.finditer(code):
                for part in _split_top_level(m.group(2)):
                    ids = _IDENT.findall(part)
                    if ids:
                        aliases.setdefault(m.group(1), set()).add(ids[-1])
        for relpath, code in self.file_code.items():
            for m in head.finditer(code):
                close = _match_paren(code, m.end() - 1)
                names = {m.group(1)} if m.group(1) else set()
                k = _prev_index(code, m.start())
                if k >= 0 and _word_ending(code, k) == "typedef":
                    semi = code.find(";", close)
                    for part in _split_top_level(
                            code[close + 1:semi if semi > 0 else len(code)]):
                        ids = _IDENT.findall(part)
                        if ids:
                            names.add(ids[-1])
                for n in list(names):
                    names |= aliases.get(n, set())
                out.append((relpath, names, code[m.end():close]))
        self._structs = out
        return out

    def _types_declaring(self, member):
        """Every type name that declares `member`, or embeds (by value) a
        type that does, to a fixed point."""
        decl = re.compile(r"\(\s*\*\s*" + re.escape(member) + r"\s*\)|\b"
                          + re.escape(member) + r"\s*[;,\[:]")
        structs = self._struct_bodies()
        types = set()
        for _, names, body in structs:
            if decl.search(body):
                types |= names
        changed = True
        while changed:
            changed = False
            for _, names, body in structs:
                if names <= types:
                    continue
                for t in types:
                    if re.search(r"\b" + re.escape(t) + r"\b\s+[A-Za-z_]",
                                 body):
                        types |= names
                        changed = True
                        break
        return types

    def _positional_init(self, types):
        """Where a positional initialiser of one of `types` is, or None."""
        names = "|".join(re.escape(t) for t in sorted(types))
        decl = re.compile(r"\b(?:" + names + r")\b")
        lit = re.compile(r"\(\s*(?:(?:struct|union)\s+)?(?:" + names
                         + r")\s*\)\s*\{")
        for relpath, text in self._texts():
            for m in lit.finditer(text):
                if _positional(text, m.end() - 1, self.is_type):
                    return "%s has a positional compound literal of it" % relpath
            for m in decl.finditer(text):
                # The declaration this type name heads: up to `=` then `{`.
                j = m.end()
                while j < len(text) and text[j] not in "=;{}()":
                    j += 1
                if j >= len(text) or text[j] != "=":
                    continue
                k = j + 1
                while k < len(text) and text[k] in _WS:
                    k += 1
                if k < len(text) and text[k] == "{" and _positional(
                        text, k, self.is_type):
                    line = (self.views[relpath].line(m.start())
                            if text is self.file_code.get(relpath) else 0)
                    return ("%s:%d initialises it POSITIONALLY, so which "
                            "function lands in which member is not named"
                            % (relpath, line))
        return None

    def pointer_targets(self, node, name):
        """-> (targets, complete) for a LOCAL function pointer `name` in
        `node`: the functions its assignments name, and whether EVERY
        assignment named one. No assignment, or its address escaping
        (`&name`), is incomplete."""
        body = self.bodies.get(node, "")
        esc = re.escape(name)
        rhs = []
        for m in re.finditer(r"(?<![\w.>$])" + esc + r"\s*=(?!=)", body):
            rhs.append(body[m.end():])
        for m in re.finditer(r"\(\s*\*\s*" + esc + r"\s*\)\s*\(", body):
            close = _match_paren(body, m.end() - 1)
            after = re.match(r"\s*=(?!=)", body[close + 1:])
            if after:
                rhs.append(body[close + 1 + after.end():])
        targets, complete = set(), bool(rhs)
        if re.search(r"&\s*" + esc + r"\b", body):
            complete = False
        for text in rhs:
            expr = _split_top_level(_split_top_level(text, ";")[0])[0]
            arms = _split_top_level(expr, "?:")
            for arm in (arms[1:] if len(arms) > 1 else arms):
                c = _clean_arg(arm)
                if c in ("NULL", "0") or _SINK_NAME.match(c):
                    continue
                if c in self.by_name:
                    targets.update(self.resolve_all(c, node[0]))
                else:
                    complete = False
        return targets, complete

    def param_call_ok(self, node, name):
        """-> (ok, why) for a call through PARAMETER `name` of `node`: every
        call site of `node` must pass, in that slot, a function this graph
        knows (or NULL), and `node` must never be referenced other than by a
        call -- otherwise some call site is invisible."""
        k = self.params[node].index(name)
        fname = node[1]
        bare = re.compile(r"\b" + re.escape(fname) + r"\b(?!\s*\()")
        for relpath, code in self.file_code.items():
            if bare.search(code):
                return False, ("%s() is referenced without being called in "
                               "%s, so not every call site is visible"
                               % (fname, relpath))
        sites = 0
        for caller, body in self.bodies.items():
            if bare.search(body):
                return False, ("%s() is referenced without being called in "
                               "%s()" % (fname, caller[1]))
            for cname, _, args, member in _calls(body):
                if cname != fname or member or node not in \
                        self.resolve_all(cname, caller[0]):
                    continue
                sites += 1
                arg = _clean_arg(args[k]) if k < len(args) else ""
                if not (arg in ("NULL", "0") or arg in self.by_name
                        or _SINK_NAME.match(arg)):
                    return False, ("%s() passes %r as %r, which is not a "
                                   "function this graph knows"
                                   % (caller[1], arg, name))
        if not sites:
            return False, "no call site of %s() was found" % fname
        return True, None

    def set_cuts(self, declared):
        """Validate declared indirections; keep the ones that may CUT an edge.

        -> problems. A member call whose target no assignment names is
        declarable (r2: nothing to follow). A RESOLVABLE member may be cut
        only at a libscpi call site (the `->error` route to `**ERROR`); in
        our code it is followed, and declaring it is refused. A direct call
        may be cut only when it resolves solely to a libscpi parser entry
        point (`_DISPATCHERS`). Anything else is refused, so the list can
        never be used to cut an ordinary writer out of the graph.
        """
        problems = []
        for relpath, fn, callee in sorted(declared):
            node = (relpath, fn)
            body = self.bodies.get(node)
            if body is None:
                continue                 # stale: reported by the caller
            members = {m.group(1) for m in _MEMBER_CALL.finditer(body)}
            if callee in members:
                if _is_libscpi(relpath):
                    self.cuts.add((relpath, fn, callee))
                elif self.member_why(callee):
                    # r4: declared, NOT cut -- a member not every value of
                    # which is named still has its named targets followed.
                    pass
                else:
                    problems.append(
                        "%s: %s() calls through member %r, which IS "
                        "resolvable (an assignment names its targets) and so "
                        "is followed. Only a libscpi call site may declare a "
                        "resolvable member; remove the line."
                        % (relpath, fn, callee))
                continue
            direct = [n for n, _, _, member in _calls(body)
                      if n == callee and not member]
            targets = self.resolve_all(callee, relpath) if direct else []
            if targets and all(_is_libscpi(t[0]) and t[1] in _DISPATCHERS
                                for t in targets):
                self.cuts.add((relpath, fn, callee))
            elif direct:
                problems.append(
                    "%s: %s() calls %s(), which may not be declared: only a "
                    "call into libscpi's parser entry points (%s) can be cut "
                    "from the graph." % (relpath, fn, callee,
                                         ", ".join(_DISPATCHERS)))
        return problems


def _file_scope_ctx(view):
    """Names declared `scpi_t` OUTSIDE every function body and parameter
    list -- a global or a struct member such as `scpiContext`."""
    code = view.code
    depth, paren = [0] * (len(code) + 1), 0
    for i, c in enumerate(code):
        if c == "(":
            paren += 1
        elif c == ")":
            paren -= 1
        depth[i] = paren
    out = set()
    for m in _CTX_DECL.finditer(code):
        j = m.start(1)
        if depth[j] == 0 and not any(s <= j < e for _, s, e in view.spans):
            out.add(m.group(1))
    return out


def _pointer_arguments(body, by_name):
    """Known function names passed as ARGUMENTS, not called.

    This is the hole that makes a miss a false negative: a callback handed to
    a runner is invoked, but no `(` follows its name at the pass site.
    """
    found = set()
    for _, _, args, _ in _calls(body):
        for arg in args:
            cleaned = _clean_arg(arg)
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
def _context_names(g, examined):
    """node -> the identifiers that carry a context inside it.

    Its own `scpi_t` declarations, every file-scope one, and -- to a fixed
    point -- each parameter some examined caller binds to an argument that
    carries a context. The binding is what follows a context into a macro
    (`REPLY(context, ...)` binds REPLY's first parameter) or into a helper
    that took it as `void *`.

    Round 4 adds two more routes, both to the same fixed point:

    * ALIASES. An lvalue assigned or initialised from the context ITSELF --
      a name, or a member/element chain ending in one, under any casts,
      `&`, `*` or `?:` arm -- carries it too: `void *alias = context;`,
      then `c2 = c1;` with `c1 = context;` later in the body (the order
      does not matter -- this is flow-insensitive). Audit r3 findings 3 and
      8 passed `alias` to an unnamed callee and the refusal never fired.
      DATA read through the context (`n = context->idx`) is not an alias:
      measured on the real tree, treating it as one tainted every length
      and index in libscpi's parser and refused `strlen`/`memcpy` calls by
      the dozen. A function RETURNING its context argument is not followed
      either -- that is stated in the module docstring as a residual.
    * MACRO SCOPE. A macro expands in its user's scope, so it inherits every
      context name its user has, and a macro whose body names one that is
      not its own parameter is itself a carrier wherever it is used
      (`#define ALIAS context`). Its parameters are excluded because the
      ARGUMENT is what the caller's own check already sees.
    """
    ctx = {n: set(g.ctx_decl.get(n, ())) | g.ctx_globals for n in examined}
    changed = True
    while changed:
        changed = False
        for node in examined:
            body = g.bodies.get(node, "")
            for base, rhs in _assignments(body):
                if base not in ctx[node] and _is_context(rhs, ctx[node]):
                    ctx[node].add(base)
                    changed = True
            for macro in _macro_scope(g, node):
                if macro not in ctx:
                    continue
                if not ctx[node] <= ctx[macro]:
                    ctx[macro] |= ctx[node]
                    changed = True
                own = set(g.params.get(macro, ()))
                if macro[1] not in ctx[node] and _mentions(
                        g.bodies.get(macro, ""), ctx[macro] - own):
                    ctx[node].add(macro[1])
                    changed = True
            for name, _, args, member in _calls(body):
                if member or name in _NOT_CALLEES:
                    continue
                flags = [_mentions(a, ctx[node]) for a in args]
                if not any(flags):
                    continue
                for callee in g.resolve_all(name, node[0]):
                    if callee not in ctx:
                        continue
                    ps = g.params.get(callee, [])
                    for i, flag in enumerate(flags):
                        if not flag:
                            continue
                        p = ps[i] if i < len(ps) else (
                            ps[-1] if ps and ps[-1] == "__VA_ARGS__" else None)
                        if p and p not in ctx[callee]:
                            ctx[callee].add(p)
                            changed = True
    return ctx


_CHAIN = re.compile(r"[A-Za-z_]\w*(?:\s*(?:->|\.)\s*[A-Za-z_]\w*"
                    r"|\s*\[[^\]]*\])*\Z")


def _is_context(expr, names):
    """True when `expr` evaluates to a context (not to data read through
    one): some `?:` arm is, under casts, `&`, `*` and parentheses, a name
    or a member/element chain whose LAST name is in `names`."""
    arms = _split_top_level(expr, "?:")
    for arm in (arms[1:] if len(arms) > 1 else arms):
        c, prev = arm.strip(), None
        while c != prev:
            prev, c = c, _clean_arg(c)
        if _CHAIN.match(c):
            last = _IDENT.findall(re.sub(r"\[[^\]]*\]", " ", c))
            if last and last[-1] in names:
                return True
    return False


def _macro_scope(g, node):
    """The macros `node` expands -- called or named bare."""
    return {n for n in g.edges(node) if n in g.macros}


def _unnamed_context_calls(g, examined):
    """-> (declarable, refusals): calls in the closure that pass a context
    to a target this graph cannot name.

    `declarable` is {(file, line, callee)} for a name with no source in the
    scan set at all (the toolchain's `memset`) -- the only kind
    `scpi_nonquery_write-external.txt` may declare. A pointer this file
    could not resolve is a REFUSAL, not declarable: it is our own code, and
    it has to be made resolvable.
    """
    ctx = _context_names(g, examined)
    mentioned = set()
    for code in g.file_code.values():
        for m in re.finditer(r"\b([A-Za-z_]\w*)\b(?!\s*\()", code):
            mentioned.add(m.group(1))
    declarable, refusals = set(), []
    for node in sorted(examined):
        relpath, fn = node
        body = g.bodies.get(node, "")
        for name, pos, args, member in _calls(body):
            if (member or name in _NOT_CALLEES or name.startswith("__builtin_")
                    or _SINK_NAME.match(name) or g.is_type(name)
                    or g.resolve_all(name, relpath)
                    or (relpath, fn, name) in g.cuts):
                continue
            if not any(_mentions(a, ctx[node]) for a in args):
                continue
            line = g.line_of(node, pos)
            where = "%s:%d: %s()" % (relpath, line, fn)
            if name in g.params.get(node, []):
                ok, why = g.param_call_ok(node, name)
                if not ok:
                    refusals.append(
                        "%s calls through parameter %r with the context, and "
                        "its targets cannot all be named: %s. A write behind "
                        "it would be MISSED." % (where, name, why))
                continue
            targets, complete = g.pointer_targets(node, name)
            if complete:
                continue
            if targets or name in mentioned or re.search(
                    r"(?<![\w.>$])" + re.escape(name) + r"\s*=(?!=)", body):
                refusals.append(
                    "%s calls through %r with the context, and not every "
                    "value it can hold is a function this checker can name, "
                    "so a write behind it would be MISSED. Assign it only "
                    "named functions, or make the call direct." % (where, name))
                continue
            declarable.add((relpath, line, name))
        # r4: a callee that is not a name at all -- `hooks[0](ctx)`,
        # `hooks[i](ctx)`, `get()(ctx)` -- is never resolvable, so it is
        # REFUSED. No shape-specific follower: audit r3 findings 1 and 6
        # were this class, invisible to `_calls`. Refused whether or not an
        # argument carries the context, because the target can reach the
        # stream through a GLOBAL one (`gClient.scpiContext`) with no
        # argument at all; measured: the real tree has NO computed callee
        # anywhere, so this costs nothing.
        for pos, callee, args in _computed_calls(body, g.is_type):
            refusals.append(
                "%s:%d: %s() calls through the computed callee %r. A callee "
                "that is not a plain name cannot be resolved, so a write "
                "behind it would be MISSED. Call a named function instead."
                % (relpath, g.line_of(node, pos), fn,
                   " ".join(callee.split())))
    return declarable, refusals


def analyse(root, scpi_c, declared_indirect=frozenset()):
    """(findings, queries_that_write, refusals, stats).

    `findings` is [(pattern, callback, route)] for non-query commands that
    reach a sink; `route` names the first sink-owning function on the path.
    `declared_indirect` is the indirect list's keys, needed BEFORE the graph
    is walked because a valid declaration cuts an edge (`Graph.set_cuts`).
    """
    files = source_files(root)
    problems = _require_scan_set(files, root)
    if problems:
        return [], [], problems, {}
    lib = libscpi_files(root)

    g = Graph()
    for path in files + lib:
        rel = os.path.relpath(path, root).replace(os.sep, "/")
        with open(path, encoding="utf-8", errors="replace") as fh:
            try:
                g.add_file(rel, fh.read())
            except AmbiguousDefinition as exc:   # incl. cdef.UnreadDefinition
                g.refusals.append("%s: %s" % (rel, exc))
    g.build()
    g.refusals.extend(g.set_cuts(declared_indirect))

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
        # outside the scan set was dropped and reported clean. Since round 3
        # libscpi's own table callbacks resolve (it is indexed), so a callback
        # landing here is defined NOWHERE we read -- and is refused unless
        # declared against a checked reason.
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
    for cut in g.cuts:
        if cut[:2] in examined:
            indirect.add(cut)
    external, ctx_refusals = _unnamed_context_calls(g, examined)
    g.refusals.extend(ctx_refusals)

    stats = {"files": len(files), "libscpi_files": len(lib),
             "functions": len(g.bodies) - len(g.macros),
             "macros": len(g.macros), "registrations": len(rows),
             "non_query": sum(1 for p, _ in rows if "?" not in p),
             "writers": len(g.writes), "examined": len(examined),
             "indirect": sorted(indirect), "unresolved": sorted(unresolved),
             "external": sorted(external)}
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
    """{(file, function, callee)} -> note, plus parse problems.

    A call this checker does not follow is the only place a write can hide
    from it, so each one is declared here with the issue that tracks it:
    a member call no assignment explains (the CircularBuffer
    `process_callback`), libscpi's two `->error` calls (the `**ERROR` route,
    out of scope), and the transports' calls into libscpi's parser. Which of
    those may CUT a resolvable edge is decided by `Graph.set_cuts`, not
    here. The list is shrink-only like the other, so a NEW indirection
    inside the non-query closure fails the gate and has to be looked at by
    a person.
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
                                "<callee>\\t#<issue>', got %r" % (path, n, s))
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
    checked, never assumed. Empty since round 3: libscpi's eight table
    callbacks used to live here, and now resolve inside its indexed layer.
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


def read_external(path):
    """{(file, line, callee)} -> note, plus parse problems.

    One call site per line, `<file>:<line>\\t<callee>\\t#<issue>`: a call
    that passes a context to a callee with NO source anywhere in the scan
    set (a toolchain function). Keyed on the line on purpose: a declaration
    covers the call it was checked against and no other.
    """
    allow, problems = {}, []
    if not os.path.exists(path):
        return allow, ["external-callee list %r not found" % path]
    with open(path, encoding="utf-8") as fh:
        for n, line in enumerate(fh, 1):
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            parts = [p.strip() for p in s.split("\t") if p.strip()]
            where = re.match(r"^(\S+):(\d+)$", parts[0]) if parts else None
            if len(parts) != 3 or not where:
                problems.append("%s:%d: expected '<file>:<line>\\t<callee>\\t"
                                "#<issue>', got %r" % (path, n, s))
                continue
            if not re.match(r"^#\d+$", parts[2]):
                problems.append("%s:%d: third field must be the issue that "
                                "tracks it, got %r" % (path, n, parts[2]))
                continue
            allow[(where.group(1), int(where.group(2)), parts[1])] = parts[2]
    return allow, problems


def evaluate(findings, refusals, stats, allow, indirect_allow,
             unresolved_allow, external_allow, paths):
    """-> [problem]: what the gate fails on. Shared by `main` and the
    self-test, so a fixture is judged exactly as the real tree is."""
    bad = list(refusals)

    # A non-query callback the gate could not analyse at all. Undeclared, this
    # used to be a silent pass -- the gate reporting clean on a command it
    # never looked at.
    seen_unresolved = set(stats.get("unresolved", []))
    for key in sorted(seen_unresolved - set(unresolved_allow)):
        bad.append("%s (%s) is registered without '?' but its callback is "
                   "defined outside the scanned sources, so this gate did NOT "
                   "analyse it. Declare it in %s against a CHECKED reason, or "
                   "bring its definition into the scan set."
                   % (key[0], key[1], paths["unresolved"]))
    for key in sorted(set(unresolved_allow) - seen_unresolved):
        bad.append("%s (%s) now resolves inside the scanned sources. Remove "
                   "the line from %s so it is analysed rather than exempted."
                   % (key[0], key[1], paths["unresolved"]))

    # An indirection inside the non-query closure is the one way a write can
    # hide from this gate, so an undeclared one FAILS rather than passing
    # quietly. This is the "follow it or refuse it" half of the design.
    seen_indirect = set(stats.get("indirect", []))
    for key in sorted(seen_indirect - set(indirect_allow)):
        bad.append("%s: %s() calls through member %r, which this checker "
                   "cannot resolve, so a response write behind it would be "
                   "MISSED. Declare it in %s with the issue that tracks it, "
                   "or make the call direct."
                   % (key[0], key[1], key[2], paths["indirect"]))
    for key in sorted(set(indirect_allow) - seen_indirect):
        bad.append("%s: %s() no longer makes the declared call %r inside the "
                   "non-query closure (or the declaration was refused). "
                   "Remove the line from %s -- a stale declaration hides the "
                   "next real one." % (key[0], key[1], key[2],
                                       paths["indirect"]))

    # A context handed to a callee with no source here: declared per call
    # site, both directions, like the others.
    seen_external = set(stats.get("external", []))
    for key in sorted(seen_external - set(external_allow)):
        bad.append("%s:%d passes the context to %s(), which has no source in "
                   "the scan set, so a response write behind it would be "
                   "MISSED. If it is a toolchain function that cannot reach "
                   "the stream, declare this call site in %s."
                   % (key[0], key[1], key[2], paths["external"]))
    for key in sorted(set(external_allow) - seen_external):
        bad.append("%s:%d: the declared call to %s() is no longer an "
                   "unresolved context-carrying call in the non-query "
                   "closure (moved, removed, or the name now RESOLVES and is "
                   "followed). Remove or correct the line in %s."
                   % (key[0], key[1], key[2], paths["external"]))

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
    return bad


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
                    help="declared calls the checker does not follow")
    ap.add_argument("--unresolved",
                    default="tools/lint/scpi_nonquery_write-unresolved.txt",
                    help="declared callbacks defined outside the scan set")
    ap.add_argument("--external",
                    default="tools/lint/scpi_nonquery_write-external.txt",
                    help="declared context-carrying calls to callees with no "
                         "source in the scan set")
    ap.add_argument("--list", action="store_true",
                    help="print every finding and every query that writes")
    ap.add_argument("--self-test", action="store_true",
                    help="prove the checker on synthetic sources, then exit")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    allow, allow_problems = read_allow(args.allow)
    indirect_allow, indirect_problems = read_indirect(args.indirect)
    unresolved_allow, unresolved_problems = read_unresolved(args.unresolved)
    external_allow, external_problems = read_external(args.external)
    findings, qwrites, refusals, stats = analyse(
        args.root, args.scpi, frozenset(indirect_allow))

    if stats:
        print("scanned %d file(s) + %d libscpi file(s): %d function(s), %d "
              "macro(s); %d registration(s), %d without '?'; %d write "
              "directly; examined %d in the non-query closure"
              % (stats["files"], stats["libscpi_files"], stats["functions"],
                 stats["macros"], stats["registrations"], stats["non_query"],
                 stats["writers"], stats["examined"]))

    if args.list:
        for pattern, cb, where in findings:
            mark = "allowed" if (pattern, cb) in allow else "NEW"
            print("  [%-7s] %-42s %-28s %s" % (mark, pattern, cb, where))
        if qwrites:
            print("  -- queries that write (expected, context only) --")
            for pattern, cb, where in qwrites[:5]:
                print("  [query  ] %-42s %-28s %s" % (pattern, cb, where))
            print("  ... %d total" % len(qwrites))

    bad = allow_problems + indirect_problems + unresolved_problems \
        + external_problems
    if stats and not stats["libscpi_files"]:
        bad.append("no libscpi sources under %r/%s: libscpi's table callbacks "
                   "and every helper they call would be unresolved. The "
                   "library moved -- update _LIBSCPI_DIRS."
                   % (args.root, _LIBSCPI_DIRS[0]))
    bad += evaluate(findings, refusals, stats, allow, indirect_allow,
                    unresolved_allow, external_allow,
                    {"indirect": args.indirect, "unresolved": args.unresolved,
                     "external": args.external})

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

# ---- round 3 (the r2 audit's misses) -------------------------------------
# Each row: id, why, the callback's code, extra files {relpath: text}, the
# declared lists, the findings it must report, and a substring that must
# appear among the gate's problems (None: the gate must PASS outright).
# `R2` says what the round-2 checker did with the same input, measured by
# running these rows against `git show fc700fda:tools/lint/...`.

# A minimal libscpi layer: one result function that writes through a
# prefix-free internal helper (so only STRUCTURAL following catches it) and
# an identity query built on it. Shape of the real ieee488.c/parser.c.
_FAKE_LIBSCPI = {
    "libraries/scpi/libscpi/src/parser.c": '''
static size_t writeData(scpi_t * context, const char * data, size_t len) {
    return context->interface->write(context, data, len);
}
size_t SCPI_ResultCharacters(scpi_t * context, const char * data, size_t len) {
    return writeData(context, data, len);
}
scpi_bool_t SCPI_ParamInt32(scpi_t * context, int32_t * value, scpi_bool_t mandatory) {
    *value = 0;
    return TRUE;
}
''',
    "libraries/scpi/libscpi/src/ieee488.c": '''
scpi_result_t SCPI_CoreIdnQ(scpi_t * context) {
    writeData(context, "DAQiFi", 6);
    return SCPI_RES_OK;
}
''',
}

# libscpi's error emission and parser entry, and the code of ours that they
# meet: the `**ERROR` writer installed as `.error`, and a console callback,
# registered from a power-up path, that feeds the parser.
_FAKE_LIBSCPI_ERR = {
    "libraries/scpi/libscpi/src/error.c": '''
static void SCPI_ErrorEmit(scpi_t * context, int16_t err) {
    if (context->interface && context->interface->error) {
        context->interface->error(context, err);
    }
}
void SCPI_ErrorPush(scpi_t * context, int16_t err) {
    SCPI_ErrorEmit(context, err);
}
''',
    "libraries/scpi/libscpi/src/parser.c": '''
static size_t writeData(scpi_t * context, const char * data, size_t len) {
    return context->interface->write(context, data, len);
}
scpi_bool_t SCPI_Input(scpi_t * context, const char * data, int len) {
    writeData(context, ";", 1);
    return TRUE;
}
''',
}
_IFACE_STRUCT = '''
struct _scpi_interface_t {
    size_t (*error)(scpi_t * context, int16_t err);
};
'''
_CUT_OURS = _IFACE_STRUCT + '''
static size_t ErrWriter(scpi_t * context, int16_t err) {
    scpi_printf(context, "**ERROR\\n");
    return 0;
}
static scpi_interface_t iface = { .error = ErrWriter, };
static int CommandComplete(int argc, const char * const * argv) {
    return SCPI_Input(&gClient.scpiContext, argv[0], argc);
}
static scpi_result_t CbSet(scpi_t * context) {
    SCPI_ErrorPush(context, -222);
    return SCPI_RES_ERR;
}
static scpi_result_t CbPow(scpi_t * context) {
    console_set_callback(&gConsole, CommandComplete);
    return SCPI_RES_OK;
}
'''

_HELPER_BODY = '''{
    scpi_printf(context, "x\\n");
}
static scpi_result_t CbH(scpi_t * context) {
    H(context);
    return SCPI_RES_OK;
}
'''
_REG_H = '    {.pattern = "SYST:ATTR", .callback = CbH,},'


def _r3(rid, why, entries, code, files=None, expect=(), problem=None,
        external=None, indirect=None):
    return {"id": rid, "why": why, "source": _case(entries, code),
            "files": files or {}, "expect": list(expect),
            "problem": problem, "external": external or {},
            "indirect": indirect or {}}


_SELFTEST_R3 = [
    _r3("libscpi-callee",
        "a libscpi callee that writes (SCPI_CoreIdnQ) is FOLLOWED into the "
        "libscpi layer and reported. R2: libscpi was excluded, the call was "
        "a silent leaf, the gate PASSED",
        '    {.pattern = "SYST:IDN", .callback = CbIdn,},',
        '''
static scpi_result_t CbIdn(scpi_t * context) {
    SCPI_CoreIdnQ(context);
    return SCPI_RES_OK;
}
''', files=_FAKE_LIBSCPI, expect=["SYST:IDN"]),

    _r3("libscpi-absent",
        "the same call with NO libscpi source to follow is REFUSED as a "
        "context-carrying call to an unnamed target. R2: silent leaf, PASS",
        '    {.pattern = "SYST:IDN", .callback = CbIdn,},',
        '''
static scpi_result_t CbIdn(scpi_t * context) {
    SCPI_CoreIdnQ(context);
    return SCPI_RES_OK;
}
''', problem="passes the context to SCPI_CoreIdnQ()"),

    _r3("libscpi-reader",
        "a libscpi callee that only READS (SCPI_ParamInt32) is followed and "
        "does NOT make its caller a writer -- the control for the row above",
        '    {.pattern = "SYST:SET", .callback = CbSet,},',
        '''
static scpi_result_t CbSet(scpi_t * context) {
    int32_t v;
    SCPI_ParamInt32(context, &v, TRUE);
    return SCPI_RES_OK;
}
''', files=_FAKE_LIBSCPI),

    _r3("local-pointer",
        "a call through a LOCAL function pointer is resolved through its "
        "assignment and reported. R2: `fp` was a silent leaf, PASS",
        '    {.pattern = "SYST:LOCAL", .callback = CbLocal,},',
        '''
static void Writer(scpi_t * context) {
    scpi_printf(context, "x\\n");
}
static scpi_result_t CbLocal(scpi_t * context) {
    void (*fp)(scpi_t *) = Writer;
    fp(context);
    return SCPI_RES_OK;
}
''', expect=["SYST:LOCAL"]),

    _r3("local-pointer-sink",
        "a local function pointer holding the SINK itself is reported. R2: "
        "`SCPI_ResultInt32;` has no `(`, so no sink matched, PASS",
        '    {.pattern = "SYST:LSINK", .callback = CbLSink,},',
        '''
static scpi_result_t CbLSink(scpi_t * context) {
    size_t (*fp)(scpi_t *, int32_t) = SCPI_ResultInt32;
    fp(context, 7);
    return SCPI_RES_OK;
}
''', expect=["SYST:LSINK"]),

    _r3("local-pointer-unnamed",
        "a local pointer whose value cannot be named is REFUSED, not "
        "assumed harmless. R2: silent leaf, PASS",
        '    {.pattern = "SYST:LOOK", .callback = CbLook,},',
        '''
static scpi_result_t CbLook(scpi_t * context) {
    void (*fp)(scpi_t *) = gHooks[context->idx];
    fp(context);
    return SCPI_RES_OK;
}
''', problem="calls through 'fp' with the context"),

    _r3("paren-sink",
        "a PARENTHESISED callee `(SCPI_ResultInt32)(context, 7)` is "
        "reported. R2: the sink regex needs `(` right after the name, PASS",
        '    {.pattern = "SYST:PAREN", .callback = CbParen,},',
        '''
static scpi_result_t CbParen(scpi_t * context) {
    (SCPI_ResultInt32)(context, 7);
    return SCPI_RES_OK;
}
''', expect=["SYST:PAREN"]),

    _r3("paren-helper",
        "`(Writer)(context)` and `(*Writer)(context)` reach our own writer. "
        "R2: no `Writer(` in the text, so no edge, PASS",
        '    {.pattern = "SYST:PH", .callback = CbPH,},\n'
        '    {.pattern = "SYST:PS", .callback = CbPS,},',
        '''
static void Writer(scpi_t * context) {
    scpi_printf(context, "x\\n");
}
static scpi_result_t CbPH(scpi_t * context) {
    (Writer)(context);
    return SCPI_RES_OK;
}
static scpi_result_t CbPS(scpi_t * context) {
    ((*Writer))(context);
    return SCPI_RES_OK;
}
''', expect=["SYST:PH", "SYST:PS"]),

    _r3("param-pointer-unnamed",
        "a function-pointer PARAMETER whose caller passes something this "
        "graph cannot name is REFUSED. R2: `fn(context)` a leaf, PASS",
        '    {.pattern = "SYST:FWD", .callback = CbFwd,},',
        '''
static void Run(scpi_t * context, void (*fn)(scpi_t *)) {
    fn(context);
}
static scpi_result_t CbFwd(scpi_t * context) {
    Run(context, gHooks[0]);
    return SCPI_RES_OK;
}
''', problem="calls through parameter 'fn'"),

    _r3("macro-sink",
        "a MACRO wrapping the sink, handed the context, is reported. R2: "
        "directives are blanked, so `REPLY(` was a silent leaf, PASS",
        '    {.pattern = "SYST:MAC", .callback = CbMac,},',
        '''
#define REPLY(c, s) SCPI_ResultText((c), (s))
static scpi_result_t CbMac(scpi_t * context) {
    REPLY(context, "ok");
    return SCPI_RES_OK;
}
''', expect=["SYST:MAC"]),

    # Attribute placements on the WRITER's definition. Leading, stacked and
    # inside-the-declarator are valid GCC (host gcc 13.3 and xc32-gcc v4.60,
    # -fsyntax-only, 2026-11-21); between-name-and-params and after-params
    # are rejected by both, and still must not be lost silently.
    _r3("attr-leading",
        "a LEADING attribute on a writer's definition is REFUSED by "
        "compiler_view. R2: the writer vanished from the graph, PASS",
        _REG_H, "__attribute__((noinline)) static void H(scpi_t * context)\n"
        + _HELPER_BODY, problem="grammar does not read"),
    _r3("attr-stacked",
        "two STACKED attributes on a writer's definition are REFUSED. R2: "
        "read as a function named `__attribute__`, PASS",
        _REG_H, "static void __attribute__((noinline)) __attribute__((unused))"
        " H(scpi_t * context)\n" + _HELPER_BODY,
        problem="grammar does not read"),
    _r3("attr-declarator",
        "an attribute INSIDE the declarator's parentheses is REFUSED. R2: "
        "the writer vanished, PASS",
        _REG_H, "static void (__attribute__((noinline)) H)(scpi_t * context)\n"
        + _HELPER_BODY, problem="grammar does not read"),
    _r3("attr-between",
        "an attribute between the name and the parameter list (GCC rejects "
        "it) is REFUSED. R2: read as `__attribute__`, PASS",
        _REG_H, "static void H __attribute__((noinline)) (scpi_t * context)\n"
        + _HELPER_BODY, problem="grammar does not read"),
    _r3("attr-trailing",
        "an attribute AFTER the parameter list (GCC rejects it) is read as "
        "H, and the write reported. R2: the same -- a regression pin",
        _REG_H, "static void H(scpi_t * context) __attribute__((noinline))\n"
        + _HELPER_BODY, expect=["SYST:ATTR"]),
    _r3("attr-own-line",
        "an attribute on its OWN line above the definition is read, and the "
        "write reported. R2: the same -- the control for the refusals",
        _REG_H, "__attribute__((noinline))\nstatic void H(scpi_t * context)\n"
        + _HELPER_BODY, expect=["SYST:ATTR"]),

    # ---- the declarations cannot mask a write ----
    _r3("external-masks-nothing",
        "a call site declared EXTERNAL whose callee DOES have a writing "
        "definition is still REPORTED, and the declaration flagged stale",
        '    {.pattern = "SYST:EXT", .callback = CbExt,},',
        '''
static void memset_like(scpi_t * context) {
    scpi_printf(context, "x\\n");
}
static scpi_result_t CbExt(scpi_t * context) {
    memset_like(context);
    return SCPI_RES_OK;
}
''', expect=["SYST:EXT"], problem="is no longer an unresolved",
        external={("services/SCPI/SCPIInterface.c", 11, "memset_like"):
                  "#1274"}),
    _r3("external-declared",
        "a context handed to a callee with NO source passes once its call "
        "site is declared (the toolchain-`memset` case)",
        '    {.pattern = "SYST:MS", .callback = CbMs,},',
        '''
static scpi_result_t CbMs(scpi_t * context) {
    memset(context, 0, 4);
    return SCPI_RES_OK;
}
''', external={("services/SCPI/SCPIInterface.c", 8, "memset"): "#1274"}),
    _r3("external-wrong-line",
        "...and a declaration for a DIFFERENT line covers nothing",
        '    {.pattern = "SYST:MS", .callback = CbMs,},',
        '''
static scpi_result_t CbMs(scpi_t * context) {
    memset(context, 0, 4);
    return SCPI_RES_OK;
}
''', problem="passes the context to memset()",
        external={("services/SCPI/SCPIInterface.c", 9, "memset"): "#1274"}),
    _r3("cut-masks-nothing",
        "declaring OUR OWN resolvable `->error(` member does not cut it: the "
        "write is still reported and the declaration refused",
        '    {.pattern = "SYST:ERR", .callback = CbErr,},',
        _IFACE_STRUCT + '''
static size_t ErrWriter(scpi_t * context, int16_t err) {
    scpi_printf(context, "**ERROR\\n");
    return 0;
}
static scpi_interface_t iface = { .error = ErrWriter, };
static scpi_result_t CbErr(scpi_t * context) {
    context->interface->error(context, 1);
    return SCPI_RES_OK;
}
''', expect=["SYST:ERR"], problem="IS resolvable",
        indirect={("services/SCPI/SCPIInterface.c", "CbErr", "error"):
                  "#1274"}),
    _r3("cut-not-a-dispatcher",
        "declaring a direct call to an ordinary libscpi WRITER does not cut "
        "it: only the parser entry points may be declared",
        '    {.pattern = "SYST:IDN", .callback = CbIdn,},',
        '''
static scpi_result_t CbIdn(scpi_t * context) {
    SCPI_CoreIdnQ(context);
    return SCPI_RES_OK;
}
''', files=_FAKE_LIBSCPI, expect=["SYST:IDN"],
        problem="may not be declared",
        indirect={("services/SCPI/SCPIInterface.c", "CbIdn",
                   "SCPI_CoreIdnQ"): "#1274"}),

    # ---- the two declared cuts the real tree needs, each with its control --
    _r3("cut-error-undeclared",
        "libscpi's `->error` route to our `**ERROR` writer is FOLLOWED when "
        "not declared -- the control for the row below",
        '    {.pattern = "SYST:SET", .callback = CbSet,},',
        _CUT_OURS, files=_FAKE_LIBSCPI_ERR, expect=["SYST:SET"]),
    _r3("cut-error-declared",
        "...and declared at the LIBSCPI call site, it is cut: the gate passes",
        '    {.pattern = "SYST:SET", .callback = CbSet,},',
        _CUT_OURS, files=_FAKE_LIBSCPI_ERR,
        indirect={("libraries/scpi/libscpi/src/error.c", "SCPI_ErrorEmit",
                   "error"): "#1274"}),
    _r3("cut-dispatcher-undeclared",
        "a transport's call into libscpi's parser, reached through a console "
        "registration, is FOLLOWED when not declared -- the control",
        '    {.pattern = "SYST:POW", .callback = CbPow,},',
        _CUT_OURS, files=_FAKE_LIBSCPI_ERR, expect=["SYST:POW"]),
    _r3("cut-dispatcher-declared",
        "...and declared, it is cut: the gate passes",
        '    {.pattern = "SYST:POW", .callback = CbPow,},',
        _CUT_OURS, files=_FAKE_LIBSCPI_ERR,
        indirect={("services/SCPI/SCPIInterface.c", "CommandComplete",
                   "SCPI_Input"): "#1274"}),
]


# ---- round 4 (the r3 audit's misses, and the INVERSION's generalisation) --
# Every row is red on 74131e64 (the r3 head) unless its `why` says CONTROL;
# a control must PASS on both. The audit's exact constructs come first, then
# one construct per class that the audit did NOT report -- the proof that
# the rule refuses a class, not a list.
_AUDIT_REG = '    {.pattern = "SYST:AUDIT", .callback = CbAudit,},'
_WRITER = '''
static void Writer(scpi_t * context) {
    scpi_printf(context, "ack\\n");
}
static void Quiet(scpi_t * context) {
    (void)context;
}
'''
_HOOKS = _WRITER + '''
static void (*hooks[])(scpi_t *) = { Writer };
'''
_HOOK = _WRITER + '''
static void (*hook)(scpi_t *) = Writer;
'''


def _cb(body):
    return ('static scpi_result_t CbAudit(scpi_t * context) {\n%s\n'
            '    return SCPI_RES_OK;\n}\n' % body)


def _hook_struct(member, decls):
    return ("struct Hook { void (*%s)(scpi_t *); };\n" % member) + _WRITER \
        + decls


_SELFTEST_R4 = [
    # A: object-like macros, followed from a function body.
    _r3("r4-f0-macro-expr",
        "audit r3 finding 0: `AUDIT_ACK;` where the macro is an expression "
        "that writes. R3: bare macro names were followed only inside macros",
        _AUDIT_REG, '#define AUDIT_ACK scpi_printf(context, "ack\\n")\n'
        + _cb("    AUDIT_ACK;"), expect=["SYST:AUDIT"]),
    _r3("r4-f5-macro-stmt",
        "audit r3 finding 5: a `do { } while (0)` statement macro that writes",
        _AUDIT_REG, '#define REPLY do { scpi_printf(context, "unexpected\\n");'
        ' } while (0)\n' + _cb("    REPLY;"), expect=["SYST:AUDIT"]),
    _r3("r4-macro-of-macro",
        "NEW: a bare macro wrapping a bare macro that writes, followed "
        "transitively",
        _AUDIT_REG, '#define INNER scpi_printf(context, "x\\n")\n'
        '#define OUTER INNER\n' + _cb("    OUTER;"), expect=["SYST:AUDIT"]),
    _r3("r4-macro-of-macro-ctx",
        "NEW: a macro wrapping a macro that IS the context, handed to an "
        "unnamed pointer -- the context is followed through both expansions",
        _AUDIT_REG, _HOOK + '#define CTX context\n#define FIRE hook(CTX)\n'
        + _cb("    FIRE;"), problem="calls through 'hook' with the context"),
    _r3("r4-macro-control",
        "CONTROL: bare macros that are constants or flow (`LIMIT`, `DONE`) "
        "are followed and the gate still passes",
        _AUDIT_REG, '#define LIMIT 5\n#define DONE return SCPI_RES_OK\n'
        'static scpi_result_t CbAudit(scpi_t * context) {\n'
        '    int n = LIMIT;\n    (void)n;\n    DONE;\n}\n'),

    # B: computed callees.
    _r3("r4-f1-hooks0",
        "audit r3 finding 1: `hooks[0](context)` through an initialised "
        "array. R3: invisible to `_calls` and to the refusal",
        _AUDIT_REG, _HOOKS + _cb("    hooks[0](context);"),
        problem="computed callee 'hooks[0]'"),
    _r3("r4-f6-hooks0",
        "audit r3 finding 6 (blind duplicate of 1), its own text",
        _AUDIT_REG, _HOOKS.replace('"ack', '"unexpected')
        + _cb("    hooks[0](context);"), problem="computed callee 'hooks[0]'"),
    _r3("r4-hooks-i",
        "NEW: `hooks[i](context)` with `i` a variable",
        _AUDIT_REG, _HOOKS + _cb("    int i = 0;\n    hooks[i](context);"),
        problem="computed callee 'hooks[i]'"),
    _r3("r4-call-result",
        "NEW: `get()(context)` -- a callee that is a call's result",
        _AUDIT_REG, _WRITER + 'typedef void (*hook_fn)(scpi_t *);\n'
        'static hook_fn get(void) {\n    return Writer;\n}\n'
        + _cb("    get()(context);"), problem="computed callee 'get()'"),
    _r3("r4-computed-noarg",
        "NEW: `hooks[0]()` with NO argument, whose target writes through "
        "the GLOBAL context",
        _AUDIT_REG, 'static void GWriter(void) {\n'
        '    scpi_printf(&gClient.scpiContext, "ack\\n");\n}\n'
        'static void (*hooks[])(void) = { GWriter };\n'
        + _cb("    hooks[0]();"), problem="computed callee 'hooks[0]'"),
    _r3("r4-computed-control",
        "CONTROL: `(void)(context)`, `(void)(USB_EVT *)(context)` (a type "
        "this graph does not index) and `if (context) (void)Quiet(context)` "
        "are casts and a statement, not computed calls -- the gate passes",
        _AUDIT_REG, _WRITER + _cb("    (void)(context);\n"
                                  "    (void)(USB_EVT *)(context);\n"
                                  "    if (context) (void)Quiet(context);")),

    # C: a member is resolved only when EVERY value it can hold is named.
    _r3("r4-f2-run",
        "audit r3 finding 2: `{ .run = Quiet }` elsewhere certified "
        "`loud.run`, where `loud = { Writer }` positionally",
        _AUDIT_REG, _hook_struct("run", "static struct Hook quiet = "
                                 "{ .run = Quiet };\nstatic struct Hook loud ="
                                 " { Writer };\n")
        + _cb("    loud.run(context);"), problem="calls through member 'run'"),
    _r3("r4-f7-cb",
        "audit r3 finding 7: the `.cb` variant (`safe`/`active`)",
        _AUDIT_REG, _hook_struct("cb", "static struct Hook safe = "
                                 "{ .cb = Quiet };\nstatic struct Hook active ="
                                 " { Writer };\n")
        + _cb("    active.cb(context);"), problem="calls through member 'cb'"),
    _r3("r4-member-nested",
        "NEW: the positional value hidden one level down, in an EMBEDDING "
        "struct's designated initialiser: `.h = { Writer }`",
        _AUDIT_REG, _hook_struct("cb", "static struct Hook safe = "
                                 "{ .cb = Quiet };\nstruct Outer { struct Hook "
                                 "h; };\nstatic struct Outer o = { .h = "
                                 "{ Writer } };\n")
        + _cb("    o.h.cb(context);"), problem="calls through member 'cb'"),
    _r3("r4-member-store",
        "NEW: a runtime store of an unnamed value, `safe.cb = hooks[0]`",
        _AUDIT_REG, _hook_struct("cb", "static void (*hooks[])(scpi_t *) = "
                                 "{ Writer };\nstatic struct Hook safe = "
                                 "{ .cb = Quiet };\n")
        + _cb("    safe.cb = hooks[0];\n    safe.cb(context);"),
        problem="calls through member 'cb'"),
    _r3("r4-member-addr",
        "NEW: the member's ADDRESS taken, then stored through: `pp = "
        "&safe.cb; *pp = hooks[0];`",
        _AUDIT_REG, _hook_struct("cb", "static void (*hooks[])(scpi_t *) = "
                                 "{ Writer };\nstatic struct Hook safe = "
                                 "{ .cb = Quiet };\n")
        + _cb("    void (**pp)(scpi_t *) = &safe.cb;\n    *pp = hooks[0];\n"
              "    safe.cb(context);"),
        problem="calls through member 'cb'"),
    _r3("r4-member-control",
        "CONTROL: every initialiser designated and naming a silent "
        "function -- the member is complete, followed, and the gate passes",
        _AUDIT_REG, _hook_struct("cb", "static struct Hook safe = "
                                 "{ .cb = Quiet };\nstatic struct Hook active ="
                                 " { .cb = Quiet };\n")
        + _cb("    active.cb(context);")),

    # D: aliases of the context.
    _r3("r4-f3-alias",
        "audit r3 finding 3: `void *alias = context; hook(alias);` with a "
        "file-scope `hook = Writer`",
        _AUDIT_REG, _HOOK + _cb("    void *alias = context;\n"
                                "    hook(alias);"),
        problem="calls through 'hook' with the context"),
    _r3("r4-f8-alias-array",
        "audit r3 finding 8: `void *alias = context; void (*fp)(scpi_t *) = "
        "hooks[0]; fp(alias);`",
        _AUDIT_REG, _HOOKS + _cb("    void *alias = context;\n"
                                 "    void (*fp)(scpi_t *) = hooks[0];\n"
                                 "    fp(alias);"),
        problem="calls through 'fp' with the context"),
    _r3("r4-alias-of-alias",
        "NEW: an aliased alias, assigned in the 'wrong' order -- `c2 = c1; "
        "c1 = context; hook(c2);`",
        _AUDIT_REG, _HOOK + _cb("    void *c1 = 0;\n    void *c2;\n"
                                "    c2 = c1;\n    c1 = context;\n"
                                "    hook(c2);"),
        problem="calls through 'hook' with the context"),
    _r3("r4-alias-control",
        "CONTROL: an alias handed to a NAMED silent function, and data read "
        "through the context handed to a sourceless one, both pass",
        _AUDIT_REG, _WRITER + _cb("    void *alias = context;\n"
                                  "    int n = context->idx;\n"
                                  "    Quiet(alias);\n    memset(&n, 0, 4);")),
]


def _ck(why, got, want):
    if got == want:
        return []
    return ["%s: got %r, want %r" % (why, got, want)]


def _write_tree(td, source, files=None):
    """A synthetic firmware/src: the table file, the SD stub, the header
    fixture, plus `files`. -> the table file's path."""
    d = os.path.join(td, "services", "SCPI")
    os.makedirs(d)
    scpi_c = os.path.join(d, "SCPIInterface.c")
    with open(scpi_c, "w") as fh:
        fh.write(source)
    # _require_scan_set insists on both real SCPI files; the synthetic
    # fixture supplies the second as an empty stub.
    with open(os.path.join(d, "SCPIStorageSD.c"), "w") as fh:
        fh.write("/* stub */\n")
    with open(os.path.join(d, "SCPIInterface.h"), "w") as fh:
        fh.write(_CASE_HEADER)
    for rel, text in (files or {}).items():
        p = os.path.join(td, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w") as fh:
            fh.write(text)
    return scpi_c


def run_r3_row(row, analyse_fn=None, evaluate_fn=None):
    """-> (finding patterns, gate problems) for one round-3 row, judged by
    the same `evaluate` the real gate uses. Exposed so a row can be run
    against another revision's `analyse` (the r2 proof)."""
    import tempfile
    analyse_fn = analyse_fn or analyse
    evaluate_fn = evaluate_fn or evaluate
    with tempfile.TemporaryDirectory() as td:
        scpi_c = _write_tree(td, row["source"], row["files"])
        findings, _, refusals, stats = analyse_fn(
            td, scpi_c, frozenset(row["indirect"]))
        problems = evaluate_fn(findings, refusals, stats, {},
                               row["indirect"], {}, row["external"],
                               {"indirect": "indirect.txt",
                                "unresolved": "unresolved.txt",
                                "external": "external.txt"})
    return sorted(p for p, _, _ in findings), problems


def check_r3_row(row, patterns, problems):
    """-> [failure] for one round-3 row's outcome."""
    bad = _ck("%s: findings" % row["id"], patterns, sorted(row["expect"]))
    expected_new = ["%s (" % p for p in row["expect"]]
    rest = [p for p in problems
            if not any(p.startswith(e) for e in expected_new)]
    if row["problem"] is None:
        if rest:
            bad.append("%s: the gate must PASS but reported %r"
                       % (row["id"], rest))
    elif not any(row["problem"] in p for p in problems):
        bad.append("%s: no problem mentions %r; got %r"
                   % (row["id"], row["problem"], problems))
    return bad


def self_test():
    import tempfile
    failures, checks = [], 0

    for why, source, expect in _SELFTEST:
        with tempfile.TemporaryDirectory() as td:
            scpi_c = _write_tree(td, source)
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

    for row in _SELFTEST_R3 + _SELFTEST_R4:
        checks += 1
        failures.extend(check_r3_row(row, *run_r3_row(row)))

    # Audit r1, finding 0/4: a registered non-query callback DEFINED NOWHERE
    # in the scan set must be reported as unanalysed, never skipped.
    with tempfile.TemporaryDirectory() as td:
        scpi_c = _write_tree(
            td, _TABLE % '    {.pattern = "SYST:GONE", .callback = CbElsewhere,},')
        findings, _, _, stats = analyse(td, scpi_c)
        checks += 2
        failures.extend(_ck("a callback defined nowhere is reported unresolved",
                            stats.get("unresolved"), [("SYST:GONE", "CbElsewhere")]))
        failures.extend(_ck("...and is NOT reported as a silent pass",
                            sorted(p for p, _, _ in findings), []))

    # The same, but as a QUERY: a '?' callback we cannot analyse is not this
    # gate's business, so it must NOT be reported.
    with tempfile.TemporaryDirectory() as td:
        scpi_c = _write_tree(
            td, _TABLE % '    {.pattern = "SYST:GONE?", .callback = CbElsewhere,},')
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

    # r4 (audit r3 finding 4): a callback that is an EXPRESSION, not a bare
    # identifier, must exit -- `AUDIT_ENABLE ? AuditWriter : AuditQuiet`
    # used to parse as `AUDIT_ENABLE` and satisfy the completeness guard.
    for why, cb in (("a ternary callback", "AUDIT_ENABLE ? AuditWriter : AuditQuiet"),
                    ("a parenthesised-call callback", "Pick(1)"),
                    ("a cast callback", "(cb_t)Writer")):
        with tempfile.TemporaryDirectory() as td:
            d = os.path.join(td, "services", "SCPI")
            os.makedirs(d)
            scpi_c = os.path.join(d, "SCPIInterface.c")
            with open(scpi_c, "w") as fh:
                fh.write('const scpi_command_t c[] = {\n'
                         '    {.pattern = "SYST:AUDIT", .callback = %s,},\n'
                         '    {.pattern = NULL, .callback = NULL,},\n};\n' % cb)
            checks += 1
            try:
                registered(scpi_c)
                failures.append("%s must exit, not parse as a prefix" % why)
            except SystemExit:
                pass
    # ...and the anchored regex still accepts the three legal endings.
    for end in (",", " ,", "}", "\n,"):
        with tempfile.TemporaryDirectory() as td:
            d = os.path.join(td, "services", "SCPI")
            os.makedirs(d)
            scpi_c = os.path.join(d, "SCPIInterface.c")
            with open(scpi_c, "w") as fh:
                fh.write('const scpi_command_t c[] = {\n'
                         '    {.pattern = "SYST:A", .callback = CbA%s}\n'
                         '    {.pattern = NULL, .callback = NULL,},\n};\n'
                         % (end if end != "}" else "}" + ","))
            checks += 1
            try:
                failures.extend(_ck("anchored form %r still parses" % end,
                                    registered(scpi_c), [("SYST:A", "CbA")]))
            except SystemExit:
                failures.append("anchored form %r must parse" % end)

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
        p = os.path.join(td, "external.txt")
        with open(p, "w") as fh:
            fh.write("a/b.c:12\tmemset\t#1274\na/b.c\tmemset\t#1274\n")
        ext, problems = read_external(p)
        checks += 2
        failures.extend(_ck("a well-formed external line is read",
                            ("a/b.c", 12, "memset") in ext, True))
        failures.extend(_ck("an external line without a line number is a "
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

    # The callee normaliser, directly: a cast must stay a cast.
    checks += 3
    is_type = lambda n: n in ("uint32_t", "MyType")   # noqa: E731
    failures.extend(_ck("`(f)(x)` becomes a call of f",
                        normalise_callees("(f)(x);", is_type).split(),
                        ["f", "(x);"]))
    failures.extend(_ck("`(uint32_t)(x)` stays a cast",
                        normalise_callees("y = (uint32_t)(x);", is_type),
                        "y = (uint32_t)(x);"))
    failures.extend(_ck("`g(a)(b)`'s argument list is not unwrapped",
                        normalise_callees("g(a)(b);", is_type), "g(a)(b);"))

    for msg in failures:
        print("  - %s" % msg)
    print("scpi_nonquery_write self-test: %s (%d failure(s); %d check(s))"
          % ("all cases pass" if not failures else "FAILED",
             len(failures), checks))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
