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

`test_1000_sd_log_arm_budget.c` covers the LOG_E line emitted by
`SCPI_StartStreamingClaimed`'s `#942` refusal arm (issue #1000) — the one that
names *why* an SD-logging start could not arm the write. Its cause text is
`SD_SuspendReasonText()`'s return, or the call site's own fallback when that is
NULL, and Logger cuts the formatted line at `LOG_MESSAGE_SIZE - 3` = 125 bytes
and staples a CRLF onto the stump, so a cut message still looks well-formed.
The prefix used to be 88 characters, leaving 35 — less than the shortest of the
three reasons (46), so all three were cut and the 76-character quarantine
reason lost `SYST:STOR:SD:ENAble 1`, the command that clears a quarantine. The
fix shortens the prefix to 27.

Unlike `test_943` / `test_953` / `test_1004` this one models no *shape* — the
subject is a length — so instead of re-implementing a loop it reproduces
Logger's truncation for real (same `vsnprintf`, same bound, same clamp, same
CRLF fixup) and runs the actual strings through it, asserting the emitted bytes
equal the intended bytes. The headline is that the quarantine remedy survives
now and provably did not before.

Almost nothing here is a copy. The Makefile target **extracts**
`LOG_MESSAGE_SIZE` from `Logger.h`, *both* of `Logger.c`'s reservations (the
`vsnprintf` bound and the clamp — the arithmetic is split across the two
files), and the prefix plus the NULL fallback from `SCPIInterface.c`, into a
generated `gen_1000_log_budget.h`. So lowering the ceiling or growing the
prefix makes the test fail on the real values rather than ask to be updated.
Extraction has a failure mode a grep guard does not — finding *nothing*, which
would satisfy every length assertion while checking none — so each extraction
is checked non-empty and the prefix is round-tripped back into the source text
before the build proceeds.

The three `SD_SuspendReasonText()` strings are the one copy. They belong to
that function rather than to this call site, and #1001 is building the general
mechanism that measures them against every caller; until then the Makefile
greps each full `return` statement and fails the build on drift. The old
88-character prefix is kept in the test as a frozen historical control, so the
suite carries its own evidence that these assertions bite.

One thing this test pins that is easy to get wrong: the **NULL fallback fit**
under the old prefix (88 + 33 + 2 = 123). Only `SD_SuspendReasonText()`'s three
returns were cut. That matters because the NULL case is the one a casual bench
reproduction reaches first, so a complete-looking line there is not evidence
the bug was absent.
`test_985_suspend_reason_consistency.c` covers `SD_SuspendReasonText()` itself
(issue #985) — the function `test_953` only tests against NULL. Same
re-implementation technique as the two above, for the same reason. Until #985
it decided *whether* the SD stack was suspended from one pair of reads
(`app_SDCard_SpiOwnedByWifi()`, itself `wifiStream || fwUpdate || quarantined`,
then `SpiBusHealth_IsSdSuspended()`) and *which cause to name* from a second,
later pair — re-reading two of the composite's three terms, and returning the
third as an unconditional default. So a cause that ended between the two pairs
was reported as a different one: a WiFi firmware update that opened the gate
and finished before its own re-read produced "WiFi streaming owns SPI4 -
SYST:STR:STOP first", telling the operator to stop a stream that was not
running. That string reaches every #589 refusal (`SD:GET`, `SD:LISt?`,
`SD:CRC?`, `SD:DELete`, `SD:FORmat`, `SD:SPACe?`, `SD:BENCHmark`) via
`SYST:LOG?`. The fix takes one flat snapshot of all four flags — using the new
`app_SDCard_WifiStreamActive()` accessor so the composite is not called *and*
decomposed — and adds a fourth arm for the previously unnamed quadrant
(suspended, no owner in this snapshot).

The environment each shape runs against is a **timeline** per flag, not a bool:
a sequence of what successive reads observe. That is what lets a case say "the
FW update was over by the second read", which is the entire defect and which a
flat-bool fixture cannot express at all. Covered: both mislabels (a FW update
and a quarantine ending mid-call), the new fourth quadrant, the all-clear NULL,
the three inherited strings and their precedence pinned by identity/content/
length, and the headline — over all sixteen *stable* combinations exactly one
verdict moves, so the NULL boundary and the other fifteen messages are
unchanged. Staleness is explicitly **not** under test: the fixed function is
still a snapshot, and no critical section is added or asserted.

No constants are copied, but the *shape* is, so the Makefile guards it: the
target extracts the real function (comment lines stripped) and **fails the
build** unless it reads each of the four flags exactly once, calls
`app_SDCard_SpiOwnedByWifi()` zero times, and still tests the four arms in the
order quarantine → fwUpdate → wifiStream → suspended. Both halves were checked
by mutating the firmware and confirming the build stops.

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

`test_999_scpi_context_storage_isolation.c` covers `CreateSCPIContext()`
(`firmware/src/services/SCPI/SCPIInterface.c`, issue #999): USB and TCP used
to be handed the SAME file-scope input-buffer and error-queue arrays, so
their independent parse/error-FIFO cursors clobbered each other's in-flight
commands and queued errors. `SCPIInterface.c` itself is not includable on the
host (same reason as `SCPIStorageSD.c` above, worse: 42 direct includes
spanning Harmony PLIB, the USB HS and WINC WiFi drivers, and FreeRTOS's
MIPS-specific port layer — confirmed by trying), but the vendored libscpi
sources that implement the actual clobber mechanism (`parser.c`/`fifo.c`/
`error.c` + their internal dependents) are portable C with zero FreeRTOS/
Harmony dependency, and this target links the REAL production libscpi
against a minimal harness. Part A reproduces the bug against real fifo/
error/parser code with storage deliberately shared the old way; Part B/D
prove the fix's shape (separate storage, no cross-talk) — but neither part
ever calls the real `CreateSCPIContext()`, since it isn't linked in. Part C's
Makefile guards therefore grep the real sources to pin that the fix's shape
(`ScpiContextStorage`, per-transport instances in `UsbCdc.c`/
`wifi_tcp_server.c`) still exists.

That gap — asserting shape via declarations while never executing
`CreateSCPIContext()`'s body — was exploited by round 2 of an adversarial
audit: reassigning the `storage` parameter to a function-local instance
inside the body reintroduces #999's exact bug without touching any
declaration, signature or caller, so it passed every original guard. A
round-2 fix added three inline Makefile greps against the body text (forbid
reassigning `storage`, forbid a local `ScpiContextStorage` instance, require
`storage->inputBuffer`/`storage->errorQueue` literally present) — but an
independent round-3 counter-audit defeated all three (an aliased pointer
with the real `storage->` text surviving only in a nearby comment; a
correct `SCPI_Init()` call followed by code that re-seats `buffer.data` and
calls `SCPI_ErrorInit()` again onto shared storage; splitting the
reassignment across two physical lines to dodge a single-line grep) and
found three false positives against legitimate code (a defensive NULL
check; a comment mentioning "ScpiContextStorage instance"; writing *through*
the pointer via `*storage = ...` rather than reassigning it).

`check_999_createscpicontext_wiring.sh` (round 3, current) replaces those
inline greps with a dedicated script: it bounds the extracted body to
`CreateSCPIContext()`'s own closing brace (not "signature to EOF"), strips
comments and flattens the body before any keyword search, scopes the wiring
check to the actual `SCPI_Init()` call's own argument text rather than
"anywhere in the body", and adds two checks forbidding the body from
touching `.buffer.data` / `error_queue` or calling `SCPI_ErrorInit()`
directly at all — closing the "correct call, then re-seat afterward" bypass
that the round-2 checks had no way to see. Mutation-proven against all of
the above (11 cases: the original reassignment, all three round-3 bypasses,
two more probes, and the three false-positive cases, all verified to fail
or pass as intended). See the Makefile's `SCPI999_BIN` comment and the
script's own header comment for the full account, including why linking
`SCPIInterface.c` itself was tried first and ruled out, and what a textual
guard still does not establish.

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
