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
`SYST:STOR:SD:BENCHmark` (issue #943). Unlike the other two it does **not**
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

`test_947_sysinfo_write_abort.c` covers the shared-response-buffer hold inside
`SYSTem:INFo?` (`SCPI_SysInfoTextGet`, issue #947). Same situation as #943:
`SCPIInterface.c` is not includable on the host, so the test re-implements
`SCPI_WriteWithRetry`'s loop and the fix's two guards (short-write abort,
cumulative deadline) against a mock clock and mock transport, and compares
pre-#947 / post-#947 / a "guard 1 only" mutation on identical inputs. The
central claim it proves: the short-write check alone does **not** bound the
hold — a transport that always eventually accepts every write, but only after
consuming most of its own retry budget each time, never trips it, and reaches
the same ~90 s-class hazard the old code had. Only the cumulative deadline
(`SCPI_SYSINFO_WRITE_BUDGET_MS`) catches that shape. Also covered: a healthy
host sends every write with zero delay in both the old and new shapes (no
reply-content change), and the deadline's tick-counter-wrap safety.

Three firmware constants are a copy here (the two shared `SCPI_WriteWithRetry`
retry constants, plus the new per-callback budget). The Makefile target greps
all three out of `SCPIInterface.c` and **fails the build** if any drifts.

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
