# SEALED independent read — ts#349's two judgement calls

Written **before** `wf_73770fec-5c6` returned anything. The point of sealing: without it I would
read the audit's output as agreement with a view I had not yet formed
([[feedback_an_endorsement_returning_to_its_author_is_not_corroboration]]). The artifact may
falsify any of this; that is what it is for.

Sources: `test_1018_982_loss_summary.py` at `46aebf10f4606f81bdbb9ec0fa8205c255b08fc8` (read from
the object DB via an idle checkout — the r4 tree the audit owns was never touched);
`SCPIStorageSD.c` in the nq-b lane tree at `608a82092`; `device-guard.sh` behaviour verified
2026-08-24 and re-confirmed wired as a `Bash` PreToolUse hook in `~/.claude/settings.json`.

---

## (b) THE CONDITIONAL `--expect-serial` — a finding against the coordinator's ruling

The decision may well be right. **The safety story attached to it is false**, and the coordinator
asked to have that stated as a finding against their approval rather than softened.

The code says (`:1274-1280`):

> *"What still protects that: the caller chooses the port, and the bench device-guard refuses a
> command naming a board this lane does not own. The serial check is the second layer, not the only
> one."*

**[V] The bench device-guard cannot be that layer, for two independent reasons:**

1. **Wrong execution context.** It is a **PreToolUse hook on `tool_name == Bash`** — it exits for
   any other tool. `release_gate.py` spawning a test as a subprocess is not an agent Bash call, and
   even when an agent launches the gate, the hook sees the **gate's** command line, not the
   per-test invocations the gate creates internally. **In the exact caller that motivated the
   weakening, the guard never fires on this test's invocation at all.**
2. **Wrong hazard even when it does fire.** It matches the **port name in the command text**
   against the registry. It has no notion of which board is on that port — verified 2026-08-24,
   where a `cat` heredoc whose *prose* named the port was blocked. It cannot detect that the board
   on COM8 is no longer `7E2873046200E891`.

> **The two "layers" are not layers. The guard checks the NAME YOU TYPED; `--expect-serial` checks
> THE BOARD THAT ANSWERED.** They fail together on the one failure mode that matters — a
> re-cabled or re-enumerated bench — which is the failure CLAUDE.md records a 30-minute false
> bisect for, under the standing rule *"never identify a board by port number alone."*

**So for gate runs, wrong-board detection is not second-layer. It is ABSENT.** The note printed at
`:1282-1284` says the check "is NOT active" and then names those two protections, which makes the
printed reassurance the wrong half of the sentence.

### The third remedy neither party considered

The coordinator offered two: supply the serial, or derive it from `*IDN?`. nq-c **correctly** killed
the second — comparing a board to the serial just read off that same board is an assertion that
cannot fail. But "derive it" was rejected as a class when only one *source* was circular.

> **The lane registry `~/.claude/bench/devices.conf` is an INDEPENDENT source** — CLAUDE.md calls
> it authoritative and it maps COM → firmware serial. Comparing `*IDN?` against **the registry's**
> expected serial for the chosen port is a real check with two independent sources, and it is
> exactly what the gate cannot currently do.

**Cost, stated rather than glossed:** it couples a test-suite test to station-local, box-local
agent tooling that is not in the repo. The cleaner placement is for **the gate** to resolve the
serial from the registry and pass `--expect-serial`, keeping the test repo-pure — which puts the
fix in `release_gate.py` / the manifest, i.e. **its own row, not this PR.**

**Predicted disposition:** not a blocker for ts#349's code; a **correction to the rationale** plus
a new row. I expect the audit NOT to raise this, because it is a claim about a comment's safety
argument and about tooling outside the diff — a **subject-scope** blind spot, not a defect in the
change.

---

## (a) `_configure_common`'s UNVERIFIED SD disable — judgement SUPPORTED, word wrong

`:349` `scpi.command("SYST:STOR:SD:ENAble 0", 0.5)`, return unchecked.

**[V] nq-c's load-bearing premise is TRUE.** `SCPI_StorageSDEnableSet` (`SCPIStorageSD.c:468`)
writes `pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE` at **`:506`, after the if/else, on
BOTH branches.** So a later `SD:ENAble 1` really does re-establish `MODE_NONE`, and a silently
failed top-of-run disable really is re-converged by `_arm_sd_logging`. **I verified this in firmware
rather than accepting the file's own comment** — the claim was N-class (the author's note about
firmware) and is now V-class.

⚠ **But "benign" is the wrong word, and the reason is in the same function.** The **disable** path
is guarded: `:490` `if (sd_card_manager_IsBusy())` → error, `goto __exit_point`, **returning without
ever reaching `:506`.** So the disable is refused *precisely when an operation is in flight* — and
the recovery route, `SD:ENAble 1`, stores `mode = MODE_NONE` with **no IsBusy check and while
holding no claim.**

That matters because `SD_ArmOrRefuseWithCleanup` (`:260-290`) is emphatic that the `mode` clear must
happen **under the claim**, since past the release it is *"an unowned write and can land on the NEXT
owner's state"* (#955).

> **So the test's "benign" path recovers via a firmware write of the shape this codebase fixed #955
> to eliminate.** Not "harmless" — "recovers by a route that is itself unguarded."

**⛔ MY OWN CORRECTION, recorded because the first version of this claim was wrong.** I initially
read `:275` (*"silently killed an operation that had legitimately armed"*) as a general statement
that writing `MODE_NONE` over a live operation kills it. **It is not.** Reading the enclosing
function showed it is specifically about #955's *unowned cross-transport store*, a window that was
then closed. My conclusion survives in a weaker, better-grounded form; the citation I first reached
for did not. **Caught by the read-the-enclosing-function rule, which is exactly what it is for.**

**Counter-argument I must state, because it may defeat this entirely:** the enable path looks
*deliberately* unguarded — `#589` at `:482-487` makes manual re-enable the quarantine escape hatch,
and `SCPIInterface.c:5027` calls `SYST:STOR:SD:ENAble` *"the one escape hatch."* An escape hatch
that took IsBusy into account would not be one. **If that is the intent, this is by design and my
observation is void.**

**Predicted disposition:** **NOT a ts#349 defect.** nq-c's judgement stands; their wording
overstates it. The asymmetry is **pre-existing firmware**, out of this PR's scope, and worth a
firmware row only after the escape-hatch question is settled — which needs the author of #589, not
an audit of a test PR.

---

## NOT VERIFIED — stated so the artifact can settle them rather than my silence doing it

1. Whether `release_gate.py` in fact passes only port + `extra_args` (I verified the **manifest
   entry** carries no `extra_args` — `63ff59d` — but not the gate's invocation code).
2. Whether HOW_WE_TEST's WSLENV clearing actually kills the `$DAQIFI_EXPECT_SERIAL` route. **If the
   gate runs WSL-native python, WSLENV is irrelevant and the env route survives** — which would
   partly restore the premise for making the check conditional. Unchecked, and it cuts against my
   own reading, so it is the one I most want tested.
3. Whether `test_861` / `test_851` really implement the cited convention.
4. Whether any early check in this test asserts SD-off state, which a failed disable would turn
   into a false FAIL rather than a benign no-op.

---

# MEASURED AFTER SEALING (appended, nothing above edited)

All four open items settled from source. Two confirm nq-c, one refutes a premise, one was my own
search error.

**(1) CONFIRMED [V].** `release_gate.py:317-318` is
`subprocess.run([sys.executable, "-u", t.script, args.port, *t.extra_args])` — port + extra_args,
nothing else. nq-c's premise is exactly right.

**(3) CONFIRMED [V], after my own miss.** `test_861` exists as
`test_861_stop_races_start_prearm.py` — **my first grep used a filename I invented
(`test_861_sd_space_after_delete.py`) and returned zero.** That was my error, not an absence; third
time this session a search pattern of mine failed against reality rather than the world being
empty. Both cited siblings do implement the convention.

**And the population is worth more than the citation.** Of the ~93 manifest-registered tests,
**14 implement any `--expect-serial` check; 79 have none at all.** Every one of the 13 others uses
the conditional form. So:

> **nq-c's decision is CORRECT and conformant** — their file genuinely was the outlier in
> hard-requiring it, and "conditional when present" is the real convention, 13/13.
>
> **And the safety story is worse than I sealed, not better:** 79 manifest tests already run with
> no wrong-board identity check. Making this one conditional brought it INTO LINE with a convention
> that is itself unprotected. That is a **fleet property, not a ts#349 regression** — which is the
> honest framing and it removes any basis for blocking this PR over it.

**⛔ (2) REFUTED — the premise is too strong, and this is the finding that most undercuts "the gate
cannot supply it."**

- `:1241` `ap.add_argument('--expect-serial', default=os.environ.get('DAQIFI_EXPECT_SERIAL'))` —
  the test **already reads the env var** as its argparse default.
- `release_gate.py:317` calls `subprocess.run` with **no `env=`**, so the child **inherits the
  gate's environment.**

> **[I, over two verified facts] If `DAQIFI_EXPECT_SERIAL` is present in the gate process's
> environment, the test receives it — `extra_args` is irrelevant to that path.** So supplying the
> serial to gate runs may need **no code change at all**, only configuration.

What I deliberately do NOT claim: I have **not** established WSLENV's exact semantics for this
hop, and `WSLENV=` in `HOW_WE_TEST.md:102` is plainly deliberate. So I am not asserting the env
route works — I am asserting that **"an env route does not survive" was never tested**, and the
whole weakening rests on it. The experiment is one line: set the var, run the gate, print what the
test received. **Nobody ran it.**

## The prediction I am sealing

The audit finds **neither** of these two. Both are claims about *rationale* and about *tooling
outside the diff*, and a hunter reading `base..head` has no access to `device-guard.sh`,
`release_gate.py` or `SCPIStorageSD.c`. **If the audit is silent on both, that silence is NOT
agreement** — it is the subject-scope bound, and this file is what keeps me from misreading it as
confirmation.
