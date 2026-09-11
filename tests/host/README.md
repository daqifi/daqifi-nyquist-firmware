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

### The reason-string length guard (#1001)

The same file also measures how long `SD_SuspendReasonText()`'s returns are
allowed to be. `Logger` truncates a formatted line, and #986 was a reason that
did not fit: it arrived cut at `then SYST:STOR:SD:ENAb`, losing the command
that clears a quarantine from the message whose only job was to name it.

**Why this needed a generator rather than a copy.** Three guards were tried in
PR #983 and each was defeated in review before the mechanism was withdrawn and
the design filed as #1001:

| attempt | how it was defeated |
|---|---|
| `grep -qF 'then SYST:STOR:SD:ENAble 1'` on the source | the matched words are a substring of the 95-character string they replaced **and** of the comment explaining the replacement, so the grep passed for exactly the state it existed to catch |
| sha256 of the function's text | the pipeline ran `tr -s '[:space:]' ' '`, collapsing whitespace **inside** the literals, so a reason respaced in its own text left the hash unchanged |
| the test measuring its own copies against a `-D` limit | copies drift from the firmware, which is what the first two existed to prevent |

All three pinned something *about* the strings. A fourth of that kind would
have drawn a fourth finding, so the guard measures the strings themselves:

- `gen_1001_suspend_reason_fixture.py` brace-matches `SD_SuspendReasonText()`
  in `SCPIStorageSD.c` (string-, char- and comment-aware, so a brace in prose
  cannot close the body) and decodes **every literal it returns**, joining
  adjacent literals the way the compiler does — the quarantine reason is
  spelled across two source lines.
- It also extracts the **four** `LOG_E` format strings in that file which
  interpolate the reason, the longest command mnemonic that can reach each one
  (following `SD_ArmOrRefuse`'s forwarding into
  `SD_ArmOrRefuseWithCleanup`, without which the binding site would be
  measured against one mnemonic instead of six), and the character ceiling —
  `LOG_MESSAGE_SIZE` from `Logger.h` plus **both** reservations in
  `Logger.c`, of which the tighter wins.
- The output, `gen_1001_suspend_reasons.h`, is a build artifact (gitignored)
  and a Makefile **prerequisite** of the test binary, so editing any of those
  three firmware files regenerates it. The test `#include`s it
  unconditionally: if the header is missing and cannot be produced, the test
  fails to *compile*. There is no route by which it quietly skips.
- The test then asserts `prefixLen + strlen(reason) <=
  GEN_1001_LOG_SURVIVING_CHARS` for **every** (reason x call site) pair — 3 x
  4 = 12 — and recomputes each `prefixLen` from the format text in C rather
  than trusting the generator's arithmetic. On failure it names the site, the
  format, the mnemonic, both lengths and the overshoot.

The generator **refuses to guess**: it exits non-zero and writes nothing if the
function cannot be found, if a `return` in it is neither `NULL` nor string
literals, if the literal or call-site counts change in *either* direction, if a
format carries a conversion whose length it cannot bound, if a mnemonic cannot
be traced, or if either `Logger` constant will not parse — each with a message
naming what a human has to re-derive. That is why this target, uniquely, has no
grep guard in the Makefile: there is no copy to check.

**Coverage, stated narrowly.** The four sites in `SCPIStorageSD.c` are every
caller in that file; `SD_ArmOrRefuseWithCleanup` binds, leaving 82 characters.
`SCPIInterface.c` interpolates the same returns at three further sites and is
deliberately not read (out of scope per #1001). Two of those are looser than
the binding site measured here, so this budget already covers them; the third,
`SCPI_StartStreamingClaimed`'s arm-raced refusal, leaves only 35 characters, so
**all three** reasons are cut there and no reason string can be made short
enough. That prefix has to shorten instead — it is **#1000**, and it is left
out rather than folded in so a budget nothing can satisfy does not sit in the
table looking actionable.

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
