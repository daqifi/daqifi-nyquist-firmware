# fw#1152 round 10 handoff

WAS DOING: Implementing round 10 of fw#1152 (ADCUseCalSetClaimed USECal
selector/readback recovery) -- F1 (the introduced HIGH: the mismatch
branch's independent re-read classified by readability alone, not by
selector) plus three review-leg findings (a/b/c).

NEXT STEP: Done. Commit pushed to nqb/1152-round10. Nothing further
required unless a later round finds a new issue.

SAFE TO RESUME FROM: the pushed commit on nqb/1152-round10 (see git log).
All five files (SCPIADC.c, daqifi_settings.h, tests/host/Makefile,
tests/host/README.md, tests/host/test_1152_usecal_type_trust.c) were
edited together and the full host suite + cppcheck were verified green
after the full set of edits -- do not cherry-pick a subset.

Verified before push:
- `make -C tests/host clean && make -C tests/host run -j20`: 29 binaries,
  341 tests, 0 failed, 44767 assertions.
- `bash tools/lint/cppcheck.sh /tmp/.../cc.txt` then
  `diff -u --strip-trailing-cr tools/lint/cppcheck-baseline.txt /tmp/.../cc.txt`:
  no diff (0 findings, matches the empty baseline).
- Two mutation passes against test_1152_usecal_type_trust.c's model (bypass
  the calVals recheck -> round10_case_a/c go red; revert to r9's
  never-recover -> round10_case_b + the r9-vs-r10 diff test go red), then
  file restored byte-identical to the committed version (diff confirmed
  IDENTICAL) before the real commit.

Found and corrected in the brief while implementing: the brief's F1
section attributed the mismatch-branch re-read's potential disagreement
with the first check to "TopLevelMatchesCommitted runs against the shared
gTempFflashBuffer" -- that is factually wrong. Read
daqifi_settings_LoadFromNvm (daqifi_settings.c:67-135): it reads flash
directly via PA_TO_KVA1 and never touches gTempFflashBuffer (only
SaveToNvm does, for staging). The real reason two independent reads of
the same NVM address can disagree is ordinary task concurrency: another
writer of this exact TopLevelSettings record -- SCPI_SaveDataPrecision /
CONF:VOLT:SAVE (SCPIInterface.c:7201) -- takes no claim at all. Used the
correct mechanism in the new code comment and the doc/README updates
instead of repeating the brief's wrong one.
