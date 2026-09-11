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
