#!/usr/bin/env python3
"""Prove log_budget.py's self-test can FAIL -- one mutant per guard.

A self-test that passes tells you nothing on its own: the rows asserting "this
form is refused" would read identically if the refusal had been deleted. So
each guard added to read_ceiling()/find_annotation() gets a mutant here that
neuters exactly it, and this harness asserts the EXACT number of self-test
rows that must go red -- not merely "something failed". A mutant that breaks
more rows than named means the guard is load-bearing somewhere undeclared; one
that breaks fewer means a row is vacuous.

Three properties are checked for every mutant, because each has been wrong
here at least once:

  1. the ANCHOR occurs exactly once, so a mutant cannot silently not apply
     (a `replace(x, y, 1)` on a string occurring four times hit the wrong
     site in an earlier round and the mutant was inert);
  2. the mutant turns exactly N rows red, N stated per row below;
  3. RESTORE returns the suite to green -- which is how an over-scoped guard
     was found, the mutant having passed while the restore did not.

Run from the repo root:

    python3 tools/lint/log_budget_mutate.py

Exit 0 = every mutant behaved as declared. The source file is restored on
every path, including an exception.
"""

import pathlib
import subprocess
import sys

TARGET = pathlib.Path("tools/lint/log_budget.py")

# find_annotation's termination predicate, before and after r16/3. Spelled out
# rather than described, so the mutant restores the ACTUAL old behaviour.
NEW_PRED = """            terminated = (closed
                          or prose_after_value
                          or not annotation_value_can_continue(src_lines, t_idx))"""
OLD_PRED = """            terminated = (("*/" in src_lines[t_idx].split("log_budget:", 1)[-1])
                          or ("//" in src_lines[t_idx].split("log_budget:")[0])
                          or prose_after_value
                          or not annotation_value_can_continue(src_lines, t_idx))"""

# (label, anchor, replacement, how many self-test rows must go red)
MUTANTS = [
    ("r16/1 neuter the Logger.c redefinition scan",
     "        if shadow:", "        if False:", 6),
    ("r16/2 neuter the clamp's second-argument check",
     "    if second != formatted:",
     "    if False and second != formatted:", 3),
    ("r16/3 restore the old raw-line termination predicate",
     NEW_PRED, OLD_PRED, 1),
    ("r16/4 stop requiring the buffer be sized by LOG_MESSAGE_SIZE",
     r'r"\s*\[\s*LOG_MESSAGE_SIZE\s*\]"', r'r"\s*\["', 2),
    ("r16/5a stop validating what the min macro MEANS",
     '    if (params, body) != ("x,y", "(((x)<=(y))?(x):(y))"):',
     '    if False and (params, body) != ("x,y", "(((x)<=(y))?(x):(y))"):', 1),
    ("r16/5b stop requiring exactly one min definition",
     "    if len(min_defs) != 1:",
     "    if False and len(min_defs) != 1:", 2),
]


def run_self_test():
    """(exit code, the self-test's own FAIL lines)."""
    r = subprocess.run([sys.executable, str(TARGET), "--self-test"],
                       capture_output=True, text=True)
    fails = [ln.strip() for ln in (r.stdout + r.stderr).splitlines()
             if ln.strip().startswith("FAIL")]
    return r.returncode, fails


def main():
    if not TARGET.exists():
        print(f"::error::{TARGET} not found -- run from the repo root")
        return 2
    original = TARGET.read_bytes()
    text = original.decode("utf-8")

    rc, fails = run_self_test()
    print(f"BASELINE: exit={rc}, {len(fails)} failing row(s)")
    if rc != 0 or fails:
        print("::error::baseline is not green; a mutation result would mean "
              "nothing against it")
        return 1

    bad = 0
    try:
        for label, anchor, replacement, want in MUTANTS:
            n = text.count(anchor)
            if n != 1:
                print(f"  [{label}] ::error:: anchor occurs {n} times -- the "
                      f"mutant was NOT APPLIED, so this row proves nothing")
                bad += 1
                continue
            TARGET.write_bytes(
                text.replace(anchor, replacement, 1).encode("utf-8"))
            rc, fails = run_self_test()
            ok = rc != 0 and len(fails) == want
            print(f"  [{label}] exit={rc}, {len(fails)} failing row(s), "
                  f"expected exactly {want} -- {'OK' if ok else 'MISMATCH'}")
            for f in fails:
                print(f"      {f[:160]}")
            if not ok:
                bad += 1
            TARGET.write_bytes(original)
            rc2, fails2 = run_self_test()
            if rc2 != 0 or fails2:
                print(f"      ::error:: RESTORE did not return to green "
                      f"(exit={rc2}, {len(fails2)} failing) -- the guard is "
                      f"over-scoped and fails on correct source")
                bad += 1
    finally:
        TARGET.write_bytes(original)

    rc, fails = run_self_test()
    print(f"FINAL: exit={rc}, {len(fails)} failing row(s), "
          f"{bad} mismatch(es)")
    if rc != 0 or fails:
        print("::error::the source was not restored cleanly")
        return 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
