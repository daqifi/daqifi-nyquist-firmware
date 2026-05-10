# Set-Once Shared Pointer Volatile Audit

**Tracking issue:** #421
**Companion audit:** #430 — `docs/POOL_POINTER_AUDIT.md` was committed
in PR #433 for the investigation record and then archived from `main`
(commit `283d3024`) per the project's one-off-tools policy. Recover
with `git show 7c73d974:docs/POOL_POINTER_AUDIT.md` if needed.
**Date:** 2026-05-10
**Status:** Audit complete — **no action**. None of the inspected sites
need `T * volatile`.

## Why this audit exists

Issue #421 was filed on the premise that the original #354 ch15
streaming regression was caused by GCC at -O3 hoisting/merging address
loads of set-once shared static pointers across the task↔ISR boundary.
PR #420 (volatile-only) was an early attempt to fix that bug for three
ADC.c pointers; it was closed without merging.

The current `main` HEAD has none of those volatiles applied — yet ch15
works correctly. This audit re-examines what actually fixed ch15 and
whether the volatile-pointer pattern is needed elsewhere.

## What actually fixed ch15

Commit `96e7c840` —
**`fix(adc): TRGSRC=3 (STRIG) for shared MODULE7 channels (#406, #421)`**.

Per the commit message and Section 22 of the PIC32MZ ADC FRM
(DS60001344E):

> *"The 5 hardware ADCHS channels CH5/6/7/8/11 (NQ1 user-channels
> 0/9/11/13/15 — all on shared MODULE7 mux scan) were streaming
> all-zeros after PR #282 introduced hardware-trigger sync... The
> Microchip-generated `plib_adchs.c` boots ADCTRG2/3 with TRGSRC=1
> (GSWTRG) on hardware channels 5-11... #282 replaced that software
> trigger with hardware STRGSRC=TMR5. But per-channel TRGSRC takes
> priority over STRGSRC for HW channels 0-11, so they kept waiting
> for a GSWTRG that the new code path never fires."*

This is a **hardware register configuration bug** — completely
unrelated to compiler optimization or volatile semantics.

The volatile-pointer narrative ("at -O3 GCC may hoist/merge address
loads") was a misattribution that propagated through PR #420 → issue
#421 → CLAUDE.md atomicity rules. The codegen evidence below confirms
the misattribution.

## Codegen A/B evidence

Built three ELFs at `-O3` with XC32 v4.60 on PIC32MZ2048EFM144:

1. `main` (no PR #443 volatiles)
2. `pr443-r1` (six `T * volatile` qualifiers added)
3. `pr443-r2` (r1 + ISR/task entry snapshot pattern)

Disassembled with `xc32-objdump -d` and extracted per-function bodies
for diff. Per-function instruction-line counts:

| Function | no-vol | +vol | Δ |
|---|---:|---:|---:|
| `ADC_ReadADCSampleFromISR` | 56 | 128 | +72 |
| `MC12bADC_EosInterruptTask` | 188 | 173 | -15 |
| `ADC_HandleAD7609Interrupt` | 128 | 72 | -56 |
| `AD7609_BSY_InterruptCallback` | 68 | 72 | +4 |
| `AD7609_DeferredInterruptTask` | 56 | 59 | +3 |
| `Streaming_TimerHandler` | 71 | 77 | +6 |
| `streaming_Task` | 652 | 652 | +0 |

**`T * volatile` is NOT a no-op on this compiler.** Total ELF text
grows by ~1.7 KB. But the changes are not what the original premise
claimed.

### What the qualifier actually does

Inspecting the diffs:

1. **Inserts redundant pointer reloads.** Where GCC would otherwise
   cache the pointer value in a register and reuse it across multiple
   accesses, volatile forces a fresh `lw v0,-32xxx(gp)` before each
   access. Example from `Streaming_TimerHandler`:

   ```
   no-vol:
     lw   v0,-32068(gp)        ; load pointer once
     lbu  v0,0(v0)             ; deref
     bnez v0,...

   +vol:
     lw   v0,-32068(gp)        ; load pointer
     ; ... unrelated work ...
     lw   v0,-32068(gp)        ; RELOAD same pointer
     lbu  v0,0(v0)             ; deref
     bnez v0,...
   ```

   In both versions the value loaded is *the same* (no concurrent
   writer), so the second load is provably redundant for correctness.

2. **Inhibits loop optimization.** In `ADC_HandleAD7609Interrupt` and
   `MC12bADC_EosInterruptTask`, the no-vol version uses strength
   reduction — keeps a struct-pointer in a register and increments
   by `sizeof(struct)` per iteration. With volatile, GCC drops the
   strength reduction and reloads `gp + offset` plus computes
   `base + index*size` each iteration. Same observable result, more
   instructions per iteration.

3. **No effect on functions whose accesses already cross opaque
   function-call boundaries.** `streaming_Task` shows `+0` lines:
   every dereference of `gpRuntimeConfigStream` / `gpStreamingConfig`
   in that function is already separated by a FreeRTOS API call
   (`ulTaskNotifyTake`, `vTaskDelay`) or another opaque call, which
   forces GCC to reload non-volatile statics anyway. Volatile adds
   nothing here.

## Correctness analysis: why none of these need volatile

For every site PR #443 proposed to qualify (and for the existing
`streaming.c::buffer` qualifier):

1. **Set-once write semantics.** Each pointer is assigned exactly
   once — either in `Init()` pre-scheduler (ADC.c, AD7609.c,
   streaming.c gpStreamingConfig/gpRuntimeConfigStream) or in a
   setter that runs only while the consumer is suspended on
   `ulTaskNotifyTake()` (streaming.c::buffer).

2. **No concurrent writer.** There is nothing for `volatile` to
   protect against. The compiler-cached register copy is observably
   identical to the freshly-reloaded memory value.

3. **Function-call boundaries empirically force reload of file-scope
   statics on this build.** Every consumer path crosses opaque
   function-call boundaries — FreeRTOS API (`ulTaskNotifyTake`,
   `xSemaphoreTake`, `vTaskDelay`, `xTaskNotifyGive`), Harmony PLIB
   (`GPIO_PinIntEnable`, `EVIC_*`, `ADCHS_*`),
   `BoardData_Get`/`BoardData_Set`, etc. At `-O3` without LTO, GCC
   *in practice* spills caller-saves and reloads file-scope statics
   after such calls — verified by the codegen A/B above (the no-vol
   variants for ADC.c and AD7609.c show the cached-pointer pattern
   precisely because the function-call barriers force reload). This
   is **not a guaranteed property of the C language** — file-scope
   `static` variables have internal linkage, and a sufficiently
   aggressive compiler (or one with LTO/IPA visibility) could in
   principle prove the callee doesn't touch them and elide the
   reload. We rely on the empirical -O3 + no-LTO behavior of this
   specific XC32 v4.60 build; if that contract changes, re-audit
   per the triggers in the "Caveats and re-audit triggers" section.
   The bench A/B for `streaming.c::buffer` (see "What we keep" table)
   demonstrated that this contract has at least one execution path
   where it doesn't hold — a useful negative reminder that the
   barrier story is empirically valid here, not principled.

4. **Consistent with the #430/#433 audit, with one caveat.** The pool-
   publisher audit (`POOL_POINTER_AUDIT.md`) tagged 6 of 7 set-once-
   shared-pointer sites as `SAFE-via-barrier`. For the *six speculative
   PR #443 sites* (ADC.c × 3, AD7609.c × 1, streaming.c × 2), the
   reasoning holds — function-call barriers are reliably present on
   every consumer path, no concurrent writer, no observable hazard.
   However, the seventh site `streaming.c::buffer` (already qualified
   on `main`) was bench-tested for safe removal and it failed the test:
   under PAT 3 / 5 kHz deterministic streaming, no-volatile produced
   1 encoder failure / 589k samples vs main's 0 / 723k. This means the
   "function-call barriers always serialize" assumption has at least
   one execution path where it doesn't hold for `buffer` — possibly a
   GCC pass that sees through the encoder-write call chain at -O3 and
   caches the pointer across an iteration, possibly inlining of a
   helper that was assumed to be a barrier. The bench result is the
   ground truth; the qualifier stays.

## What we keep, what we drop

| Site | File:line | Action |
|---|---|---|
| `gpBoardConfig`, `gpBoardRuntimeConfig`, `gpBoardData` | `firmware/src/HAL/ADC.c` | Leave non-volatile (current main state) |
| `pModuleConfigAD7609` | `firmware/src/HAL/ADC/AD7609.c:85` | Leave non-volatile |
| `gpStreamingConfig`, `gpRuntimeConfigStream` | `firmware/src/services/streaming.c:185, 188` | Leave non-volatile |
| `buffer`, `bufferSize` | `firmware/src/services/streaming.c:177-178` | **Keep volatile.** Bench A/B (NQ1 unit, 4-test sequence, 2026-05-10) showed asymmetric encoder-failure rate: `main` = 0 encoder failures across 722,792 samples; strip-volatile branch = 1 encoder failure across 589,541 samples — and the failure landed in the PAT 3 / 5 kHz deterministic test where encoder output should be invariant. One failure is rare (0.0002%) but the asymmetry against zero on `main` is real signal that *some* execution path doesn't get the function-call-barrier protection the rest of the audit relied on. The codegen savings (8 instructions in `streaming_Task`, 528 bytes total) is too small to justify shipping a non-zero encoder-failure regression. The qualifier is load-bearing intermittently and stays in place. |
| `gQuesBits`, `gInTimerHandler`, `gIsEnabled` | various (added in `96e7c840`) | **Keep volatile** — these are NOT set-once pointers; they are RMW flags / single-bit ISR flags where the qualifier serves the standard "volatile flag set in ISR, polled in task" pattern from CLAUDE.md atomicity rules. Different shape, different rationale. |

## CLAUDE.md update

The "Atomicity & Concurrency Rules" section gets a new bullet
capturing this finding so future audits don't re-litigate the
question:

> **Don't add `T * volatile` to set-once shared pointers speculatively.**
> Codegen A/B at XC32 v4.60 + -O3 + no LTO (2026-05-10) showed the
> qualifier IS observable — adds redundant reloads + inhibits loop
> strength reduction — and for the six PR #443 candidate sites
> (ADC.c × 3, AD7609.c × 1, streaming.c × 2) the function-call barriers
> on every consumer path already force GCC to reload non-volatile
> statics, so the volatile reloads are correct-but-redundant.
> However, the seventh site `streaming.c::buffer` (qualified on main)
> was bench-tested for safe removal and the strip introduced an
> encoder failure that wasn't present on main — so don't *remove*
> existing `T * volatile` qualifiers speculatively either, even when
> codegen shows the reload is "redundant by inspection." The original
> #354 ch15 regression (which motivated this whole class of concern)
> was actually fixed by a hardware TRGSRC register configuration in
> commit `96e7c840`, not by anything compiler-related. See
> `docs/SET_ONCE_POINTER_AUDIT.md`.

## Caveats and re-audit triggers

The codegen-A/B finding is specific to this build configuration. Re-audit if:

- **LTO is enabled.** With link-time optimization GCC can see
  through call boundaries and may CSE the pointer load across them.
  At that point opaque-function-call barriers no longer protect us.
- **An opaque function call in a hot loop is replaced with an inline
  helper.** E.g. `__attribute__((always_inline))` on something that
  used to be a call boundary in `streaming_Task` or
  `MC12bADC_EosInterruptTask`. The barrier disappears; reloads stop
  happening; the cached-pointer hazard becomes real.
- **A set-once pointer becomes a set-many pointer.** The whole
  premise depends on "set once in Init, never written again". If a
  re-init or hot-swap path is added, the volatile question reopens.
- **XC32/GCC compiler upgrade.** A newer GCC could plausibly do
  cross-module CSE without LTO via richer points-to analysis.
  Re-run the codegen A/B as part of the upgrade qualification.

## References

- #354 — original ch15 regression (closed)
- #420 — closed PR with volatile-only fix attempt (never merged)
- #421 — this audit's tracking issue
- #430 / #433 / `docs/POOL_POINTER_AUDIT.md` — companion audit on pool publisher pointers (same conclusion: SAFE-via-barrier in 6/7 cases)
- `96e7c840` — actual #354 ch15 fix (TRGSRC=3 for shared MODULE7 channels)
- ISO C11 §6.7.3 (qualifiers), §5.1.2.3 (program execution / side effects)
- Microchip "MPLAB Harmony v3 Synchronous Drivers" tech brief DS90003269A — prescribes critical sections + mutexes for cross-context shared data, NOT volatile for set-once pointers
