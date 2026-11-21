#!/usr/bin/env python3
"""#1227: Streaming_Start() must not clear the encoder buffer on the STOP path.

Streaming_UpdateState() is Stop-then-Start unconditionally, so Streaming_Start()
also runs with IsEnabled == false when a stream is being stopped. That call comes
from a task above streaming_Task's priority and can land after the encoder has
filled `buffer` but before Streaming_WriteWithRetry sends it; an unguarded
memset(buffer, 0, ...) then makes the final packet go out as zeros.

streaming.c is not host-compilable and the window is too narrow to hit over
SCPI, so this guards the PLACEMENT: every `memset(buffer` inside
Streaming_Start() must be reachable ONLY when IsEnabled is true, and there must
be at least one (a check over nothing must fail).

"Reachable only when IsEnabled is true" is enforced strictly: some enclosing
`if (COND)` (a plain `if`, braced or braceless) must have a COND that is a
top-level `&&` conjunction, with no top-level `||` or `?:`, one conjunct of
which is exactly `gpRuntimeConfigStream->IsEnabled` (optionally `== true`,
`== 1`, `!= false`, `!= 0`; redundant outer parens allowed), NOT negated. A
memset with any `else` / `else if` ancestor, or as an `else` statement, is
refused. Merely MENTIONING IsEnabled does not count: `!IsEnabled && x` and
`IsEnabled || x` both still clear on STOP.
Deliberate bias: an unusual-but-correct shape (guard through a helper, an early
`return` on !IsEnabled, a local copy of the flag, a memset in the else of a
`!IsEnabled` test) is REFUSED rather than accepted -- rewrite it to the plain
`if (gpRuntimeConfigStream->IsEnabled && ...)` form or extend this checker.

Comments and string literals are stripped first: the prose around the fix names
every identifier checked here, so a raw search would pass on the comment alone.

Usage:  check_1227_stop_no_buffer_clear.py <streaming.c>
        check_1227_stop_no_buffer_clear.py --self-test
"""
import re
import sys

SIG = re.compile(r'^static void Streaming_Start\(void\)\s*\{', re.M)


def strip(src):
    """Blank comments, string and char literals, keeping length and newlines."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        two = src[i:i + 2]
        if two == '//':
            while i < n and src[i] != '\n':
                out.append(' ')
                i += 1
        elif two == '/*':
            end = src.find('*/', i + 2)
            end = n if end < 0 else end + 2
            out.append(''.join('\n' if ch == '\n' else ' ' for ch in src[i:end]))
            i = end
        elif c in '"\'':
            q = c
            out.append(q)
            i += 1
            while i < n and src[i] != q:
                if src[i] == '\\':
                    out.append(' ')
                    i += 1
                out.append(' ')
                i += 1
            out.append(q)
            i += 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def function_body(src):
    m = SIG.search(src)
    if not m:
        return None
    depth, i = 0, m.end() - 1
    for j in range(i, len(src)):
        if src[j] == '{':
            depth += 1
        elif src[j] == '}':
            depth -= 1
            if depth == 0:
                return src[i:j + 1]
    return None


ENABLED_CONJUNCT = re.compile(
    r'^gpRuntimeConfigStream\s*->\s*IsEnabled'
    r'(?:\s*(?:==\s*(?:true|1)|!=\s*(?:false|0)))?$')


def _unparen(t):
    """Strip whitespace and redundant outer parens that wrap the whole text."""
    t = t.strip()
    while t.startswith('(') and t.endswith(')'):
        depth = 0
        for k, c in enumerate(t):
            depth += (c == '(') - (c == ')')
            if depth == 0 and k < len(t) - 1:
                return t           # the first '(' closed early: not a wrapper
        t = t[1:-1].strip()
    return t


def _top_split(cond):
    """Split on top-level '&&'. Return None if a top-level '||' or '?' exists."""
    parts, depth, cur, i = [], 0, [], 0
    while i < len(cond):
        c = cond[i]
        two = cond[i:i + 2]
        if c in '([':
            depth += 1
        elif c in ')]':
            depth -= 1
        if depth == 0 and (two == '||' or c == '?'):
            return None
        if depth == 0 and two == '&&':
            parts.append(''.join(cur))
            cur = []
            i += 2
            continue
        cur.append(c)
        i += 1
    parts.append(''.join(cur))
    return parts


def _if_condition(header):
    """Condition text of a plain `if (COND)` header, else None (else/else-if/
    anything with trailing text after the closing paren)."""
    m = re.match(r'\s*if\s*\(', header)
    if not m:
        return None
    depth, start = 1, m.end()
    for k in range(start, len(header)):
        if header[k] == '(':
            depth += 1
        elif header[k] == ')':
            depth -= 1
            if depth == 0:
                if header[k + 1:].strip():
                    return None
                return header[start:k]
    return None


def _guards_enabled(header):
    """True if `header` is a plain if whose condition forces IsEnabled true."""
    cond = _if_condition(header)
    if cond is None:
        return False
    parts = _top_split(_unparen(cond))
    if parts is None:
        return False
    return any(ENABLED_CONJUNCT.match(_unparen(p)) for p in parts)


def _has_else(header):
    return re.match(r'\s*else\b', header) is not None


def check(src):
    """Return a list of problems; empty means the placement is correct."""
    body = function_body(strip(src))
    if body is None:
        return ['Streaming_Start(void) not found at column 0']
    problems, found = [], 0
    stack = []          # header text of each open block
    last = 0            # start of the text since the previous { } or ;
    for i, ch in enumerate(body):
        if ch == '{':
            stack.append(body[last:i])
            last = i + 1
        elif ch == '}':
            if stack:
                stack.pop()
            last = i + 1
        elif ch == ';':
            stmt = body[last:i]
            m = re.search(r'\bmemset\s*\(\s*buffer\b', stmt)
            if m:
                found += 1
                # A braceless `if (COND) memset(...)`: the statement text is
                # the header plus the memset; judge the part before it.
                inline = stmt[:m.start()]
                in_else = any(_has_else(h) for h in stack) or _has_else(inline)
                guarded = any(_guards_enabled(h) for h in stack) \
                    or _guards_enabled(inline)
                if in_else or not guarded:
                    problems.append('memset(buffer ...) is not provably reachable '
                                    'only when IsEnabled is true (needs an '
                                    'enclosing `if (... && gpRuntimeConfigStream->'
                                    'IsEnabled ...)`, not negated, not under '
                                    '||/else)')
            last = i + 1
    if found == 0:
        problems.append('no memset(buffer ...) found in Streaming_Start: a check '
                        'over nothing must fail, not pass')
    return problems


def self_test():
    head = 'static void Streaming_Start(void) {\n'
    cases = [
        ('unguarded (the defect)', head +
         '  if (!Running) {\n    if (buffer != NULL) memset(buffer, 0, n);\n  }\n}\n', False),
        ('guarded by an enclosing IsEnabled block', head +
         '  if (!Running) {\n    if (gpRuntimeConfigStream->IsEnabled) {\n      memset(buffer, 0, n);\n    }\n  }\n}\n', True),
        ('guarded in the same condition (the fix)', head +
         '  if (!Running) {\n    if (gpRuntimeConfigStream->IsEnabled && buffer != NULL) {\n'
         '      memset(buffer, 0, n);\n    }\n  }\n}\n', True),
        ('IsEnabled only in a comment', head +
         '  if (!Running) {\n    /* if (cfg->IsEnabled) */\n    memset(buffer, 0, n);\n  }\n}\n', False),
        ('IsEnabled guard closed before the memset', head +
         '  if (!Running) {\n    if (gpRuntimeConfigStream->IsEnabled) { x(); }\n    memset(buffer, 0, n);\n  }\n}\n', False),
        ('no memset at all', head + '  if (!Running) { x(); }\n}\n', False),
        ('function absent', 'int main(void){return 0;}\n', False),
        ('negated: !IsEnabled && x (still clears on STOP)', head +
         '  if (!gpRuntimeConfigStream->IsEnabled && buffer != NULL) {\n'
         '    memset(buffer, 0, n);\n  }\n}\n', False),
        ('disjunction: IsEnabled || x', head +
         '  if (gpRuntimeConfigStream->IsEnabled || buffer != NULL) {\n'
         '    memset(buffer, 0, n);\n  }\n}\n', False),
        ('negated with parens: !(IsEnabled)', head +
         '  if (!(gpRuntimeConfigStream->IsEnabled)) {\n'
         '    memset(buffer, 0, n);\n  }\n}\n', False),
        ('memset in the else of if (IsEnabled)', head +
         '  if (gpRuntimeConfigStream->IsEnabled) {\n    x();\n  } else {\n'
         '    memset(buffer, 0, n);\n  }\n}\n', False),
        ('braceless else memset', head +
         '  if (gpRuntimeConfigStream->IsEnabled) x();\n'
         '  else memset(buffer, 0, n);\n}\n', False),
        ('IsEnabled if is a sibling, not an ancestor', head +
         '  if (gpRuntimeConfigStream->IsEnabled) {\n    x();\n  }\n'
         '  if (buffer != NULL) {\n    memset(buffer, 0, n);\n  }\n}\n', False),
        ('IsEnabled of another object', head +
         '  if (other->IsEnabled && buffer != NULL) {\n'
         '    memset(buffer, 0, n);\n  }\n}\n', False),
        ('IsEnabled only inside a call argument', head +
         '  if (f(gpRuntimeConfigStream->IsEnabled) && buffer != NULL) {\n'
         '    memset(buffer, 0, n);\n  }\n}\n', False),
        ('|| inside the && chain', head +
         '  if (gpRuntimeConfigStream->IsEnabled && a || b) {\n'
         '    memset(buffer, 0, n);\n  }\n}\n', False),
        ('exact guard (the shipped form)', head +
         '  if (gpRuntimeConfigStream->IsEnabled && buffer != NULL) {\n'
         '    memset(buffer, 0, n);\n  }\n}\n', True),
        ('guard with == true, extra conjunct first, parens', head +
         '  if ((buffer != NULL) && (gpRuntimeConfigStream->IsEnabled == true)) {\n'
         '    memset(buffer, 0, n);\n  }\n}\n', True),
        ('braceless guarded', head +
         '  if (gpRuntimeConfigStream->IsEnabled && buffer != NULL)\n'
         '    memset(buffer, 0, n);\n}\n', True),
    ]
    rc = 0
    for name, src, want_ok in cases:
        ok = not check(src)
        flag = 'PASS' if ok == want_ok else 'FAIL'
        if ok != want_ok:
            rc = 1
        print('[%s] %s' % (flag, name))
    return rc


if __name__ == '__main__':
    if sys.argv[1:] == ['--self-test']:
        sys.exit(self_test())
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    with open(sys.argv[1], newline='') as f:
        probs = check(f.read().replace('\r\n', '\n'))
    if probs:
        for p in probs:
            print('ERROR: #1227 -- ' + p)
        sys.exit(1)
    print('OK: #1227 -- Streaming_Start clears the encoder buffer only when IsEnabled')
