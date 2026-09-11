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

`test_981_sd_failnext_hook.c` covers the one-shot arm/consume contract behind
`SYSTem:STORage:SD:FAILNext` (issue #981) — a bench/test-only hook that forces
ONE real SD write, issued during the next teardown drain, to fail once, so the
write-failure accounting paths fixed by #825/#838/#915/#979 can be
regression-tested without running the bench card out of space.
`sd_card_manager.c` is not includable on the host (FreeRTOS + Harmony's
`SYS_FS` + the whole SD state machine), so — same technique as
`test_943`/`test_953` — this re-implements just the flag/consume shape against
a mock and the Makefile greps the real source for four properties the model
depends on: the flag's declaration, the POSITION (not just presence) of the
critical-sectioned test-and-clear and its `currentProcessState ==
UNMOUNT_DISK` safety gate at the consume site, the `#409` reset-scrub landing
immediately before the `isInitDone` guard, and the setter being a plain store
with no critical section of its own (the atomicity argument the firmware PR's
design review settled). Any of the four moving fails the **build**, not just
the test. Covers: starts disarmed, arm-then-consume is one-shot,
disarm-without-consuming leaves the next write clean, re-arming after a
consume works again, and disarming an already-idle hook is a no-op.

The consume-site guard checks POSITION rather than four independent
presence-only greps because an adversarial audit on PR #1013 found the
presence-only version could not fail on two real mutations of the source:
moving `taskEXIT_CRITICAL()` to before the armed test (every token still
present, atomicity destroyed), and flipping the real
`injectWriteFailure = true;` to `= false;` (a token the old guard never even
looked for). `test_981_sd_failnext_real_consume.head.c` / `.tail.c` close the
remaining gap a textual check — however positional — cannot: the Makefile
splices **all of `SDCardWrite()`**, signature through closing brace, verbatim
out of the real source and into a tiny compiled-and-RUN harness (no-op
critical-section and `LOG_E` stand-ins, the five `gSDCardData` fields the
function touches, and a **mock `SYS_FS_FileWrite` that counts its calls** and
returns a byte count that is never `-1`). Nothing about the function is
hand-typed there.

It asserts the EFFECT, on two observables: **fired** → the function's real
`int` return is `-1` **and zero calls reached the filesystem**; **not fired**
→ the return is the mock's byte count **and exactly one call** did. Its first
case is a control that asserts the write *is* reachable, so the "zero writes"
assertions cannot pass vacuously. Covers: the `injectWriteFailure` mutation
above, the `UNMOUNT_DISK` gate itself (armed-but-elsewhere must not fire, and
the write must go through untouched), one-shot consumption, and an arm that
survives a refused attempt firing at its next opportunity.

That whole-function splice is round **three** of the same defect. Round two
extracted only as far as `taskEXIT_CRITICAL();` and let the tail close the
fragment with a hand-written `return injectWriteFailure;` — so the payload
(`if (injectWriteFailure) { LOG_E(…); writeLen = -1; goto __exit; }`, the real
write it must skip, and `__exit: return writeLen;`) was never compiled or run,
and the assertion was on a **local bool**: the same proxy-not-effect shape the
first two rounds were spent on. Deleting **only** the `goto __exit;` from the
real source left all eight binaries green while, on hardware, the hook cleared
its arm, logged "consumed", set `writeLen = -1`, then fell through into the
real write, which succeeded and overwrote it — fault injection entirely dead,
a "failed" write silently succeeding, nothing red anywhere. That mutation now
fails this binary with the return value and the write count in the message.
Its build-time sanity check on the extraction is deliberately a loose bound
(a plausible line count, last line `}`) rather than an exact one, because an
exact count would fail the *build* on the very mutations this binary exists to
catch by *running* the code — turning a precise behavioural verdict back into
a text-shape one.

It still does not attempt the ordering mutation — a single-threaded host run
cannot observe a concurrency property — which stays the positional grep's job.

None of this exercises the real device or hardware timing. Only two of
`SDCardWrite()`'s five call sites may consume a given arm in practice — the
two that run inside a teardown already in progress — by design (the other
three can trigger the pre-existing `ERROR → UNMOUNT_DISK → INIT →
OPEN_FILE(WRITE_PLUS)` remount, which truncates the log file; see
`gFailNextWrite`'s block comment in `sd_card_manager.c`). Real-hardware
validation of the SCPI-to-drain path is `test_981_sd_failnext_hook.py`
(daqifi-python-test-suite)'s job.

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
