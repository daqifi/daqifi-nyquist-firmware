# Layered Static Analysis — Evaluation & Plan (2026-07)

**Ticket:** [#415 — Upgrade static analysis: layered linting (cppcheck + XC32 MISRA + Coverity)](https://github.com/daqifi/daqifi-nyquist-firmware/issues/415)
**Status:** Evaluation / plan only. **No lint-config or CI changes land with this document** — those are scoped follow-ups gated on the decisions below.
**Scope:** Assess three ways to strengthen static analysis above today's cppcheck baseline gate:
(a) more cppcheck checks / the MISRA addon, (b) XC32's built-in MISRA-C support, (c) Coverity Scan.

---

## 1. What exists today

| Piece | Location | Behavior |
|---|---|---|
| cppcheck wrapper | `tools/lint/cppcheck.sh` | Runs cppcheck `--enable=warning,style,performance,portability --inconclusive` over `firmware/src/`, excluding `third_party/`, `libraries/`, `config/`. Output sorted `LC_ALL=C` for cross-runner determinism. |
| Baseline | `tools/lint/cppcheck-baseline.txt` | The accepted finding set. **Currently 0 lines** (the 2 style findings noted in CLAUDE.md have since been resolved; the file is empty). |
| Suppressions | `tools/lint/cppcheck-suppress.txt` | 2 documented false positives: `DioProbe.c` array-bounds (flow analysis can't see the bound), FreeRTOS `portmacro.h` FPU-guard `#error` (chip macro cppcheck doesn't define). |
| CI gate | `.github/workflows/cppcheck.yml` | Ubuntu 24.04, cppcheck **pinned to 2.13.0** (version-drift guard), fails the check if output differs from the committed baseline in either direction. Triggers on PRs touching `firmware/src/**`, `tools/lint/**`, or the workflow. |

**Cost today:** free, ~5 s per run, near-zero false-positive burden (baseline gate absorbs churn).

### 1.1 The motivating bug (why this ticket exists)

The ch15 regression (PR #354, bisected 2026-05-06) was a **structural change in how globals are aliased through function parameters, miscompiled at -O3** — rebuilding `ADC.c` at `-O1` made it disappear with no source change. It slipped past cppcheck, `-Werror -Wall`, manual review, and a symptom-mismatched bench test.

> **⚠️ Reality check on the "which tier would have caught #354" premise.** No **source-level** static analyzer (cppcheck, XC32 MISRA, or Coverity) models the XC32/GCC **-O3 optimizer's** aliasing decisions. They analyze C semantics, not the backend's optimization of them. What these tools *can* do is flag the *coding pattern* that made the code fragile — a cross-context shared global aliased/RMW'd without `volatile` or a critical section — i.e. raise a MISRA 8.13 / concurrency-style advisory on the *shape* of the code, not prove the miscompile. That advisory might have drawn a reviewer's eye to the alias, but "Coverity catches this class of bug" (ticket §Tier 3) is **overstated**: the actual root cause was fixed by a hardware `TRGSRC` register configuration (`96e7c840`), not by a volatile qualifier — and the CLAUDE.md `SET_ONCE_POINTER_AUDIT` found volatile on these pointers *redundant* at -O3 with no LTO. Treat "layered linting would have caught #354" as **hypothesized, not demonstrated.** The real value of layered linting is a broader net for the *general* UB/concurrency/portability class, not a guaranteed catch of this specific miscompile.

---

## 2. Option (a) — More cppcheck: extra checks + the MISRA addon

cppcheck ships addons (separate `--addon=` passes on top of the core engine):

| Addon | Catches | Notes |
|---|---|---|
| `misra` | MISRA C 2012 rule violations (a substantial subset — cppcheck implements ~130 of the 143 rules; the rest need dataflow it doesn't have). | **Rule *text* is copyrighted** and not distributed — without a local `misra_rules.txt` you get rule *numbers* (`[misra-c2012-8.13]`) but not the prose. Numbers are enough to gate + look up. |
| `cert` | CERT C secure-coding rules (overlaps MISRA). | Optional; lower priority than MISRA for this codebase. |
| `y2038` | 32-bit `time_t` overflow. | Low relevance (no wall-clock time math on the hot path). |
| `threadsafety` | Non-thread-safe libc (`strtok`, `localtime`, static-local returns). | Some relevance given FreeRTOS multi-task use. |

Also cheap to turn on in the core engine: `--enable=information` and raising `--check-level=exhaustive` (2.13 supports it) for deeper value-flow at higher CPU cost.

**What the MISRA addon adds beyond current cppcheck:** mandatory/required-rule coverage the base `warning,style` set skips — notably **Rule 8.13** (pointer-to-const correctness), **Rule 11.x** (pointer conversions / aliasing), **Rule 8.x** (declaration/linkage hygiene), **Rule 1.3** (undefined behavior), **Rule 10.x** (implicit conversions / essential type). This is the closest cheap approximation of "flag the alias/volatile shape" from §1.1.

- **Integration effort:** low. Add `--addon=misra` to `cppcheck.sh`, regenerate the baseline, land it. Same Ubuntu-pinned runner, same baseline-diff gate — no new infrastructure. The one wrinkle is deciding gating vs advisory (see §5).
- **False-positive / baseline burden:** **high on first run, then flat.** A firmware tree that was never written to MISRA will produce **hundreds to low-thousands** of violations (Directive 4.x, Rule 15.x single-exit, Rule 21.x banned-functions, Rule 10.x conversions dominate). The existing baseline mechanism absorbs this — but a multi-thousand-line baseline is only useful if the gate is "no *new* violations." Blanket-suppressing whole rule categories (e.g. 15.5 single-return, 15.6 mandatory braces if we don't want them) up front keeps the baseline meaningful.
- **Licensing / cost:** free (GPL). No per-run cost. Adds a few seconds.

---

## 3. Option (b) — XC32 built-in MISRA-C

> **⚠️ The ticket's premise "already paid for via XC32 license — just enable `-misra`" is very likely FALSE.** MISRA checking is **not** a feature of the free/standard MPLAB XC32 edition. Historically it was gated behind the **XC32 PRO** license; in the current subscription model it is part of the **Functional Safety (MISRA / MISRA-C) license add-on**, a **separately-purchased paid product**. Before any effort is spent here, someone must confirm the exact XC32 v4.60 license tier on the build station (`xc32-gcc --version` + the license manager / `xclm` status) — the "just flip a flag, it's free" assumption should be treated as unverified. If we're on the free edition, this option has a **real dollar cost** (functional-safety license per seat), which changes the calculus entirely.

Assuming the license *is* present:

- **What it adds beyond cppcheck:** MISRA checking wired into the *actual production compiler front-end* — so it sees exactly the translation units, macros, include paths, and `-D` defines the real build uses (cppcheck approximates these from the Makefile). That means fewer "cppcheck couldn't resolve the config" misses and findings that are congruent with what ships. Coverage is the Microchip MISRA implementation (2012), configurable per-rule.
- **Integration effort:** medium. XC32 is invoked through the MPLAB X project + generated Makefiles (which are **gitignored** and regenerated — see CLAUDE.md), so the flag has to live in `configurations.xml` (per-config, and kept consistent across NQ1/NQ2/NQ3) or in a dedicated analysis-only make target. CI implication: the cppcheck gate runs on a plain Ubuntu runner with no toolchain; an XC32-MISRA gate needs XC32 installed in CI (it *is* available Linux-side at `/opt/microchip/xc32/v4.60`) **and** a license reachable from the runner — non-trivial for a hosted runner.
- **False-positive / baseline burden:** similar order to the cppcheck MISRA addon (same standard, different implementation) — hundreds+ on a legacy tree; needs a suppression/baseline story. Microchip's MISRA output format differs from cppcheck's, so it can't reuse `cppcheck-baseline.txt` — it's a second baseline to maintain.
- **Licensing / cost:** **paid** (functional-safety add-on) unless already licensed — the open question above.

**Verdict:** highest-fidelity MISRA source (real compiler front-end), but the **licensing assumption is unconfirmed and probably wrong**, and CI integration (toolchain + license in the runner) is the heaviest of the three. Park behind Option (a) until the license tier is confirmed.

---

## 4. Option (c) — Coverity Scan

- **Free for OSS:** yes — `scan.coverity.com` is free for open-source projects, and this repo is public, so it qualifies. Integration is a GitHub Actions workflow that runs `cov-build` (wrapping the compile), uploads the result tarball, and Coverity's cloud does the analysis.
- **What it adds beyond cppcheck/MISRA:** genuine **inter-procedural** dataflow — cross-function null-deref, use-after-free, resource/leak tracking, uninitialized-read (the PR #409 class **natively**), tainted-data, and a **concurrency checker** that can flag missing-`volatile` / unsynchronized shared-global access (the PR #410 manual-audit class, **semi-automated**). This is the only tier of the three that does whole-program value-set analysis; it is qualitatively stronger than cppcheck's mostly-intraprocedural engine.
- **The build-capture wrinkle (this is the real effort):** Coverity must *observe the actual compile*, so `cov-build` has to wrap the **XC32 cross-compiler**. Coverity doesn't know XC32 out of the box — you run `cov-configure --comp-config` (or `--template --compiler xc32-gcc`) to teach it the compiler's built-ins and target. This is doable (XC32 is GCC-derived and Coverity handles GCC cross-compilers well) but is the single biggest setup task, and it means **CI must build the firmware with XC32** under `cov-build` — heavier than the standalone cppcheck runner. XC32 is available Linux-side, so a self-hosted or toolchain-provisioned runner can do it.
- **Submission-frequency limits:** Coverity Scan throttles builds by project size — for our LOC bracket (~tens of kLOC of owned source) the free tier allows multiple submissions/day but **not per-push on a busy PR day**. So Coverity realistically runs **nightly / on-merge-to-main / on-demand**, *not* as a per-PR blocking gate. The ticket's acceptance criterion "runs on every PR … zero-new-defects gate" is **not achievable within free-tier limits** and should be relaxed to nightly-advisory + triage.
- **False-positive / baseline burden:** Coverity's FP rate is low-to-moderate and its web UI has first-class **triage/dismissal workflow** (mark intentional / false-positive with justification, persisted server-side) — so unlike cppcheck it doesn't need a text baseline in-repo; the "baseline" lives in the Coverity project. First-scan triage of a legacy tree is still a real one-time chunk of work (dozens–hundreds of findings to classify).
- **Licensing / cost:** free (OSS tier). Cost is **engineering time** (cov-configure + CI build capture) and the **public-defect-visibility** consideration (Scan findings are visible to project members; fine for us).

**Verdict:** the strongest *bug-finding* tier and the only one that touches the inter-procedural class the ticket cares about — but it is **advisory/nightly by nature** (frequency limits), and the "would've caught #354" claim remains hypothesized (§1.1). Best sequenced *after* the cheap cppcheck-MISRA win proves the team's appetite for triaging a larger finding set.

---

## 5. Comparison table

| Dimension | (a) cppcheck + MISRA addon | (b) XC32 built-in MISRA | (c) Coverity Scan |
|---|---|---|---|
| **Analysis depth** | Intraprocedural + limited value-flow; ~130/143 MISRA rules | MISRA 2012 via real compiler front-end | **Inter-procedural** dataflow + concurrency + taint |
| **Catches beyond today** | MISRA 8.13/11.x/10.x/1.3 pattern rules | Same rule set, higher config fidelity | Null-deref, uninit (#409), missing-volatile (#410), leaks, UAF |
| **The #354 -O3 miscompile** | No (flags the *shape* at best) | No (flags the *shape* at best) | No — hypothesized only (§1.1) |
| **CI integration effort** | **Low** — one flag in existing script | Medium–High — XC32 + license in runner, 2nd baseline | High — `cov-configure` XC32, build-capture in CI |
| **Gating model** | Advisory → gate (baseline-diff, reuses existing) | Own baseline; gate feasible if licensed | **Advisory/nightly** — free tier can't per-PR gate |
| **First-run FP/baseline burden** | High once, then flat (existing baseline mech) | High once; **separate** baseline format | Moderate; server-side triage UI (no in-repo baseline) |
| **Licensing / cost** | **Free** (GPL) | **Paid** functional-safety add-on — *ticket assumption unconfirmed/likely wrong* | **Free** (OSS Scan tier) |
| **Recurring cost** | ~seconds/run | build-time; license seat | ~10–20 min/scan; frequency-limited |
| **Runner needs** | Plain Ubuntu (as today) | XC32 toolchain **+ license** | XC32 toolchain + Coverity tools |

---

## 6. Phased recommendation

**Phase 1 — cppcheck MISRA addon, non-gating advisory (do first).**
Add `--addon=misra` (and consider `threadsafety`) to a *separate advisory* cppcheck invocation — **not** wired into the existing gating baseline yet. Emit its findings as a CI annotation / artifact that doesn't fail the build. Rationale: it's the cheapest possible step (one flag, existing free runner, no license, no new infra), it surfaces the MISRA-shaped findings closest to the #354 class, and running it advisory-first lets us *measure* the real violation count and FP rate before committing the team to triaging it. Blanket-suppress the categories we consciously don't follow (e.g. 15.5 single-exit) at the outset so the signal is legible.

**Phase 2 — evaluate, then gate cppcheck-MISRA.** After Phase 1 has run on real PRs for a few weeks, review the finding set: classify into (real bugs / fix), (accepted-forever / suppress with justification), (whole-rule-not-for-us / category-suppress). Fold the residue into a committed MISRA baseline and flip it to gating ("no new violations"), mirroring today's cppcheck-baseline mechanism. Update `tools/lint/` docs on interpreting + suppressing MISRA findings.

**Phase 3 — Coverity Scan, nightly advisory.** Register the repo on `scan.coverity.com`, stand up the `cov-configure`-for-XC32 + `cov-build` GitHub Actions workflow on a **nightly / on-merge** schedule (not per-PR — free-tier frequency limits forbid it). Triage the first-scan findings in Coverity's UI. Keep it advisory until the team trusts the signal; only then consider a merge-blocking "no new high-severity defects" gate. This is where the inter-procedural / missing-volatile / uninit classes (#409/#410) actually get automated.

**Phase 4 (conditional) — XC32 built-in MISRA.** Only if (i) Phase 1/2 shows MISRA is delivering real value *and* (ii) the XC32 v4.60 license tier is **confirmed** to include the functional-safety/MISRA feature (verify `xclm` status first — do **not** assume it's "already paid for"). Even then, it largely duplicates Phase 1/2's rule coverage at higher fidelity; justify it by the CI-toolchain cost before proceeding. If the license is absent, this phase requires a purchase decision and should be its own budget ticket.

### Corrections to the ticket's acceptance criteria

The ticket's original criteria need two adjustments before they're actionable:

1. **"Coverity runs on every PR with a zero-new-defects gate"** → not feasible on the free Scan tier (submission-frequency limits). Reframe as **nightly/on-merge advisory**, gate only after triage maturity.
2. **"XC32 `-misra` — already paid for, just enable the flag"** → **licensing unverified and probably a paid add-on.** Gate this phase on confirming the license tier; do not treat it as free.
3. The **"would have caught #354"** framing is **hypothesized, not proven** (§1.1) — no source-level tool models the -O3 backend. Keep it as motivation for a broader net, not as a success metric any single tier must meet.

---

## 7. One-line summary

Start with the **free cppcheck MISRA addon as a non-gating advisory** (Phase 1) — cheapest, closest to the #354 *shape*, measures the burden before we commit. **Coverity Scan (free OSS, nightly-advisory)** is the strongest bug-finder and the right home for the inter-procedural / volatile / uninit classes, but is advisory-by-nature and sequenced third. **XC32 built-in MISRA is last and conditional** — its "already licensed / free" premise is unconfirmed and likely wrong, and it duplicates the cppcheck-MISRA rule coverage at higher CI cost. No lint-config changes land here; each phase is its own follow-up PR.
