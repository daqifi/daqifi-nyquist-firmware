#!/usr/bin/env python3
"""#1227: Streaming_Start() must not clear the encoder buffer on the STOP path.

Streaming_UpdateState() is Stop-then-Start unconditionally, so Streaming_Start()
also runs with IsEnabled == false when a stream is being stopped. That call comes
from a task above streaming_Task's priority and can land after the encoder has
filled `buffer` but before Streaming_WriteWithRetry sends it; an unguarded
memset(buffer, 0, ...) then makes the final packet go out as zeros.

streaming.c is not host-compilable and the window is too narrow to hit over
SCPI, so this guards the PLACEMENT: every `memset(buffer` inside
Streaming_Start() must be nested in a block whose controlling condition tests
IsEnabled, and there must be at least one (a check over nothing must fail).

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
            if re.search(r'\bmemset\s*\(\s*buffer\b', stmt):
                found += 1
                guarded = any(re.search(r'\bIsEnabled\b', h) for h in stack) \
                    or re.search(r'\bIsEnabled\b', stmt)
                if not guarded:
                    problems.append('memset(buffer ...) runs on the stop path '
                                    '(not under an IsEnabled condition)')
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
         '  if (!Running) {\n    if (cfg->IsEnabled) {\n      memset(buffer, 0, n);\n    }\n  }\n}\n', True),
        ('guarded in the same condition (the fix)', head +
         '  if (!Running) {\n    if (cfg->IsEnabled && buffer != NULL) {\n'
         '      memset(buffer, 0, n);\n    }\n  }\n}\n', True),
        ('IsEnabled only in a comment', head +
         '  if (!Running) {\n    /* if (cfg->IsEnabled) */\n    memset(buffer, 0, n);\n  }\n}\n', False),
        ('IsEnabled guard closed before the memset', head +
         '  if (!Running) {\n    if (cfg->IsEnabled) { x(); }\n    memset(buffer, 0, n);\n  }\n}\n', False),
        ('no memset at all', head + '  if (!Running) { x(); }\n}\n', False),
        ('function absent', 'int main(void){return 0;}\n', False),
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
