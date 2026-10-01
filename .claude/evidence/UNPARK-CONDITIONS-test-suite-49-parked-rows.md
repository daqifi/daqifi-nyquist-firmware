# Unpark conditions — the 49 `parked` rows in `daqifi-python-test-suite`

**Read-only. No labels changed, no comments posted, nothing touched on any board.**
Produced 2026-10-01 by lane nq-b. Scripts: `.claude/evidence/TS-parked-fieldset.sh`,
`.claude/evidence/TS-parked-conditions.sh`.

**All 49 rows fetched and payload-verified** (comment count re-read and compared against
`.comments` per row, up to 4 retries). **0 UNDETERMINED.**

---

## ⛔⭐ THE HEADLINE INVERTS THE BRIEF: THIS POPULATION IS HEALTHY, AND THAT IS A SELECTION EFFECT

| | | |
|---|---|---|
| **EXPLICIT** condition (`unparks when` / `what unparks` / `unpark condition`) | **39** | **80%** |
| condition stated as a **NEED** (no labelled heading) | **10** | 20% |
| **NO condition language at all** | **0** | 0% |

**Every one of the 49 states a condition.** Four of five state it under an explicit heading. That
is markedly better than the firmware set, where 2 of 14 had no `Unpark condition:` line and one
row's condition had been corrected twice.

### ⛔ BUT EVERY PATHOLOGY I WAS TOLD TO EXPECT IS OUTSIDE THIS POPULATION

I was briefed to expect: rows with no stated condition (**ts#312**, **ts#446**), a park whose
condition was already satisfied with the label never removed (**ts#405**, cost seven days), a hold
stated in a synonym (**ts#222** said HELD), a live credential defect nearly discarded by a
SHA-pinned staleness test (**ts#113**), and two rows based on `autopush/*` rather than main
(**ts#165**, **ts#230**).

**Checked: all seven are NOT in the 49. None of them carries the `parked` label.**

> **The `parked` label selects for rows whose author was careful enough to label them — and an
> author careful enough to apply the label is careful enough to state the condition. So "0 of 49
> undocumented" is substantially a property of the SELECTOR, not of the fleet.**

This is the same finding as *a park lives in a comment — "labels are the authority" is unsafe*,
reached from the opposite direction. There it was *7 of 8 rows carried a park with no label*; here
it is *49 of 49 labelled rows carry a stated condition*. **Both say the label and the condition are
produced by the same care, so the label cannot be used to find the rows that lack one.**

**THE COMPLEMENT IS BOUNDED AND SMALL: 74 open test-suite PRs − 49 labelled = 25 unlabelled rows**,
and that is where all seven cited pathologies live. **That is the population worth reading next**,
and it is 25 rows rather than 36.

---

## Field set — all 49 (the base question, answered for every row)

- **`baseRefName` = `main` for all 49. ZERO on `autopush/*`.** The sk#165 / sk#230 dead-trunk
  concern **does not apply to this population**. Clean negative, checked first because it was
  cheap and could have invalidated everything downstream.
- **`mergeStateStatus`: 46 CLEAN, 3 DIRTY** — and DIRTY is carried as its own axis, not absorbed
  by `parked`:

| row | state | labels | head | title |
|---|---|---|---|---|
| **ts#458** | ready, **DIRTY** | `blocked:operator-decision`, `parked` | `ec60db1189` | #1148 companion — `CONF:ADC:USECal` must not pe… |
| **ts#320** | ready, **DIRTY** | `parked` | `0b7d0befb2` | #950 zero-slot-partition regression check |
| **ts#278** | ready, **DIRTY** | `parked` | `050d3a0407` | enable a channel before asserting the post-fla… |

- **`isDraft`: 47 ready, 2 draft.**
- **Label states (not buckets): 46 carry `parked` ALONE.** Only three carry a second actionable
  label — **ts#458** and **ts#454** (`blocked:operator-decision`), **ts#349**
  (`blocked:audit-findings`). So for 94% of this population **the label layer carries no
  information** and the condition exists only in comments.
- **ts#458 is the fw#1130 shape**: two actionable labels *and* DIRTY — three independent axes on
  one row, where an `if/elif` keeps one and drops two.

---

## The 10 rows stating their condition as a NEED rather than under a heading

These are the rows a reader scanning for `unparks when` would miss. **All ten do state a
condition** — the defect is discoverability, not absence.

`#97` · `#278` · `#330` · `#343` · `#348` · `#382` · `#402` · `#410` · `#454` · `#461`

**ts#97** is the clearest instance, and its condition is a KIND absent from the firmware set —
**hardware procurement**, not hardware scheduling:

> *"## Parked — needs a DS18B20 + 4.7 kΩ pull-up on a DIO channel … **Blocked on absent
> hardware.** The sensor subset needs a DS18B20 (or any 1-Wire slave) with a 4.7 kΩ pull-up wired
> to a DIO channel. The bench has no 1-Wire device fitted, so `RESet?` finds no presence pulse and
> every sensor assertion skips."*

It also states why the row is safe to leave: *"the sensor subset auto-detects presence and SKIPs
with a clear message rather than reporting a fake PASS."* **A park that documents why its own
absence is non-harmful** — the opposite of a vacuous PASS, and worth copying.

---

## ⭐ THE STRUCTURAL FINDING: A MASS BASE-REFRESH ON 2026-10-01, 17 OF 49 ROWS

Seventeen rows carry a same-day comment of identical form:

> *"**Under which directive:** the operator's 2026-09-27 ruling that a park blocks *merging*, not
> *refreshing*. This row was N commits behind `main` and `CONFLICTING`. … **and the unpark
> condition is unchanged.**"*

That explains why only 3 of 49 are DIRTY: the population was refreshed today, each row explicitly
re-affirming its condition. **Re-stating "the condition is unchanged" in the same comment as the
refresh is exactly right** — it is the practice whose absence made fw#1124 need a separate
"this does not unpark the PR" note.

### The check that mattered: did the refresh void anyone's PASS?

My own fw#1137 finding is that a refresh moves the head off an audit `PASS` and makes a bench
byte-identity claim **false rather than stale**, while costing a `BLOCK` nothing. Applied here:

**Screened all 17 refreshed rows. None rests on a PASS without also carrying a BLOCK or
`keep_fixing`.** Every refreshed row's park rests on a BLOCK-side verdict, where a refresh is free.
The sweep was sound on this population.

⚠ **STATED AS A SCREEN, NOT A CLEARANCE.** The test is "mentions PASS and never mentions
BLOCK/`keep_fixing`/`survived refutation`". A row could mention a BLOCK in an unrelated context
while its own park rests on a PASS, and the screen would pass it. **A zero here means "no row was
flagged", not "no row was affected"** — and since fw#1137 proves the hazard is real in the
firmware repo, the screen firing zero is informative rather than conclusive.

---

## Why I stopped here rather than producing 49 four-field entries

Authorised explicitly: *"If the first ten come back as ordinary parks with clear conditions, say so
and stop … a sampled 'these are ordinary' is worth more than a complete table nobody reads."*

The first ten were substantially ordinary — 8 of 10 with explicit conditions naming tickets and
heads (ts#287's *"**Unparks when:** the 3 confirmed findings — filed as #304 … — are fixed, and a
fresh adversarial audit returns clean on the resulting head"* is representative). So rather than
transcribe 39 well-formed conditions, this reports **the population structure, the three DIRTY
rows, the ten discoverability cases, and the one check whose answer could have been actionable.**

**The per-row payloads are on disk** (`scratchpad/ts/<pr>.txt`, all 49, payload-verified) if anyone
wants a specific row quoted verbatim — fetching was the expensive half and it is done.

### ⚠ One instrument defect found and fixed mid-task

My first extractor matched `'ExclusiveHeld'` — a Python dict key in a code diff on ts#281 —
because I listed bare `Held|HELD` alternatives with no word boundary. Same class as a bare
`noProvenance` matching `noProvenance: false`. Fixed to boundary-anchored alternatives; **and the
first fix introduced a second defect**, because dropping `re.I` for boundary precision made
`**Unparks when:**` (capital U) stop matching. The working form is boundaries **with** `re.I`,
verified 8/8 in both directions (3 must-miss, 5 must-hit).

⛔ **And one case the fix cannot reach, stated rather than papered over:** *"the bus is held so
`SD:SPACe?` stopped answering"* is a TRUE match for the pattern and is NOT a park. That is the
**discuss-vs-is** problem — a speech-act distinction, not a pattern one — and it stays in the human
read. No regex closes it.
