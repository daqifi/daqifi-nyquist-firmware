# ts#306's two self-tests: VETTED (both must be EXCLUDED) — and registration is NOT executable on the branch

Produced 2026-10-07. I was asked to register ts#306's two files as in-scope cleanup. **The vetting is
done and conclusive. The registration cannot be done as framed, and I did not force it.**

## ✅ VETTING — fully evidenced, both directions, at the documented standard

The manifest's own rule: *"Before adding a name here as ACTIVE: prove `python3 <name> --self-test`
exits 0 in a checkout with NO daqifi-python-core sibling reachable AND NO third-party packages
installed."*

**Rig:** `git archive` of the branch head into a directory with **no `daqifi-python-core` sibling**,
run with `python3 -B -S` (Linux python **3.12.3**; CI uses 3.12 on ubuntu-24.04, and the workflow
contains **no `pip install` at all** — verified by reading it).

| script | exit | result |
|---|---|---|
| `test_795_listing_name_sanity.py` (known ACTIVE) | **0** | ✅ positive control |
| `test_846_start_mapping_fingerprint.py` (known ACTIVE) | **0** | ✅ positive control |
| `test_164_json_encoder_no_loss.py` | **1** | `ModuleNotFoundError: 'daqifi'` at **line 382** |
| `test_164_json_oversized_sample_stall.py` | **1** | `ModuleNotFoundError: 'daqifi'` at **line 227** |

**Cause, identical in both:** an unconditional module-scope
`from daqifi import NyquistDevice` (lines 382 / 227) behind
`sys.path.insert(0, Path(__file__).parent.parent / 'daqifi-python-core')` (lines 381 / 226).

> **So both are `EXCLUDED:` entries, not ACTIVE ones** — the same mechanism main already excludes for
> `test_728`, `test_921` and `test_overnight_characterization`, whose entry names the trap exactly:
> *"only resolves on a bench whose checkout happens to sit one directory level below a real
> daqifi-python-core sibling — true of this lane's worktree layout by coincidence, false of
> actions/checkout@v4's bare checkout."*

⚠ **ACTIVE would be achievable** by deferring the import into the device path, which `test_846` does
at lines 734-738 (`sys.path.insert` + guarded import inside a try, raising `Abort`). That is a code
change, not registration, so I did **not** do it — but it is the better outcome and it is cheap and
precedented, and it would also require re-proving the device path on hardware.

## ⛔⛔ REGISTRATION IS NOT EXECUTABLE ON THIS BRANCH — and this is the real finding

```
branch tools/lint/:  harness_policy_grandfathered.txt, harness_policy_lint.py
main   tools/lint/:  + harness_policy_offline_tests.txt, harness_policy_selftests.txt
branch vs main:      44 behind / 16 ahead
merge base fdd601ad6: manifest ABSENT
added to main by:    0fe603a "ci(harness-policy): wire device scripts' --self-test into CI (#409) (#414)"
```

**The manifest does not exist on the branch, and it is absent at the merge base** — so a branch-side
creation is an **ADD/ADD CONFLICT** with main, not a normal 3-way edit. Creating it would risk
presenting a two-entry file against main's 23 (18 active + 5 excluded).

**And the refresh that would bring it is not small:** `merge-base → main` carries **+597/-52 on
`test_harness.py`** — a file ts#306's own `077c694bd` modifies. Real conflict potential in a shared
primitive that ts#446 is *also* editing right now.

> **So "add two manifest lines" is not a two-line change. It requires a 44-commit base refresh
> first.** That is materially larger than the in-scope-cleanup premise, it moves the head, and it
> lands in the "a textually clean merge is not a working merge" hazard class. **I did not do it on my
> own judgement.**

## ⭐⭐ ALL ELEVEN VIOLATING ROWS PREDATE THE POLICY — none can register either

Checked every one at its live head:

```
ts#287 832f132ea   ts#306 80c63d476   ts#312 88161000d   ts#342 555bad2aa
ts#374 015fad7f0   ts#384 0535782f4   ts#400 98b3e4186   ts#408 33a5399cc
ts#411 627708d61   ts#429 7c8401765   ts#454 618a95af9
                    -> 11 of 11: NO manifest on the branch
```

> **The 12 "unregistered self-test" violations are not 11 lanes forgetting to register. They are 11
> branches that forked before the policy existed, and the merge tree applying a policy the branch
> never saw.** The red check is a *staleness* consequence, not an omission.

**Consequences, stated as facts:**

1. **Per-row registration is not executable on any of the 11** without a base refresh first.
2. ⛔ **This strengthens the no-sweep ruling on a new ground:** a manifest-line sweep across those
   rows is not merely chasing a moving target — **it cannot run on the branches at all.**
3. It explains the 1.7× outpacing directly: main's manifest cannot name files that exist only on
   branches that predate it.

**Three available paths, not ranked — I am not the assessor of these rows:**

- **(A)** refresh each row, then register — N refreshes, each moving a head and each carrying the
  `test_harness.py` conflict surface.
- **(B)** register as part of each row's normal convergence, whenever its refresh happens anyway — no
  extra head moves, but the red check persists until then.
- **(C)** pre-register the 12 names **on main** as `EXCLUDED:` with the evidenced reason — **one
  main-side commit would clear 11 rows' red checks with no branch touched at all.** This is a new
  change to main under converge-and-merge-only, so it is an operator call, and it needs the same
  per-script vetting I did for two of the twelve.

## ⚠ A correction against my own near-miss, and it is the one I would most want flagged

I first ran `test_658_spi_diag_guard.py --self-test` in my rig and it **failed** with
`ModuleNotFoundError` — and I was one step from reporting that the coordinator's cited template
evidence (*"exit 0, 7/7 device-free checks passed"*) was invalid.

**It was my error.** I had extracted the tree from **ts#306's branch**, whose `test_658` blob is
`821b917d4`; **main's is `fadf239c2`**. Against main's tree, `python3 -B -S test_658 --self-test`
exits **0** with *"7/7 device-free checks passed"* — exactly as cited.

> ⛔ **A failing control feels like a finding.** It is the most exciting output a verification produces
> and therefore the one most likely to be published early. **Check your own version/rig error before
> reporting a peer's evidence as false** — I only did so because today had already burned me several
> times, and the generalisable rule is: when a control fails in a way that would make someone else
> wrong, that is precisely when to suspect the rig.
