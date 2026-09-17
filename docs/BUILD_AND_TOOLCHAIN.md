# Build & toolchain details

> Split out of `CLAUDE.md` on 2026-09-10. That file is loaded into **every turn of
> every agent**, and at ~38k tokens it was ~13% of all token spend; this material is
> reference — needed when you work in this area, not on every task. **It is
> unchanged, not summarised.** Read it in full before changing anything it
> describes, and update it here rather than re-adding it to `CLAUDE.md`.

Per-file optimization overrides, the source patches -O3 requires, the behavioural patches to vendored third-party sources, and the known linker issue.

---

### Compiler Optimization Level

The project builds with **-O3** globally, with per-file overrides where needed. This required patches to fix false positives and third-party incompatibilities:

### Per-File Optimization Overrides

| File | Optimization | Reason | Permanent? |
|------|-------------|--------|-----------|
| `third_party/wolfssl/wolfcrypt/src/tfm.c` | -O3 with `-Wno-error=array-bounds` | GCC loses track of loop variable range after inlining in wolfSSL big-number math. Known third-party issue. | No — reevaluate after wolfSSL upgrade (currently pinned to v5.4.0) |

The previous `FreeRTOS_tasks.c -O1` override was replaced (issue #426) by defining `configLIST_VOLATILE volatile` in `FreeRTOSConfig.h` — the upstream-blessed back door for compilers that hoist/reorder stores in `listINSERT_END` at -O2+.

⚠️ **The override was only half-removed in `default`, and this sentence used to claim otherwise.** The `<item>` for `FreeRTOS_tasks.c` is still present with `overriding="true"`, and its `<C32>` (C compiler) optimization value was blanked rather than the item being deleted — so in `default` the kernel compiles with **no `-O` flag at all**, not -O3. Verified 2026-08-18 by building `default` and reading the `xc32-gcc` command line: that one file carries zero `-O` tokens while every other file carries `-O3`. `Nq3` has no such item and does compile it at -O3. Removing the dead item (so the kernel inherits the conf-level -O3) is a behavior change and needs its own bench validation — do not do it as drive-by cleanup.

### Source Patches for -O2/-O3

| File | Patch | Reason | Permanent? |
|------|-------|--------|-----------|
| `libraries/scpi/libscpi/src/utils_private.h:49` | Added platform guard to `__attribute__((visibility))` | ELF visibility is meaningless on bare-metal PIC32, errors at -O3 with -Werror | No — reevaluate after libscpi upgrade |
| `Util/Logger.c:483` | `strncpy` → `memcpy` | `strncpy(dst, src, strlen(src))` never null-terminates; memcpy is what the code actually means (next line does manual null-term) | **Yes** — genuine bug fix |
| `services/wifi_services/wifi_serial_bridge_interface.c:68,80` | `__attribute__((noinline))` on `UARTReadGetBuffer` | GCC -O3 inlines 512-byte ring buffer read into 1-byte caller, triggers false `-Warray-bounds`. Function takes a mutex so inlining is counterproductive anyway. | No — reevaluate after XC32/GCC upgrade |

### Behavioural Patches to Vendored Third-Party Sources

Distinct from the -O3 table above: those exist to make the compiler happy and
are expected to evaporate on a toolchain upgrade. **These change what the
library DOES**, so an upgrade that drops one silently reopens a shipped defect.
Re-apply or re-verify every row after any upgrade of the named library.

| File | Patch | Reason | On upgrade |
|------|-------|--------|-----------|
| `libraries/scpi/libscpi/src/parser.c`, `test/test_parser.c` | (a) `DaqifiIntTokenFullyConsumed`, used by `ParamSignToUInt32` / `ParamSignToUInt64`: an integer parameter must be a decimal token the conversion consumed IN FULL; a partly-read token pushes `-104` instead of yielding its truncated prefix. (b) `SCPI_ParamBool` (parser.c) DISCARDED that conversion's return and now honours it — otherwise it reports success with a value taken from a rejected token. (`expression.c` has three more of the same shape; deliberately NOT touched — see the note below.) (c) The four `TEST_Param*("10.5")` rows assert the new contract | #880. `strtol`/`strtoul` stop at the first unusable character and upstream tested only "converted ≥1 character", so `-0.5` → `0`, `0.5` → `0`, `1E1` → `1`, `1.7` → `1` were accepted with `0,"No error"`. `CONF:ADC:SINGleend -0.5,1` therefore WROTE channel 0 while the integer `-1` was correctly refused. It is a **parser**-level defect reaching every integer SCPI parameter in the firmware (141 direct `SCPI_Param{Int,UInt}{32,64}` calls across the six `services/SCPI/*.c` files, plus 20 `SCPI_OptionalParamInt32` and 3 direct `SCPI_ParamToInt32` sites), so no callback guard can see it — #877's `AdcChannelArgInRange` receives the already-truncated integer. Chosen over a firmware-side wrapper because the wrapper would mean editing every one of those call sites, which is precisely how the "fixed one site, left the twin" class recurs | **Re-apply, all three parts.** Upstream has the same behaviour — its own `test_parser.c` even carried `/* TODO: should be FALSE, -104 */` on those four rows — so an upgrade will drop this. Part (b) is UNREACHABLE in this firmware today (nothing calls `SCPI_ParamBool`; the hex is byte-identical with and without it) and exists so the divergence stays self-consistent. **`expression.c`'s three twins were attempted and reverted in #882**: propagating `SCPI_EXPR_ERROR` there needs two semantic decisions this firmware cannot exercise — `channelSpec` is called with `length == 0` for entries before the requested index, so a guard inside `if (i < length)` makes validation depend on which index is asked for, and the wrappers already push `SCPI_ERROR_EXPRESSION_PARSING_ERROR`, so a propagated failure queues two errors. Left to a change that can validate them -- tracked as **#884**. `test_880_decimal_token_truncation.py` is the runtime check |

### Known Linker Issue (Issue #271, informational)

The XC32 linker script (`p32MZ2048EFM144.ld`) uses a "best-fit allocator" for `.bss.*` sections. At O2+ with `-fdata-sections`, this can place variables at two addresses (`.sbss` GP-relative vs `.bss.*` best-fit), causing dual-address bugs. This was the original symptom that triggered investigation of the O2 FreeRTOS crash, but the actual fix landed elsewhere: defining `configLIST_VOLATILE volatile` in `FreeRTOSConfig.h` forces the kernel's list-item link fields to be volatile, preventing the reorder of `listINSERT_END` stores that was the real cause. With that macro defined, `FreeRTOS_tasks.c` is *intended* to build at -O3 alongside the rest of the firmware — see the ⚠️ above for why it currently does not, in `default`. The linker script is left at Microchip default.

**When upgrading XC32 or third-party libraries**, try removing source patches 1 and 3 (under "Source Patches for -O2/-O3" above) and rebuild with -Werror. If the build passes clean, the patches can be deleted. The `configLIST_VOLATILE` define should be kept regardless of compiler version — it's the upstream FreeRTOS-blessed pattern, not a workaround.
