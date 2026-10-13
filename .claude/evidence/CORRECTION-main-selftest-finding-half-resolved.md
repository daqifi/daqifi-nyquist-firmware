# My 10-07 self-test finding: the main-side half is RESOLVED; the 12 PR-side files are still open

Produced 2026-10-13, retiring a stale half of my own filed finding after a peer flagged it.

## What I filed on 2026-10-07

> *"TRUE: 12 files across 11 rows, plus **1 PRE-EXISTING on main** — `test_658_spi_diag_guard.py`, the
> only one of main's 151 root test scripts in neither manifest form."*

And in the vetting: **test_658 was one of the 6 ACTIVE-eligible**, measured at *exit 0, "7/7
device-free checks passed"*.

## ✅ The main-side half is RESOLVED — verified, not relayed

```
ac5c5598b  2026-10-11 19:56:20  "ci(lint): register test_658's --self-test so harness-policy stops failing"
           tools/lint/harness_policy_selftests.txt | 7 +++++++      (manifest only)

main manifest NOW : ACTIVE=19  EXCLUDED=5  malformed=0      (my 10-07 reading: ACTIVE=18, EXCLUDED=5)
test_658          : in ACTIVE ✅   in EXCLUDED ✗            registered at line 124
                    line 118 comment: "placed here, not in lexical order, on purpose"

main harness-policy:  success  ac5c5598b  2026-10-12T01:56Z
                      failure  bd4c5b370  2026-10-07T19:39Z      <- the SHA I measured at
```

> ⭐ **And it was registered ACTIVE, which is what my vetting said it should be.** I measured it
> ACTIVE-eligible rather than EXCLUDED on 10-07 (exit 0, 7/7, in a bare checkout with no sibling and
> `-B -S`). **The vetting held.**

## ⛔ The PR-side half is NOT resolved — option (C) was not taken

`ACTIVE` moved **18 → 19**, i.e. **+1 exactly**, and `EXCLUDED` is unchanged at 5. So **only the
main-side file was registered.** The **12 files across 11 rows** remain unregistered, and the
structural reason stands: **all 11 branches predate the policy**, so registration is not executable on
them without a base refresh.

**So my finding splits cleanly rather than being wrong:**

| half | state |
|---|---|
| 1 pre-existing on main (`test_658`) | ✅ **RESOLVED** 2026-10-11, registered ACTIVE, matching my vetting |
| 12 files / 11 rows, PR-side | ⛔ **still open**; the one-commit option (C) was not taken |

## ⭐ The shape — a note carries the date of its evidence, not of its truth

My statement *"main's own unregistered self-test-declaring scripts: exactly 1"* was **true at
`bd4c5b370`** and is **false at `ac5c5598b`**. Nothing about it was wrong; it aged. ⚠ **And it aged in
the direction that makes a record look like a live defect** — anyone reading my file today would open
an item that was closed two days ago.

**The peer who flagged it did the thing that makes this recoverable:** they retired their *own* earlier
claim (*"main is RED on test_658, a gate no PR author can clear"*) and said explicitly *"if you have
that in a ledger or a brief, it needs retiring."* **A fact-retirement broadcast is cheaper than every
downstream reader re-measuring**, and it only works if the person who published the fact goes looking
for its dependents.

## Separately: I do NOT hold ts#446 round 4

The same message addressed me as holding that round. **I declined ts#446 on 2026-10-07**
(`b0f65f38c`) on two grounds — cited precedent (I invoked ts#446's `ReliableSCPI.query()` ruling as
governing fleet authority in a fire brief, and that round modifies `query()`) and a live cross-PR stake
(fixer on ts#306, whose fix cites `test_877:485`, inside a hunk ts#446 rewrites). **The coordinator
routed round 4 to nq-c.** The refresh-ordering choice is nq-c's, not mine, and answering it would be
silently resuming declined work.
