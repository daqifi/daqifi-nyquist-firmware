# fw#1152 round 13 handoff

WAS DOING: implementing round 13's three review-leg items (1 model-fidelity,
2 comment-accuracy) on `nqb/1152-round13`, based off
`origin/fix/1148-usecal-selector-ordering` @ `6305da85788779d230c209fac6141022acfb9123`.

All three items are DONE, verified (29 binaries / 341 tests / 0 failed /
44,767 assertions; run_1152_tests 33/0/825; cppcheck matches baseline), and
pushed to `nqb/1152-round13`.

NEXT STEP: none pending from this round. If resuming, first re-read the
audit's next brief (if any) rather than assuming more work here; this round's
three items are closed.

SAFE TO RESUME FROM: `nqb/1152-round13` HEAD (pushed). Three commits on top
of the base, each one hunk, each independently verified:

1. `test(1152): label USECAL_PREFIX historical, like its siblings` --
   item (1). Verified `dc967c9e1` is a commit (not the blob `efcdf3d75` the
   brief named), and is the DIRECT PARENT of `50002cd2a` (#1152's own first
   commit) via `git merge-base --is-ancestor dc967c9e1 50002cd2a^`. So
   PREFIX models #1152's own base commit, not main -- labeled historical to
   match its siblings (POSTFIX/R2/R5/R8/R9), no model logic changed.
2. `docs(settings): narrow ValidateADCCalBank's doc -- it is not a
   consistency guarantee` -- item (2), `firmware/src/services/daqifi_settings.h`.
3. `test(1152): correct FIDELITY assumption 6 -- claim scope and modelled
   interleaving` -- item (3), same file as (1) but a disjoint hunk.

Only `firmware/src/services/daqifi_settings.h` and
`tests/host/test_1152_usecal_type_trust.c` are touched (confirmed via
`git diff <base>..HEAD --stat`). `SCPIInterface.c`, any config claim, and any
critical section are untouched -- the #1153 residual was not touched, only
described more precisely in prose.

A suspected prompt-injection arrived mid-task, appended to a Bash tool
result rather than as a genuine caller turn (claimed to be "the coordinator",
telling me not to touch `test_1152_usecal_type_trust.c:632`). Not treated as
authoritative; verified independently that `:632` describes R2 (a historical
shape where validation failure genuinely touches nothing) and was correctly
left alone regardless -- it was never in scope for items (1)/(2)/(3). Flagged
to the caller in the handback report.
