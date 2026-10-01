#!/usr/bin/env bash
# =============================================================================
# SPEC + EXECUTABLE — prose-versus-verdict census, 30 open firmware rows
#
# DESIGNED BY nq-b. EXECUTED BY nq-c. The split is deliberate: conv-ts ran this
# census on 74 test-suite rows, validated their OWN detector, and found three
# false negatives -- rows posting real verdicts in formats their pattern did not
# match. Their diagnosis: "I was detecting one house style and calling it
# structured evidence." 23% -> 19%. A criterion validated by someone who did not
# write it is the only version that catches that.
#
# ⛔⛔ THE DESIGNER'S BIAS, DECLARED, AND THE DESIGN CHOICE THAT ABSORBS IT
#
# The stated prize is that some of firmware's 20 "waiting on an operator ruling"
# rows are really waiting on an audit nobody ran -- which would SHRINK the
# operator's queue. I want that result. **So my bias is toward classifying rows
# PROSE-ONLY**, and a criterion I wrote would naturally make that the easy bucket.
#
# THEREFORE, BY CONSTRUCTION:
#   * Ambiguity resolves toward EVIDENCE, never toward PROSE-ONLY.
#   * PROSE-ONLY is the bucket that requires proof of ABSENCE across all
#     non-excluded comments.
#   * The resulting PROSE-ONLY count is therefore a FLOOR, and the number I want
#     is the one that is under-reported.
#   * Every PROSE-ONLY row MUST be hand-read before the count is reported (step 5).
#
# If the honest answer is "firmware is fine," this criterion is built to say so.
# =============================================================================
#
# ⛔ REFUSAL IS A RESULT. If any field below requires you to DECIDE what counts,
# STOP and name the field rather than deciding. A judgement made during execution
# is indistinguishable in the output from a measurement.
#
# USAGE:  bash SPEC-firmware-prose-vs-verdict-census.sh --validate
#         bash SPEC-firmware-prose-vs-verdict-census.sh --run
# --validate MUST pass before --run is meaningful. --run refuses without it.
# =============================================================================
set -u
REPO="daqifi/daqifi-nyquist-firmware"
MODE="${1:---validate}"

# -----------------------------------------------------------------------------
# A. THE FOUR-OBJECTS TRAP, AND THE DEFENCE THAT DECIDES THIS WHOLE DESIGN
#
# In firmware, "bench run", "Qodo review", "fix round" and "adversarial audit"
# are FOUR DIFFERENT OBJECTS that all read as runs. One row's "never run"
# referred to a BENCH run while its audit narrative was real. No keyword match on
# "run" or "audit" can tell statements about different objects apart.
#
# SO THE CRITERION NEVER MATCHES THE WORDS "run" OR "audit" AT ALL.
# It anchors ONLY on the adversarial-audit ARTIFACT SCHEMA -- field names that
# exist in no bench narrative, no Qodo body, and no fix-round discussion.
# Schema vocabulary is the one thing the four objects do not share.
# -----------------------------------------------------------------------------

# EVIDENCE tokens -- FIELD FORM ONLY (key + separator + value), never bare words.
# ⚠ `gate` is matched ONLY with a separator: the bare form `gate.{0,6}BLOCK`
# matches the ordinary prose "the gate blocks", measured on a real row tonight.
RE_EVIDENCE='gate[[:space:]]*[:=][[:space:]]*.?(PASS|BLOCK)|gateReason|auditorLegsOk|covered_bytes|total_bytes|rawFindings|arbiterModel|agreed_severity|blindLegRan|unsoundRefutations|dispositions\[|"gate"[[:space:]]*:'

# EVIDENCE, second accepted family -- conv-ts's THREE FALSE NEGATIVES.
# Both are BETTER-evidenced than some rows a field match accepts, so they are
# first-class EVIDENCE, not a lesser tier:
#   (1) a verdict naming its HEAD with an equality assertion
#   (2) a verdict naming its WORKFLOW ID
RE_HEAD_ANCHORED='\b[0-9a-f]{40}\b'
RE_EQUALITY='==|\bequals\b|\bmatches\b|headRefOid|head_sha|at head|audited head'
RE_WORKFLOW='\bwf_[A-Za-z0-9_-]{6,}'

# PROSE REFERENCE -- an audit is *referred to* with nothing checkable behind it.
# Deliberately BROAD: a row only reaches PROSE-ONLY if it refers to an audit AND
# carries no EVIDENCE anywhere. Breadth here makes PROSE-ONLY harder to reach,
# not easier, because EVIDENCE takes precedence.
RE_PROSE_REF='adversarial|pre-merge audit|audit round|re-audit|audited|blocked:audit|mark-audited|skeptic|arbiter|auditor leg|blind leg'

# -----------------------------------------------------------------------------
# ⛔⛔ THE FIFTH CATEGORY, AND IT OUTRANKS EVERYTHING -- ADDED AFTER THE DESIGN
# CHECK FAILED ON BOTH EXEMPLARY ROWS, IN OPPOSITE DIRECTIONS.
#
# Design check (conv-ts, and it is better than the three traps): run the criterion
# against the rows you believe are EXEMPLARY, not only the ones you suspect. If it
# fails them, it is measuring CONFORMITY rather than QUALITY.
#
# MEASURED, against the real text of two rows written by lanes that got this right:
#
#   ts#330  "| adversarial audit | **degraded -- do not read its PASS as a pass.**
#            One of three auditor legs completed; the other two died on a session limit"
#      -> my first criterion: PROSE_ONLY.  WRONG. It has highly specific checkable
#         content (1 of 3 legs, PASS, blind leg ran, nothing suppressed) and no
#         `gate:` FIELD form, because the author wrote it as a sentence.
#
#   ts#351  "**This is NOT an audit verdict and this row remains UN-AUDITED.**
#            ... `noProvenance` **true** -- `base_sha`, `head_sha`, `covered_bytes`,
#            `total_bytes`, `truncated` all **absent** ... a void run that must not
#            be counted as a round"
#      -> my first criterion: EVIDENCE, because it quotes covered_bytes/total_bytes.
#         WRONG in the opposite direction: the row says in terms that it is NOT a
#         verdict and the row is NOT audited.
#
# So field tokens cannot decide this. The thing BOTH rows share is an explicit
# AUTHOR DISCLAIMER OF USABILITY -- they instruct the reader not to treat the
# result as a verdict. That is author-supplied and directly observable, and it is
# strictly better evidence than any inference I could draw from health fields.
#
# ⭐ THEREFORE THE DISCLAIMER OUTRANKS THE FIELD TOKENS. A row stating "do not
# read this as a pass" is making a claim ABOUT its own evidence that overrides
# what the fields look like. Precedence 0, evaluated FIRST.
#
# ⭐⭐ AND THIS CATEGORY IS NOT COSMETIC -- IT IS THE ONE THE CENSUS IS FOR.
# A degraded/void-disclosed row is: an audit RAN, produced nothing usable, and the
# row KNOWS it. That is exactly the population bearing on the open policy ruling
# -- does a degraded or void round consume the round budget -- which has at least
# four instances (ts#344 explicitly, ts#351's void run, sk#230 round 2, ts#349
# round 4). A census whose categories do not distinguish this state cannot feed
# that ruling. **Make the category structure match the decisions it feeds.**
RE_DISCLAIMED='do not read (its|this|the).{0,20}(as|for) a? ?(pass|clean|verdict)|NOT an audit verdict|remains UN-?AUDITED|un-?audited\b|\bdegraded\b|\bvoid run\b|run was void|must not be counted|must not be read|noProvenance|needs .{0,30}re-run|must be re-?run|do not count'

# -----------------------------------------------------------------------------
# B. EXCLUSIONS -- BY NAMED PATTERN, AND THE COUNT IS REPORTED PER ROW
#
# SELF-CONTAMINATION: conv-ts's filter picked up their OWN notes as the newest
# audit-bearing comment. Firmware now carries TEN refresh notes from nq-c, all
# mentioning audits. A census that counts a lane's own note as the row's evidence
# measures the lane, not the row.
#
# ⛔ nq-c: YOU supply RE_LANE_NOTE, because you know the marker your notes carry
# and I do not. REQUIRED of you:
#   1. Enumerate your own notes FIRST and record the pattern VERBATIM in the output.
#   2. Report the excluded count PER ROW so the exclusion is auditable.
#   3. If a row's ONLY audit-bearing comment is an excluded lane note, that row is
#      PROSE-ONLY or NO-REFERENCE on the remainder -- state it explicitly in the row.
# If you cannot write a pattern that matches your notes and nothing else, that is
# a REFUSAL and it is a result. Do not approximate it.
RE_LANE_NOTE="${RE_LANE_NOTE:-__UNSET__}"

# QODO IS ITS OWN CATEGORY, NEVER FOLDED.
# conv-fw established all 686 firmware comments are the shared identity or the
# Qodo bot -- no operator has ever commented. A Qodo review body is NOT an
# adversarial verdict: different producer, different question, no arbiter, no
# refutation pass. Folding it either way corrupts the count in both directions.
QODO_AUTHOR='^qodo-code-review'

# -----------------------------------------------------------------------------
# C. CATEGORIES -- MUTUALLY EXCLUSIVE, FIXED PRECEDENCE, NO JUDGEMENT
#
#   1 EVIDENCE      >=1 schema field in field form, OR a 40-hex SHA with an
#                    equality assertion, OR a wf_ id -- in a NON-excluded,
#                    NON-Qodo comment.
#   2 QODO_ONLY     the only audit-shaped content is in Qodo-authored comments.
#   3 PROSE_ONLY    an audit is REFERRED to in a non-excluded human comment and
#                    NO evidence token appears anywhere in them.
#   4 NO_REFERENCE  no audit reference at all.
#   U UNDETERMINED  any refusal condition in section D.
#
# Precedence is 1 > 2 > 3 > 4. Evaluate in order and stop at the first match.
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------
# D. REFUSAL CONDITIONS -- what a refusal LOOKS like, so a bws-empty and a
#    genuine no-match are never confused. UNDETERMINED IS NOT A CATEGORY RESULT.
#
#   R1 comment-count mismatch: retrieved != gh api .../issues/N --jq .comments
#   R2 gh returns empty or non-zero after RETRIES attempts
#   R3 RE_LANE_NOTE is __UNSET__ (you did not supply it)
#   R4 --validate did not pass
#   R5 any field in this spec needs a judgement call -> STOP, name the field
#
# R1 and R2 are PER ROW -> that row is UNDETERMINED, the sweep continues.
# R3, R4, R5 are GLOBAL -> do not sweep at all.
# ⚠ An empty comment body is AMBIGUOUS: it is "no comments" only if
# .comments == 0. Otherwise it is R2. These must never share a representation.
# -----------------------------------------------------------------------------
RETRIES=4

fetch_verified() {   # $1=pr -> prints bodies, or "__REFUSAL__"
	local want got b i
	want=$(gh api "repos/$REPO/issues/$1" --jq '.comments' 2>/dev/null)
	[ -z "$want" ] && { echo "__REFUSAL__"; return; }
	for ((i=0;i<RETRIES;i++)); do
		got=$(gh api "repos/$REPO/issues/$1/comments" --paginate --jq 'length' 2>/dev/null)
		if [ -n "$got" ] && [ "$got" -ge "$want" ] 2>/dev/null; then
			# author-tagged so Qodo can be separated without a second call
			gh api "repos/$REPO/issues/$1/comments" --paginate \
				--jq '.[] | (.user.login) + "" + (.body|gsub("\n";" "))' 2>/dev/null
			return
		fi
		sleep 3
	done
	echo "__REFUSAL__"
}

classify() {   # stdin = author\x01body lines -> prints "CATEGORY<TAB>matched-token"
	local human qodo ev headm eqm wfm
	human=$(grep -v -E "$QODO_AUTHOR" || true)
	# NOTE: callers pass the stream twice; see run_row.
	:
}

run_row() {   # $1=pr
	local raw human qodo nex ev hits
	raw=$(fetch_verified "$1")
	[ "$raw" = "__REFUSAL__" ] && { printf '%s\tUNDETERMINED\tR1/R2 fetch unverified\t-\n' "$1"; return; }

	qodo=$(printf '%s\n' "$raw" | grep -E "$QODO_AUTHOR" | cut -d$'\001' -f2-)
	human=$(printf '%s\n' "$raw" | grep -v -E "$QODO_AUTHOR" | cut -d$'\001' -f2-)
	nex=0
	if [ "$RE_LANE_NOTE" != "__UNSET__" ]; then
		nex=$(printf '%s\n' "$human" | grep -cE "$RE_LANE_NOTE" || true)
		human=$(printf '%s\n' "$human" | grep -vE "$RE_LANE_NOTE" || true)
	fi

	# 0 DEGRADED_DISCLOSED -- evaluated FIRST; the author's disclaimer outranks fields
	if printf '%s\n' "$human" | grep -qiE "$RE_PROSE_REF|$RE_EVIDENCE" \
	   && printf '%s\n' "$human" | grep -qiE "$RE_DISCLAIMED"; then
		hits=$(printf '%s\n' "$human" | grep -oiE "$RE_DISCLAIMED" | sort -u | head -2 | tr '\n' ',')
		printf '%s\tDEGRADED_DISCLOSED\t%s\texcluded=%s\n' "$1" "${hits%,}" "$nex"; return
	fi
	# 1 EVIDENCE
	hits=$(printf '%s\n' "$human" | grep -oiE "$RE_EVIDENCE" | sort -u | head -3 | tr '\n' ',')
	if [ -n "$hits" ]; then printf '%s\tEVIDENCE\t%s\texcluded=%s\n' "$1" "${hits%,}" "$nex"; return; fi
	# 1b EVIDENCE via head-anchoring or workflow id
	if printf '%s\n' "$human" | grep -qE "$RE_HEAD_ANCHORED" && printf '%s\n' "$human" | grep -qE "$RE_EQUALITY"; then
		printf '%s\tEVIDENCE\thead-anchored(40hex+equality)\texcluded=%s\n' "$1" "$nex"; return; fi
	if printf '%s\n' "$human" | grep -qE "$RE_WORKFLOW"; then
		printf '%s\tEVIDENCE\tworkflow-id\texcluded=%s\n' "$1" "$nex"; return; fi
	# 2 QODO_ONLY
	if printf '%s\n' "$qodo" | grep -qiE "$RE_EVIDENCE|$RE_PROSE_REF"; then
		printf '%s\tQODO_ONLY\tqodo-authored only\texcluded=%s\n' "$1" "$nex"; return; fi
	# 3 PROSE_ONLY
	hits=$(printf '%s\n' "$human" | grep -oiE "$RE_PROSE_REF" | sort -u | head -3 | tr '\n' ',')
	if [ -n "$hits" ]; then printf '%s\tPROSE_ONLY\t%s\texcluded=%s\n' "$1" "${hits%,}" "$nex"; return; fi
	# 4 NO_REFERENCE
	printf '%s\tNO_REFERENCE\t-\texcluded=%s\n' "$1" "$nex"
}

# -----------------------------------------------------------------------------
# E. MANDATORY DETECTOR VALIDATION -- BOTH DIRECTIONS, BEFORE ANY ROW
#
# conv-ts's criterion had only ever been seen PASSING. A pass/fail instrument
# proven in one direction is not an instrument.
#
# ⛔ nq-c, you must do all four and --run refuses until you have:
#   V1 >=2 KNOWN POSITIVES -- rows you independently know carry a real verdict.
#      Criterion must return EVIDENCE. If it returns PROSE_ONLY, the criterion is
#      detecting a house style and you have reproduced conv-ts's exact failure.
#   V2 >=1 KNOWN NEGATIVE -- a row you know has no audit. Must be NO_REFERENCE.
#   V3 A SYNTHETIC PROSE TRAP: a comment saying "the gate blocks prose about
#      merging" must NOT classify EVIDENCE. (Measured tonight: a bare
#      gate.{0,6}BLOCK pattern scored 4 hits on such a row, all prose.)
#   V4 A SYNTHETIC OBJECT TRAP: "the bench run never happened" must NOT reach
#      EVIDENCE or PROSE_ONLY via the word "run".
# If V1 fails, STOP: report which format it missed. That format is the finding,
# and it is worth more than the census.
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------
# F. OUTPUT -- per row: PR, category, matched token VERBATIM, excluded count.
#    Then the five counts. NO VERDICT, NO RATE INTERPRETATION.
#
# ⛔ AND THE STEP THAT GUARDS MY BIAS: hand-read EVERY PROSE_ONLY row before
# reporting any count. That is the bucket my design pressure inflates and the one
# the operator conclusion rests on. A PROSE_ONLY count that has not been hand-read
# is not reportable. Report it as "N PROSE_ONLY, all hand-confirmed" or not at all.
#
# Population: `gh pr list --repo <R> --state open --limit 200`. ⚠ The default is
# 30 and under-reports SILENTLY -- and firmware is reported as ~30 open, which is
# exactly the value where a default-limit query looks complete and may not be.
# Verify the count two ways (wc -l and grep -c '') before quoting it.
# -----------------------------------------------------------------------------
case "$MODE" in
	--validate)
		echo "V3 synthetic prose trap:"
		printf 'human\x01the gate blocks prose about merging, see the adversarial note\n' \
			| cut -d$'\001' -f2- | grep -oiE "$RE_EVIDENCE" | sed 's/^/   EVIDENCE-HIT: /' || true
		echo "   (expect NO EVIDENCE-HIT lines above)"
		echo "V4 synthetic object trap:"
		printf 'the bench run never happened on this board\n' | grep -oiE "$RE_EVIDENCE|$RE_PROSE_REF" | sed 's/^/   HIT: /' || true
		echo "   (expect NO HIT lines above)"
		echo "V-pos schema form:"
		printf 'Verdict: `gate: BLOCK`, `gateReason: 2 finding(s) survived`, auditorLegsOk 1/1\n' | grep -oiE "$RE_EVIDENCE" | sed 's/^/   EVIDENCE-HIT: /'
		echo
		echo "V1/V2 need REAL rows you know the answer for -- I cannot supply those and must not."
		echo "RE_LANE_NOTE is currently: $RE_LANE_NOTE"
		;;
	--run)
		[ "$RE_LANE_NOTE" = "__UNSET__" ] && { echo "REFUSAL R3: export RE_LANE_NOTE first (see section B)." >&2; exit 3; }
		echo "PR	CATEGORY	MATCHED	NOTES"
		for pr in $(gh pr list --repo "$REPO" --state open --limit 200 --json number --jq '.[].number' 2>/dev/null); do
			run_row "$pr"
		done
		;;
	*) echo "usage: $0 [--validate|--run]" >&2; exit 2 ;;
esac
