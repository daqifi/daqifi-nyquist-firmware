# PRE-REGISTERED CRITERION — does a caller DEPEND on `idn_serial`'s leniency?

Written to disk BEFORE opening any caller. nq-b, 2026-09-30, read-only.
Population: the 21 root test files calling `idn_serial`, measured at commit `05eb7f72`.

## THE QUESTION — and it is not "is this caller guarded"

> **Would requiring a COMPLETE serial break this caller's LEGITIMATE use?**

The guard question asks whether a caller is exposed. **This asks whether a caller would be BROKEN by
the fix.** A caller can be unguarded *and* indifferent to the fix; that is the common case and it is
a NO here.

## DEPENDS = YES — any one of these

1. **Feeds a deliberately partial/split reply** — synthetic input constructed to truncate, a fake
   device that pauses mid-serial, a socket script that stops short. *The caller's purpose requires
   the half-read value to come back.*
2. **Compares by PREFIX on purpose** — `startswith`, a prefix glob, or a documented
   short-vs-full-SHA-style tolerance applied to the serial.
3. **Asserts a SHORT value** — any assertion whose expected value is narrower than a complete
   serial, or which asserts `idn_serial` returns non-None for an incomplete line.
4. **Matches a VARIANT whose trailing field is shaped differently** — a board/firmware whose
   `*IDN?` legitimately has fewer or differently-shaped fields after the serial, so requiring the
   trailing field would reject a healthy device.

## DEPENDS = NO

The caller passes a **live device reply** and compares for **equality** (or `!=`) to reach a
verdict. Tightening `idn_serial` changes such a caller only by making it *correct* — a truncated
reply would return `None` instead of a wrong-but-parseable serial, and `None` must then be handled,
which is a **fix-shape** question, not a dependence.

⚠ **Note the one real hazard inside NO:** if a caller compares `idn_serial(...) != expected` and the
fix makes a truncated read return `None`, that caller now FAILS on `None != expected` — still a
FAIL, still against a healthy board. **So "NO dependence" does not mean "unaffected."** I will flag
any NO whose mismatch branch cannot distinguish `None` from a real wrong peer, as
**NO-BUT-NEEDS-NONE-HANDLING**, because that is the tightening risk landing somewhere else rather
than being closed.

## UNDECIDABLE

The caller's input origin cannot be settled by reading — e.g. the value flows through a helper whose
source depends on runtime transport selection. Its own bucket; never folded into YES or NO.

## PURPOSE CHECK — mandatory, from the census hazard just recorded

> **A test that REPRODUCES a bug is textually indistinguishable from code that HAS the bug.**

So before scoring any caller, read what the file is FOR. A regression test for the truncation is a
YES (it depends on observing the half-read), not an exposed site. This check is what removed the one
non-member from the prior list of 8.

## CITATION RULE

Quote the greppable predicate. Line numbers qualified by commit `05eb7f72`.
