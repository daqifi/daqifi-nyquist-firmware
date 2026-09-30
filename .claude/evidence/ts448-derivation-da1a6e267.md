# DERIVED FAILURE SCENARIO — ts#448, written BEFORE any edit

Derived by me from source at `test_harness.py`, head
`da1a6e267474eacc686cfc585c9e4c7d6b1459b3` (worktree `/mnt/c/daqifi/wt/audit-ts448`,
0 porcelain lines, matches the live PR head). **No reduced artifact was used as
evidence.** The artifact's title is quoted at the bottom only so the agreement
test can be applied; it was not an input to the derivation.

## Two sites, not one

The coordinator named one address. The shape occurs **twice** at this head:

| | site | function |
|---|---|---|
| A | `:1202` / `:1208` / `:1214` | `ReliableSCPI.query_bytes` |
| B | `:3250` / `:3272` / `:3286` | `io_bytes` |

Same three steps in the same order, same window formula. Any statement about
one is a statement about the other unless separately qualified.

## The mechanism

Whole-line mode (`until_whole_line=True`, what SD:LISt? passes) is supposed to
reject a marker **glued to preceding content** — a corrupt filename ending in
the marker text, or a frame whose line ending never arrived. It implements that
as:

1. `window = max(len(m) for m in until) + len(b"DAQIFI>") + 8`
2. `tail = bytes(out[-window:]).rstrip(b"\r\n \t")`  ← **slice first**
3. prompt strip, only when the prompt starts a new line
4. `for sep in (b"\n", b"\r"): if sep in tail: tail = tail.rsplit(sep, 1)[-1]`
5. `tail == m`

Step 4 asks **"is there a separator inside the window?"** and treats *absence*
as "the marker occupies the whole line." But the window is a fixed-size suffix
computed in step 1, so absence of a separator has two causes that step 4 cannot
distinguish:

- the marker genuinely starts its own line (the intended CLEAN reading), or
- **the separator exists in `out` but fell OUTSIDE the window** — so the bytes
  that would have exposed a glued marker were discarded in step 2, before the
  boundary test ever ran.

## The budget, which is what makes it reachable

Bytes available for context ahead of the matched marker:

    slack = window - len(marker) = (max_marker_len - len(marker)) + 15

For the **longest** marker in the set that is exactly **15 bytes**. A clean
frame already spends most of it *after* the marker — the slice happens before
the `rstrip`, so trailing bytes count:

    \r\n (2) + DAQIFI> (7) + \r\n (2) = 11

Four more trailing bytes — tab padding (which `:3245`'s own comment records as
something that happens on this device), a doubled prompt, an extra CRLF — bring
the total to 15. The slice then begins **exactly at the marker's first byte**,
`aaa.csv` is gone, step 4 finds no separator, and step 5 compares the marker
against itself.

⚠ **CORRECTED BY THE PROBE — this is an EQUALITY, not a threshold.** I first
wrote "at or after" and "past 15", i.e. that any sufficiently long trailing run
would do. The probe refutes that: with 20 trailing bytes (`\r\nDAQIFI>\r\n`
doubled) the cut lands *inside* the marker, the tail is a proper suffix of it,
and the read is correctly **refused**. The exposure is therefore

    trailing_bytes_after_the_marker == window - len(matched_marker)

exactly — one byte either side and the defect does not fire (too few keeps the
glue visible, too many truncates the marker). That is an alignment coincidence,
not a routine occurrence, and it bounds the severity: I am not entitled to call
this a defect a listing hits ordinarily.

## Concrete failing input (whole-line mode, `until` = test_728's `_LIST_TERMINATORS`)

`max_marker_len` = 26 (`__END_OF_LIST__ INCOMPLETE`) → `window` = 41.

    aaa.csv__END_OF_LIST__ INCOMPLETE\r\nDAQIFI>\t\t\t\t\r\n

The marker is **glued to `aaa.csv` with no line break** — the exact malformed
frame whole-line mode exists to reject. Trailing bytes after the marker = 15,
so `out[-41:]` starts precisely at `__`. After the rstrip and the prompt strip
`tail == b"__END_OF_LIST__ INCOMPLETE"`, no separator is present, and
`tail_is_marker` is **True**. A quiet gap then confirms it and `io_bytes`
returns `Read(data, confirmed=True)`.

**Harm:** the caller is told the listing ended on its own terminator when the
frame is damaged. Every consumer that treats `confirmed=True` as "this capture
is whole" — `test_795`'s name-sanity scan explicitly says *"io_bytes() has
already confirmed the read ended on a ..."* — then reasons about a truncated
listing as a complete one. It is a **false PASS**, against a device that really
did emit a broken frame.

## Direction of harm, stated because it is the part I got wrong first

My first pass concluded the window could only cause a false **NEGATIVE** (a
healthy reply reported unconfirmed). That was wrong, and wrong in the direction
that would have let me contradict the artifact: I counted only the bytes
*before* the marker and assumed the slice always retains ≥15 bytes of leading
context. The slice is taken **before** the `rstrip`, so the trailing prompt and
padding consume the same budget. Recording it because "my reasoning found no
defect" is exactly the reading a wrong premise produces.

## AGREEMENT TEST

Artifact title: **"Tail-window truncation can falsely confirm an unterminated
listing."**

Derivation: **AGREES.** Truncation of the tail window discards the separator
evidence and a glued/unterminated marker line is confirmed. Same mechanism the
coordinator described as slice-before-boundary. Proceeding is permitted.

## PROBE RESULT — E, executed against the real harness at this head

`probe_ts448.py`, driving the REAL `io_bytes` with a scripted fake serial.
Controls first, both directions:

| frame | window | confirmed |
|---|---|---|
| **KP** clean, marker on its own line | 41 | `True` ✔ expected |
| **KN** glued marker, 11 B trailing | 41 | `False` ✔ expected |
| glued, longest marker, **15 B** trailing | 41 | **`True`** ⛔ false confirm |
| control: same bytes, separator PRESENT | 41 | `True` (legitimately) |
| glued, single-terminator set, 15 B trailing | 33 | **`True`** ⛔ false confirm |
| glued, single-term set, 11 B trailing | 33 | `False` |
| glued, doubled prompt, 20 B trailing | 41 | `False` (cut truncates marker) |

Both controls behave, so the instrument is trustworthy in both directions. The
defect reproduces, and the control differing **only in placement** — separator
present vs glued, same byte count — returns the **same** verdict as the
malformed frame. That indistinguishability is the defect: `confirmed=True`
stops carrying the fact it is supposed to carry.

## SCOPE — the operator's question, answered from source

**It is the underlying `io_bytes` behaviour, so I stop.** Verified, not inferred:

- Site A `query_bytes` is `+` in `git diff 010c9d6d5..HEAD` → introduced here.
  Site B `io_bytes` is unchanged **context**, already on the merge-base at
  `:2901` with `until_whole_line` at `:2834` → **pre-existing**.
- `query_bytes` has **zero callers** in the suite outside `test_harness.py` and
  its own companion test. Fixing Site A alone fixes nothing any test exercises.
- The whole reach is Site B: `test_728:262`, `test_795:411`, `test_814:372`,
  `test_851:298`, `test_854:257` all call
  `io_bytes(..., until_whole_line=True)` for `SD:LISt?`, in a file 152 tests
  import.
- ⛔ The PR's own companion case **(j)** (`test_io_bytes_and_query_bytes.py:618`)
  runs shared fixtures through BOTH copies and asserts they agree *"byte for
  byte on `data`, exactly on `confirmed`."* Its fixtures are cases (a)–(e)'s,
  none of which is truncation-aligned (they are shorter than `window`, so no
  truncation occurs at all). So fixing Site A alone would not fail case (j) —
  it would leave it **vacuously green while the two copies silently diverge**,
  which is the exact drift case (j) was written to catch.

So the in-scope-only fix is worse than no fix: it defeats the PR's own tripwire
while repairing a method nobody calls. The minimal fix with any reach is at
`io_bytes`, with `query_bytes` following to preserve parity — a shared-file
behaviour change, its own row. **Nothing was edited.**
