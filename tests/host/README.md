# Host unit tests (`tests/host/`)

PC-based (`gcc`) unit tests for self-contained firmware utility modules. No
hardware, no MPLAB X, no RTOS — compile and run in seconds. Issue #124.

## Run

```bash
cd tests/host
make run
```

`make run` builds and runs the suite; the process exit code is non-zero if any
test fails, so it drops straight into CI. `make clean` removes build artifacts.

**Wired into CI as of #946**: `.github/workflows/host-tests.yml` runs `make
run` in this directory on every PR/push touching `tests/host/**` or the
firmware sources these tests compile or grep (see that workflow's `paths:`
list). Before #946 this sentence described the exit-code *contract* only —
no workflow anywhere in the repo actually invoked `make run`, for any of the
three tests here, not just the one #946 added.

## What's covered

`test_circularbuffer.c` exercises `firmware/src/Util/CircularBuffer.c`:

- init / `AddBytes` / `NumBytesAvailable` / `NumBytesFree` accounting
- exact-full fill and all-or-nothing add rejection
- physical wrap-around (copy-mode stitches both chunks; callback-mode
  deliberately hands only the pre-wrap chunk — see the comment in
  `CircularBuf_ProcessBytes`)
- `ProcessBytes` callback semantics (#126): ring advances by the callback's
  returned count only, clamped to the bytes offered; a negative return
  consumes nothing and propagates the error
- `Reset`, `InitExternal` (borrowed buffer, not freed on deinit), `Resize`
  (grow owned, same-size no-op, zero rejected, external rejected)
- SPSC counter wraparound near `UINT32_MAX` — both the pure
  `produced - consumed` math and a round-trip through the real
  `AddBytes` / `ProcessBytes` API across the 2^32 boundary
- NULL-argument safety on every entry point

`test_943_bench_stall_bound.c` covers the per-chunk write loop inside
`SYST:STOR:SD:BENCHmark` (issue #943). Unlike the CircularBuffer /
FixedPointFmt / AD7609Scale tests it does **not**
include any firmware source: `SCPIStorageSD.c` drags in libscpi, FreeRTOS and
the SD manager, so the test re-implements the pre-fix and post-fix loop
**shapes** against an injected mock clock and mock `WriteToBuffer`, then
compares their verdicts on identical inputs. What it proves is that the old
shape's exit condition counted *iterations* (invariant under preemption, so its
"10 s" bound really took 10 s × the preemption stretch) while the new one
counts *elapsed ticks* and holds to `[10 s, 10 s + one poll)` at every stretch.
Also covered: the happy path takes no sleep at all, progress resets the
deadline (so a slow-but-draining card is not killed), and the tick-counter
wrap.

Because the test re-implements rather than includes, the two firmware timeout
constants are a copy. The Makefile target greps them out of `SCPIStorageSD.c`
and **fails the build** if either drifts, so a stale copy cannot pass silently.

`test_971_sd_arm_refusal_order.c` covers the under-claim settle order of a
**refused** SD arm — `SD_ArmOrRefuseWithCleanup()` in the same
`SCPIStorageSD.c` (issue #971). Six SCPI commands arm through it; on refusal
it must put `mode` back to `MODE_NONE`, run the caller's retraction (FORmat's
only), and **then** release the claim, because the release is what lets the
competing SCPI transport in. Getting that order wrong is a bug lineage, not a
hypothetical: #955 (the clear ran past the release), #964 (the retraction
did), #942/#974 (the verdict was ignored).

Like `test_943` it does **not** include any firmware source — same file, same
reason — so it is a **model** of that ~8-line function, exercised through
stubbed primitives that record an event trace. Covered: the refusal trace is
exactly clear → retract → release; a snapshot taken *inside* the release stub
shows both cleanups already done at the instant ownership is handed over; the
five-caller shape (`onRefused == NULL`) still clears and releases and invokes
nothing; and the success path releases while touching neither `mode` nor the
retraction. It then runs a cooperative two-owner simulation that offers the
competing transport the CPU at every step boundary, and requires the
production order to survive all of them **and both historical defect orders
to fail at one** — a model no wrong ordering can fail would establish nothing.

Two limits are stated at length in the file's own header and are worth
repeating here. It does not reproduce concurrency: the interleavings are
hand-enumerated step boundaries in a single-threaded program, so it can show
an ordering is unsound but not that one is safe under FreeRTOS on PIC32MZ.
And its link to the firmware is a **content hash**, not a property check: the
Makefile hashes that function's code (whole-line comments stripped,
whitespace collapsed) and fails the build when it changes, with a separate
message for "the function is gone". That pin claims only "this text is
unchanged" — deliberately, because PR #976 catalogued fifteen ways an honest
refactor slips past a *textual* assertion about this exact function, and a
crude second copy of that assertion here would give false comfort. When it
fires, re-read the C against the model and update the hash.

Not covered, and explained in the file rather than faked: that each of the six
arm sites *ends* on refusal instead of falling through to its readiness poll.
Its callers are 60-150 line libscpi callbacks in the same non-host-includable
file, and re-implementing their shapes would assert only that the copies in
the test behave as written. The header names the smallest seam that would make
it testable.

`test_953_bench_suspend_diagnosis.c` covers the branch a little further down
the same function (issue #953): the one that has to say *why* the benchmark's
file never opened. Same technique as `test_943` and for the same reason — the
decision cascade is re-implemented against injected values rather than
included. Until #953 it had two arms, so a benchmark whose arm succeeded and
whose SD task was then suspended mid-wait (WiFi streaming taking SPI4, a WiFi
FW update, the #925 jam quarantine) hit the fallback and blamed the *card* —
"likely SPI-mode incompatible" — for a task that had simply stopped running.
The fix adds a third arm reporting `SD_SuspendReasonText()`.

The test asserts all four quadrants of (suspended × dir-full) against **both**
the pre-fix and post-fix shapes, and its headline property is that **exactly
one** quadrant moves. That is what pins the ordering decision: the recorded
`#689`/`#690` dir-full verdict is tested **before** the live suspend reason,
because it can only have been set by the SD task actually running and refusing
this request's open — so with both true the refusal is the real cause and the
suspend is incidental. Testing the suspend first (as #953's ticket proposed)
would have moved that quadrant too, silently narrowing #690.

No constants are copied here, so this target has no equivalent of `test_943`'s
two greps. What it copies is the **order of the three arms**, so the Makefile
guards that instead: it locates each arm's marker in `SCPIStorageSD.c` and
**fails the build** unless they still appear as dir-full → suspend → card.

`test_1004_help_write_abort.c` covers `SCPI_Help`'s (the `HELP` command)
shared-response-buffer write-abort bound (issue #1004) — the third site of a
pattern whose other two fixes are still IN FLIGHT: #947/PR #992 for
`SCPI_SysInfoTextGet` and #995/PR #1008 for `SCPI_GetCommandHistory` are both
still open, so neither sibling fix — nor `test_995` — is in this tree. Same
technique as `test_943`/`test_953`: `SCPIInterface.c` is not includable on the host, so the test
re-implements the pre-fix and post-fix write **shapes** — the self-gating
`ScpiHelpWrite` helper's two guards (cumulative deadline, checked before each
transport call; short-write latch, checked after) — against an injected mock
clock and mock transport, then compares their verdicts. Unlike #995's planned test,
`SCPI_Help`'s write count is not pinned to a single firmware constant (it
depends on the registered command table's total text size), so the test uses
a representative write count from the issue's own measurement plus a sweep
over a range, rather than one pinned to a `#define`. The Makefile target
greps the three firmware constants (`SCPI_WRITE_MAX_RETRIES`,
`SCPI_WRITE_RETRY_DELAY_MS`, `SCPI_HELP_WRITE_BUDGET_MS`) plus the FreeRTOS
tick-rate/width assumption out of the real source and **fails the build** if
any has drifted.

`test_json_string_escape.c` covers `firmware/src/services/JSON_StringEscape.h`
(issue #164) — the JSON string-escaping helper split out of `JSON_Encoder.c`
so it can be compiled and tested here with no board dependencies. Neither
field it protects (the WiFi SSID, and defensively the friendly device name)
is reachable via a real streaming session: `Json_Encode`'s one caller
(`streaming.c`) never includes `ssid_tag` or `friendly_device_name_tag` in
its field list, so there is no bench recipe that streams an SSID through the
encoder. This suite is the only place the escaping logic is exercised at
all. Covers: printable ASCII passthrough, the two JSON-structural characters
(`"` and `\`), the named single-character escapes (`\b\f\n\r\t`), `\u00XX`
escaping of every other non-printable byte (including 0x80-0xFF, since a raw
high byte would make the whole JSON stream invalid UTF-8), the `inLen` bound
(no over-read past a non-NUL-terminated source), buffer-too-small refusing
wholesale rather than truncating mid-escape, the exact worst-case sizing
`JSON_Encoder.c` allocates on its stack, and NULL/zero-size safety.

## Framework

`test_framework.h` is a ~90-line header-only harness — `TEST()` to define a
case, `RUN()` to execute it, `ASSERT_TRUE` / `ASSERT_EQ` / `ASSERT_BYTES`, and
`TEST_SUMMARY()` for the exit code. No Unity / CMocka / network downloads (the
build station may be offline).

## Stubs & the build-time source copy

`CircularBuffer.c` has two firmware includes that don't exist on the host:

- `osal/osal.h` → `stubs/osal/osal.h` maps `OSAL_Malloc`/`OSAL_Free` onto libc
  `malloc`/`free`, so the allocator paths run for real.
- `Logger.h` → `stubs/Logger.h` makes `LOG_E` a no-op (the real header pulls in
  FreeRTOS + libscpi).

Both are **quoted** includes, which GCC resolves relative to the *source file's
own directory* first — so a stub on the include path can't shadow the real
`Logger.h` sitting next to `CircularBuffer.c`. The Makefile therefore copies the
source to `CircularBuffer_uut.c` in this directory before compiling; from here
the quoted includes fall through to `-Istubs` and `-I../../firmware/src/Util`
(the real `CircularBuffer.h`). The copy is regenerated on every build, so it
always tracks the real source — edit `CircularBuffer.c` and re-run `make run`.

## Adding another module

1. Drop `test_<module>.c` here with its own `main()` (or extend the Makefile to
   build multiple binaries).
2. Add thin stubs under `stubs/` for any firmware headers the module pulls in.
3. If the module uses quoted firmware includes, follow the build-time-copy
   pattern above so the stubs win.
