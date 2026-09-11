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

`test_958_sd_benchmark_filename_collision.c` covers the *filename* the same
command builds (issue #958) — third suite on `SCPIStorageSD.c` and, for the
same reason, the same technique: the name generation is re-implemented against
injected tick values rather than included. Until #958 the scratch name came
from a **16-bit** slice of the tick (`benchmark_%d.dat`, `tick & 0xFFFF`), so
it repeated every 65536 ticks — 65.5 s at the 1 kHz tick — and the open it is
armed against truncates, so two benchmarks that far apart destroyed the
earlier one's file with no error and no log line. The fix uses the tick whole
(`benchmark_%lu.dat`); two runs cannot then share a tick, because the
callback's last step is a drain-and-close wait whose loop cannot exit without
entering (the `mode = MODE_NONE` + `UpdateSettings()` above it forces the
manager to DEINIT, and `IsIdle()` is IDLE-or-INIT only), so every run costs at
least one `vTaskDelay(10)` — and the `testInProgress` interlock stops the next
run from naming its file before this one returns.

Covered: the headline wrap collision (pre-fix names equal, post-fix distinct,
swept over several bases and several whole multiples of the wrap), injectivity
over a boundary-value tick table, the `benchmark_*.dat` prefix/suffix contract
the python suite's snapshot-and-diff depends on, and that the widened field
still fits `SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX` without truncation (a cut
name could collide again). The last case asserts the *premise* — equal ticks
give equal names — because that is what the fix rests on and what a reviewer
has to check against the firmware.

Not covered, deliberately: the reboot half of #958. The tick restarts at 0, so
a post-reboot run can still land on a pre-reboot file's name. Closing that
needs the candidate stat-ed before it is armed, which cannot be done in
`SCPI_StorageSDBenchmark` — the FAT volume is mounted only inside an SD-task
session and unmounted at the end of one, so a stat from that callback fails
with an unmounted-volume error rather than "absent". See the file header for
the exact error/line citations; the follow-up on #958 tracks it.

Three greps guard this target: the post-fix format string must be present, the
masked pre-fix one must **not** be (a suite whose premise is "the mask is gone"
has to fail if it returns), and the field-length constant must still be 40.

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
