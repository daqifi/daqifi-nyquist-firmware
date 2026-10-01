#!/usr/bin/env bash
# =============================================================================
# SPEC v6 — prose-versus-verdict census, 30 open firmware rows
# DESIGNED BY nq-b. EXECUTED BY nq-c.
#
# ⛔ v1 FAILED V1 VALIDATION AND nq-c STOPPED RATHER THAN PUBLISHING A CENSUS.
# v2 adopts all three of their repairs plus two more from their hand-read of all
# 16 category-0 rows. Their report: nq-c's
# .claude/audit-artifacts-2026-09-29/fw-census-V1-FAILED-criterion-defect.md
#
# THE v1 DEFECT, confirmed by me before repairing:
#   RE_DISCLAIMED carried the BARE token `noProvenance`, so `noProvenance: false`
#   — provenance PRESENT, audit HEALTHY — matched it. Category 0 outranks the
#   field tokens, so a row was filed as disclosing a degraded audit BECAUSE it
#   documented that its audit was fine. 9 of 30 rows: 965 1008 1020 1027 1048
#   1106 1124 1130 1152 — the nine BEST-evidenced rows in the corpus.
#   **The detector was anti-correlated with evidence quality.**
#
# ⛔⛔ AND THE ASYMMETRY WAS INSIDE ONE PATTERN PAIR I WROTE MINUTES APART:
#   RE_EVIDENCE    gate[[:space:]]*[:=][[:space:]]*.?(PASS|BLOCK)   <- VALUE-bearing
#   RE_DISCLAIMED  noProvenance                                     <- value-BLIND
#   I made the gate arm value-bearing *because* I had just been bitten by the
#   bare form matching the prose "the gate blocks", and then did not apply it to
#   the adjacent token. **A FIELD NAME IS NOT A CLAIM** — and the fix existed in
#   the same file, one variable away.
#
# My prediction that firmware would carry MORE degraded rows than the test-suite
# is NOT SUPPORTED. nq-c: "the abundance is the detector." True size ~4-5 of 30
# against 16 as written — inflated ~3x, and category 0 feeds the degraded-round
# budget ruling, so 16-when-4 would have argued a policy from an artefact.
# =============================================================================
set -u
REPO="daqifi/daqifi-nyquist-firmware"
MODE="${1:---validate}"
RETRIES=4
LANE_TREE="${LANE_TREE:-/mnt/c/daqifi/wt/nq-b}"   # this lane IS the firmware repo; used to VALIDATE candidate SHAs locally

# -----------------------------------------------------------------------------
# A. EVIDENCE — unchanged from v1, which validated clean. FIELD FORM ONLY.
# Anchored on adversarial-audit SCHEMA vocabulary, never on the words "run" or
# "audit": firmware has five objects that read as runs (bench run, Qodo review,
# fix round, adversarial audit, degraded round) and schema is what they do not share.
RE_EVIDENCE='gate[[:space:]]*[:=][[:space:]]*.?(PASS|BLOCK)|gateReason|auditorLegsOk|covered_bytes|total_bytes|rawFindings|arbiterModel|agreed_severity|blindLegRan|unsoundRefutations|dispositions\[|"gate"[[:space:]]*:'
RE_HEAD_ANCHORED='\b[0-9a-f]{40}\b'
RE_EQUALITY='==|\bequals\b|\bmatches\b|headRefOid|head_sha|at head|audited head'
RE_WORKFLOW='\bwf_[A-Za-z0-9_-]{6,}'
RE_PROSE_REF='adversarial|pre-merge audit|audit round|re-audit|audited|blocked:audit|mark-audited|skeptic|arbiter|auditor leg|blind leg'

# -----------------------------------------------------------------------------
# B. REPAIR 1 — EVERY FIELD TOKEN IS NOW VALUE-BEARING. A field name is not a claim.
# REPAIR 3 — the disclaimer vocabulary is AUDIT-ANCHORED, because `degraded`,
#   `needs re-run` and `must be re-run` are ordinary firmware words. nq-c measured
#   fw#1024 firing on "the rate under a sustained DEGRADED LINK is not measured" —
#   a NETWORK LINK. Bare `degraded` and bare `re-run` are REMOVED; both survive
#   only adjacent to an audit noun.
# ⛔ REPAIR 7 (nq-c residual 1, and it CLOSES repair 4 rather than shrinking it).
# Their catch: RE_QODO_DOMINANT='qodo' tested MENTION while my repair-4 rationale
# said SUBJECT — "the same name-is-not-a-claim form as the original noProvenance
# defect, one level down." Correct. Repair 6 (sentence scope) shrank the blast
# radius; it did not fix the predicate.
#
# THE FIX INVERTS IT: instead of VETOING on a Qodo mention, REQUIRE the disclaiming
# sentence to carry an adversarial-audit ANCHOR. That approximates subject directly
# and drops the qodo dependency for the common case.
#   "Do not read the clean QODO state as a clean gate"  -> no anchor  -> excluded
#   "The adversarial-audit.js orchestrator did not run"  -> anchor    -> included
#   "noProvenance: true"                                 -> self-anchoring schema
# A narrow qodo guard survives for the one case the anchor cannot settle: a
# sentence that says "audit" but means Qodo's.
# ⚠ `\baudit` deliberately has NO trailing boundary. My own two-sided test caught the
# miss: `\baudit\b` does not match "auditED" or "auditOR", so "audited head <sha>
# matches" and "the auditor leg confirmed head <sha> equals live" were both REJECTED
# — false negatives in my biased direction, and visible only because I tested a form
# I expected to PASS rather than only forms I expected to fail. (conv-ts's
# exemplary-set rule, applied at the pattern level.) Within a conjunction that also
# requires a 40-hex SHA and an equality token, the looser form is safe.
RE_AUDIT_ANCHOR='adversarial|auditorLegsOk|noProvenance|gateReason|covered_bytes|rawFindings|blindLegRan|hunterLegsIncomplete|codex_exit|\baudit'
RE_QODO_DOMINANT='qodo'
RE_STRONG_ANCHOR='adversarial|auditorLegsOk|noProvenance|gateReason|covered_bytes|rawFindings|blindLegRan|hunterLegsIncomplete|codex_exit|gate[[:space:]]*[:=]'
RE_RANNESS='both legs died|was voided|auditorLegsOk|codex_exit|noProvenance|truncated[^A-Za-z]{0,6}true|\bhunter|\bskeptic|no machine-produced|did run'

# ⭐ AND THE SYNERGY THAT MATTERS: because an in-sentence anchor is now REQUIRED,
# the disclaimer vocabulary can be BROAD without false positives. v2 had to keep it
# narrow precisely because nothing anchored it. So REPAIR 8 (nq-c's missed category
# 0, fw#901) is safe to add:
#   "The sanctioned adversarial-audit.js orchestrator **did not run**"
#   "read this before treating anything here as a pass"
#   "**I am not merging on it**"   "no machine-produced steeringSuppressed"
# RE_DISCLAIMED matched ZERO sentences on that row — a FALSE NEGATIVE in category 0,
# i.e. MY BIASED DIRECTION, and the mirror of the v1 defect: v1 claimed healthy rows,
# v2 missed a disclosed one. Accepted: fw#901 is category 0 -> 7 of 30, PROSE_ONLY 1.
# ⛔ REPAIR 11 (2026-10-01) — THE TWO `[^.]` SPANS ARE REPLACED BY `.`, BECAUSE A
# BOUNDED NEGATED CLASS CANNOT CROSS A DOT AND AUDIT PROSE IS ABOUT FILES WITH DOTS
# IN THEIR NAMES. Reproduced by conv-ts on its own tooling, then here:
#     "Do not read this as a pass"                           [^.]{0,40} -> 1 HIT
#     "Do not read the regression_gate.py result as a pass"  [^.]{0,40} -> 0 MISS
#     same string, dot-tolerant  .{0,40}                                -> 1 HIT
# This is a STRUCTURAL exclusion, not a tuning problem: widening {0,40} can never
# fix it, because the span cannot pass the dot at any width.
#
# ⭐ AND THE SECOND BOUND WAS ALREADY THERE AND CORRECT. `[^.]` was trying to keep
# the span inside one sentence — but the splitter at :194/:326
# (`sed 's/\([.!?]\)[[:space:]]\+/\1\n/g'`) ALREADY does that, and does it RIGHT: it
# splits on a period followed by WHITESPACE, so `regression_gate.py result` stays in
# one sentence while `…as a pass. Next…` splits. So `[^.]` added a second, wrong
# bound on top of a correct one and only ever subtracted matches.
#
# ⚠ FIXED THOUGH THE DIFFERENTIAL IS CLEAN. nq-c ran all 30 census rows: ZERO
# affected, counts unchanged (EVIDENCE 18 · DEGRADED_DISCLOSED 7 · PROSE_ONLY 1 ·
# NO_REFERENCE 3). It is inert at this head only because no disclaimer-shaped
# sentence happens to put a dot inside the span. **A defect that did not bite is not
# a defect that cannot** — the clean differential is a property of the CORPUS, not of
# the PATTERN, and "Do not read the regression_gate.py output as a pass" is a sentence
# someone here will write.
#
# The `[^A-Za-z]` and `[^0-9]` spans are NOT this bug and are deliberately kept: they
# implement "require the FIELD FORM (key + separator + value)", and a dot is neither
# a letter nor a digit, so they cross one freely. Changing them would re-open the
# bare-`noProvenance` false positive that matched `noProvenance: false`.
RE_DISCLAIMED='noProvenance[^A-Za-z]{0,6}true|hunterLegsIncomplete[^A-Za-z]{0,6}true|codex_exit[^0-9]{0,6}[1-9]|auditorLegsOk[^0-9]{0,8}0([^0-9]|$)|truncated[^A-Za-z]{0,6}true|NOT an audit verdict|not an adversarial verdict|remains UN-?AUDITED|row is un-?audited|\bvoid run\b|run was void|was voided|must not be counted as a round|do not read.{0,40}(pass|clean|verdict)|before treating anything.{0,30}as a pass|not merging on it|no machine-produced|did not run|did not come|both legs died|\bdegraded\b'

RE_LANE_NOTE="${RE_LANE_NOTE:-__UNSET__}"
QODO_AUTHOR='^qodo-code-review'

# -----------------------------------------------------------------------------
# C. REPAIR 2 — PRECEDENCE 0 IS SCOPED TO A SINGLE COMMENT.
# v1 grepped the whole joined human stream, so it ANDed across the row: a
# disclaimer in comment A voided a verdict in comment Z. nq-c identified this as
# what made repairs 1 and 3 load-bearing rather than cosmetic.
#
# Category 0 now requires, WITHIN ONE COMMENT: an audit anchor AND a disclaimer
# AND no Qodo subject. Comments are split on a sentinel, never on blank lines.
# -----------------------------------------------------------------------------

# D. REFUSALS. R1 is `retrieved < want` — NOT `!=`, and the asymmetry is
# deliberate: a comment ADDED between the count call and the fetch is benign,
# while FEWER than counted is the dangerous direction. (v1's prose said `!=`
# while its code said `-ge`; nq-c caught the mismatch. Code was right, doc wrong.)
#   R1 retrieved < want               -> that row UNDETERMINED, sweep continues
#   R2 gh empty/non-zero after RETRIES-> that row UNDETERMINED, sweep continues
#   R3 RE_LANE_NOTE unset             -> GLOBAL refusal, do not sweep
#   R4 V1/V2 validation not passed    -> GLOBAL refusal
#   R5 a field needs a judgement call -> STOP and name the field
# An empty body is "no comments" only when .comments == 0; otherwise it is R2.
# (v1's dead `classify()` stub is REMOVED — it was never called.)

SEP=$'\002'
# ⛔⛔ REPAIR 5 — VERIFY THE PAYLOAD, NOT A PARALLEL COUNT.
# v2's first form compared `want` (a count call) against `got` (a SECOND count
# call) and then fetched the BODIES in a THIRD, UNVERIFIED call. MEASURED: fw#1124
# returned NO_REFERENCE once and EVIDENCE on re-run with identical inputs — a
# transient produced a CONFIDENT WRONG CATEGORY instead of UNDETERMINED, which is
# strictly worse than a refusal and is the exact failure R1 exists to prevent.
# **I verified the thing that was easy to count rather than the thing that
# mattered.** Now: fetch ONCE and count the records IN the stream we will classify.
fetch_verified() {   # $1=pr -> author\x01body per comment, SEP-joined; or __REFUSAL__
	local want payload n i
	want=$(gh api "repos/$REPO/issues/$1" --jq '.comments' 2>/dev/null)
	[ -z "$want" ] && { echo "__REFUSAL__"; return; }
	for ((i=0;i<RETRIES;i++)); do
		payload=$(gh api "repos/$REPO/issues/$1/comments" --paginate \
			--jq '.[] | (.user.login) + "" + (.body|gsub("\n";" ")) + ""' 2>/dev/null)
		# count the SEP records in the payload we are about to classify
		n=$(printf '%s' "$payload" | tr -cd "$SEP" | wc -c)
		if [ -n "$payload" ] && [ "$n" -ge "$want" ] 2>/dev/null; then
			printf '%s' "$payload"; return
		fi
		sleep 3
	done
	echo "__REFUSAL__"
}

run_row() {   # $1=pr
	local raw nex=0 c author body hits
	raw=$(fetch_verified "$1")
	[ "$raw" = "__REFUSAL__" ] && { printf '%s\tUNDETERMINED\tR1/R2 fetch unverified\t-\n' "$1"; return; }

	local -a HUMAN=() QODO=()
	while IFS= read -r -d "$SEP" c || [ -n "$c" ]; do
		c=${c#$'\n'}; [ -z "$c" ] && continue
		author=${c%%$'\001'*}; body=${c#*$'\001'}
		if printf '%s' "$author" | grep -qE "$QODO_AUTHOR"; then QODO+=("$body"); continue; fi
		if [ "$RE_LANE_NOTE" != "__UNSET__" ] && printf '%s' "$body" | grep -qE "$RE_LANE_NOTE"; then
			nex=$((nex+1)); continue
		fi
		HUMAN+=("$body")
	done <<< "$raw"

	# 0 DEGRADED_DISCLOSED -- anchor at COMMENT level, disclaimer at SENTENCE level.
	#
	# ⛔ REPAIR 6 — THE QODO VETO IS SCOPED TO THE DISCLAIMER'S OWN SENTENCE.
	# v2's first form vetoed any comment mentioning "qodo" ANYWHERE. MEASURED:
	# fw#1110 is a genuine category 0 (`codex_exit: 124`, `hunterLegsIncomplete:
	# true`, `noProvenance: true`) and its audit-report comment mentions Qodo FIVE
	# times, so the veto killed it. An audit write-up routinely discusses Qodo in
	# the same breath. **I committed object conflation inside the repair for object
	# conflation** — the veto must ask what the DISCLAIMER is about, not what the
	# comment mentions.
	# ⛔ REPAIR 9 (nq-c residual 2): `tr '.' '\n'` is FRAGILE HERE. Firmware comments
	# embed C code, version numbers and dotted filenames, so a disclaiming sentence
	# splits mid-claim and separates an audit noun from its disclaimer — losing a
	# genuine category 0, which is the CONSERVATIVE direction and therefore MY biased
	# direction. Split only on sentence punctuation FOLLOWED BY WHITESPACE, which
	# leaves `adversarial-audit.js orchestrator`, `1.2.3` and `void f(void);` intact.
	local sent
	for body in ${HUMAN+"${HUMAN[@]}"}; do
		printf '%s' "$body" | grep -qiE "$RE_EVIDENCE|$RE_PROSE_REF" || continue
		# sentence-level: disclaimer AND an in-sentence audit anchor (repair 7),
		# minus the narrow qodo-dominant case the anchor cannot settle
		# A blanket qodo veto is residual 1 again -- it is what killed fw#1110, whose
		# audit write-up mentions Qodo five times. So the veto is CONDITIONAL: drop a
		# qodo-mentioning sentence ONLY when it carries no STRONG adversarial anchor.
		# That is the subject test, as close as text allows.
		sent=""
		while IFS= read -r s; do
			[ -z "$s" ] && continue
			printf '%s' "$s" | grep -qiE "$RE_DISCLAIMED"   || continue
			printf '%s' "$s" | grep -qiE "$RE_AUDIT_ANCHOR" || continue
			if printf '%s' "$s" | grep -qiE "$RE_QODO_DOMINANT" \
			   && ! printf '%s' "$s" | grep -qiE "$RE_STRONG_ANCHOR"; then continue; fi
			sent="$s"; break
		done <<< "$(printf '%s' "$body" | sed 's/\([.!?]\)[[:space:]]\+/\1\n/g')"
		[ -z "$sent" ] && continue
		hits=$(printf '%s\n' "$sent" | grep -oiE "$RE_DISCLAIMED" | sort -u | head -2 | tr '\n' ',')
		# ⛔ RULING (a) ON nq-c's NAMED DEFINITIONAL GAP — A SIXTH CATEGORY.
		# fw#1027: "### Gates I did NOT run" / "**No adversarial audit.** Codex
		# high-effort is at capacity pool-wide" — NOTHING RAN, deliberately, with a
		# stated reason. Category 0 is defined as "an audit RAN, produced nothing
		# usable, and the row KNOWS it", so 1027 is a DIFFERENT STATE.
		#
		# nq-c's argument is decisive, and it is my own design rule turned back on me:
		# for the ruling this category feeds, the two states point OPPOSITE ways. A
		# round that never ran plainly does not consume the round budget; whether a
		# DEGRADED round consumes it is the open question. Folding them corrupts
		# exactly the decision the category exists to inform.
		#
		# REJECTED (c) fold-and-subdivide-later: that IS the corruption.
		# REJECTED (b) require a ran-ness token and let bare "did not run" fall
		#   through: a regex fix for a definitional problem, and it fails in the
		#   DANGEROUS direction — 1027 would fall through to EVIDENCE, reading as
		#   "carries checkable audit evidence" when the row says the opposite. That is
		#   the ts#351 error again. nq-c noted (b) happens to split 901 from 1027 by
		#   accident of phrasing; relying on an accident is not a discriminator.
		#
		# DISCRIMINATOR: RAN-NESS, verified by me to split the two rows.
		#   fw#901  "cross-vendor codex hunter (four times, on four different heads),
		#            an independent opus hunter, and two skeptics briefed to refute"
		#           -> machinery RAN (unsanctioned), output exists, declared NOT a
		#              verdict -> category 0. Checked at source, not taken from nq-c.
		#   fw#1027 nothing ran at all -> category 0b.
		# Ran-ness is tested FIRST, so a row disclosing BOTH (901) lands in 0.
		# ⛔ RULING ON nq-c's SUPERSEDED-DISCLOSURE FINDING: A PER-ROW FLAG, NOT A
		# SEVENTH CATEGORY. Their discriminator is better than a timestamp and I
		# verified it: fw#996's disclosing comment cites `d6ded5c16` and `495836d57`,
		# neither of which prefixes the live head `df60bc24b3`, and a later comment
		# carries an Audit PASS at the head that IS live. Head-pinning settles it with
		# no ordering comparison, so it is immune to the 54-second resolution problem
		# that made their timestamp test unusable on 1027.
		#
		# WHY A FLAG AND NOT A CATEGORY — it CROSS-CUTS, and a row has only one category:
		#   * An EVIDENCE row can equally carry a PASS at a stale head.
		#   * The BUDGET ruling is a HISTORICAL ACCOUNTING question — a degraded round
		#     ran and either did or did not consume a round, whether or not a later head
		#     superseded it. A category would erase that.
		#   * The CURRENT-STATE reading ("which rows are blocked now") needs exactly this
		#     qualifier. One bucket cannot serve both; a category forces a false choice
		#     between two orthogonal facts.
		# So: category unchanged, flag appended. 2 of 8 superseded is a caveat the
		# operator needs attached to the number, not a re-partition of it.
		#
		# ⚠ THREE-WAY, because absence has kinds: a disclosure citing NO head cannot be
		# settled mechanically and must NOT be defaulted to CURRENT.
		# ⛔ FLAG FIX 1 (nq-c) — `\b[0-9a-f]{9,40}\b` EXTRACTS NON-SHA TOKENS.
		# Measured on fw#1110: 11 tokens of length 9, 5 of length 10, 1 of length 12,
		# INCLUDING all-digit GitHub comment IDs (5697736359, 5697832981, 5698039550,
		# 5698980187) — every digit is valid hex. A spurious candidate can only flip
		# HISTORICAL -> CURRENT, so the corpus was safe, but **bounded by luck rather
		# than by construction**, which is their phrase and the right objection.
		#
		# ⚠ AND THE OBVIOUS FIX IS WRONG: excluding all-digit tokens would drop a
		# genuinely all-digit short SHA, losing a candidate that could have matched ->
		# HISTORICAL, which for a current-state reading is the PERMISSIVE direction
		# ("the degradation is stale, ignore it"). So guessing by shape errs the wrong
		# way. VALIDATE INSTEAD: this lane's tree IS the firmware repo, so ask git
		# whether each candidate is a real commit. Exact, local, no network.
		#
		# FLAG FIX 2 (nq-c) — the live-head call had NO RETRY, which is why fw#991
		# returned PIN:UNDETERMINED on their sweep (recomputed with retries:
		# PIN:HISTORICAL). It failed safe, but a refusal that a retry would resolve is
		# a refusal the sweep should not be reporting.
		local flag="PIN:none" lh cs raw_cs i
		for ((i=0;i<RETRIES;i++)); do
			lh=$(gh pr view "$1" --repo "$REPO" --json headRefOid --jq .headRefOid 2>/dev/null)
			[ -n "$lh" ] && break
			sleep 3
		done
		raw_cs=$(printf '%s' "$body" | grep -oE '\b[0-9a-f]{7,40}\b' | sort -u)
		cs=""
		while IFS= read -r c; do
			[ -z "$c" ] && continue
			# a candidate counts only if THIS repo knows it as a commit
			[ "$(git -C "$LANE_TREE" cat-file -t "$c" 2>/dev/null)" = "commit" ] && cs="$cs$c"$'\n'
		done <<< "$raw_cs"
		cs=$(printf '%s' "$cs" | sed '/^$/d')
		if [ -n "$cs" ] && [ -n "$lh" ]; then
			flag="PIN:HISTORICAL"
			while IFS= read -r s; do [ -z "$s" ] && continue
				case "$lh" in "$s"*) flag="PIN:CURRENT"; break;; esac
			done <<< "$cs"
		elif [ -z "$lh" ]; then flag="PIN:UNDETERMINED"; fi
		if printf '%s' "$body" | grep -qiE "$RE_RANNESS"; then
			printf '%s\tDEGRADED_DISCLOSED\t%s\texcluded=%s %s\n' "$1" "${hits%,}" "$nex" "$flag"; return
		fi
		printf '%s\tDISCLOSED_NOT_RUN\t%s\texcluded=%s %s\n' "$1" "${hits%,}" "$nex" "$flag"; return
	done

	local all; all=$(printf '%s\n' ${HUMAN+"${HUMAN[@]}"})
	# 1 EVIDENCE
	hits=$(printf '%s\n' "$all" | grep -oiE "$RE_EVIDENCE" | sort -u | head -3 | tr '\n' ',')
	[ -n "$hits" ] && { printf '%s\tEVIDENCE\t%s\texcluded=%s\n' "$1" "${hits%,}" "$nex"; return; }
	# ⛔⛔ REPAIR 10 (nq-c) — THE HEAD-ANCHORED ARM REQUIRED ZERO AUDIT VOCABULARY.
	# Repair 7 made category 0 require an in-sentence audit anchor. This arm, which I
	# described as "unchanged from v1, which validated clean", required none: just a
	# 40-hex SHA AND an equality word, anywhere in the row. MEASURED:
	#   "confirm crc32 equals A7823538 at head <40hex>"  sha=1 eq=1 anchor=0 -> EVIDENCE
	#   "the bench image matches <40hex>"                sha=1 eq=1 anchor=0 -> EVIDENCE
	# And it is NOT synthetic: fw#1092's co-located trigger is bench hardware text —
	# `firmware_crc32  A7823538  <- equals the RECORDED value` — in a comment carrying
	# another lane's board serial. **A BENCH CRC32 CHECK SATISFIED MY STRONGEST-NAMED
	# EVIDENCE FORM.** That is the five-objects trap inside the EVIDENCE arm, and
	# "validated clean in v1" meant validated on synthetic schema strings, never
	# against a corpus where commit SHAs and the word "equals" are everywhere.
	#
	# THIRD TIME IN THIS SPEC I HARDENED ONE ARM AND LEFT ITS NEIGHBOUR: gate
	# value-bearing / noProvenance not; category 0 anchored / this arm not.
	#
	# FIX: same predicate, same scope as repair 7 — SHA, equality AND an audit anchor
	# in ONE SENTENCE. This also makes the MATCHED column self-justifying, which is
	# nq-c's real point: that column is the audit trail for the classification, so a
	# token drawn from a bench check makes even a correct answer unverifiable.
	#
	# ⚠ AND IT MOVES ROWS IN MY BIASED DIRECTION. A row whose only evidence was an
	# unanchored head claim now falls to PROSE_ONLY — the bucket my declared bias
	# inflates. Per the standing rule that is the result to DISTRUST: every PROSE_ONLY
	# row must be hand-read before any count is reported, and that now matters more
	# than it did when PROSE_ONLY was 1.
	local hs=0
	while IFS= read -r s; do
		[ -z "$s" ] && continue
		printf '%s' "$s" | grep -qE "$RE_HEAD_ANCHORED"  || continue
		printf '%s' "$s" | grep -qiE "$RE_EQUALITY"      || continue
		printf '%s' "$s" | grep -qiE "$RE_AUDIT_ANCHOR"  || continue
		hs=1; break
	done <<< "$(printf '%s\n' "$all" | sed 's/\([.!?]\)[[:space:]]\+/\1\n/g')"
	if [ "$hs" = 1 ]; then
		printf '%s\tEVIDENCE\thead-anchored+audit-anchor\texcluded=%s\n' "$1" "$nex"; return; fi
	printf '%s\n' "$all" | grep -qE "$RE_WORKFLOW" && { printf '%s\tEVIDENCE\tworkflow-id\texcluded=%s\n' "$1" "$nex"; return; }
	# 2 QODO_ONLY
	if printf '%s\n' ${QODO+"${QODO[@]}"} | grep -qiE "$RE_EVIDENCE|$RE_PROSE_REF"; then
		printf '%s\tQODO_ONLY\tqodo-authored only\texcluded=%s\n' "$1" "$nex"; return; fi
	# 3 PROSE_ONLY
	hits=$(printf '%s\n' "$all" | grep -oiE "$RE_PROSE_REF" | sort -u | head -3 | tr '\n' ',')
	[ -n "$hits" ] && { printf '%s\tPROSE_ONLY\t%s\texcluded=%s\n' "$1" "${hits%,}" "$nex"; return; }
	printf '%s\tNO_REFERENCE\t-\texcluded=%s\n' "$1" "$nex"
}

# -----------------------------------------------------------------------------
# E. THE RULING nq-c REFUSED, AND IT IS MINE TO MAKE: DO PARK NOTES COUNT AS LANE NOTES?
#
#   ⛔ NO. A PARK NOTE IS NOT A LANE NOTE AND MUST NOT BE EXCLUDED.
#
# A park note is where a lane records the ROW'S STATE, and it routinely carries
# the verdict itself ("parked at the cap, audit BLOCK at X"). Excluding it would
# delete the row's primary audit record and push genuinely evidenced rows toward
# PROSE_ONLY — the direction of my declared bias, which is the reason to be
# strict here rather than convenient.
#
# The exclusion targets HOUSEKEEPING — "base refresh pushed", lane working and
# bench notes — not STATE DECLARATIONS. And a park is the highest-care state
# check anyone performs here, so it is the LAST comment to throw away.
#
# nq-c's supplied pattern already draws the line correctly (refresh + lane/bench
# working notes, park notes NOT matched): 16 excluded of 133 human comments,
# 0 of 3 real verdict posts excluded. **Confirmed as supplied; keep it.**
# -----------------------------------------------------------------------------

# F. OUTPUT: per row PR, category, matched token verbatim, excluded count. Then
# the five counts + UNDETERMINED. No rate interpretation.
# ⛔ HAND-READ EVERY PROSE_ONLY ROW before reporting any count — my bias inflates
# that bucket and the operator conclusion rests on it.
case "$MODE" in
	--validate)
		echo "=== REPAIR 1: value-bearing field tokens"
		for s in 'noProvenance: false' 'noProvenance: true' 'auditorLegsOk: 0 of 1' 'auditorLegsOk 1/1'; do
			printf '  %-26s disclaimed=%s\n' "$s" "$(printf '%s\n' "$s" | grep -ciE "$RE_DISCLAIMED")"
		done
		echo "  (expect 0,1,1,0)"
		echo "=== REPAIR 3: audit-anchored degraded"
		for s in 'the rate under a sustained DEGRADED LINK is not measured' 'the adversarial audit was degraded'; do
			printf '  %-56s disclaimed=%s\n' "$(printf '%.52s' "$s")" "$(printf '%s\n' "$s" | grep -ciE "$RE_DISCLAIMED")"
		done
		echo "  (expect 1,1 -- bare degraded is INTENTIONAL now; the in-sentence ANCHOR does the work, not the vocabulary. 1024 stays EVIDENCE.)"
		echo "=== REPAIR 4: Qodo-scoped veto"
		s='Do not read the clean QODO state as a clean gate'
		printf '  disclaimed=%s  qodo-scoped=%s -> vetoed\n' \
			"$(printf '%s\n' "$s" | grep -ciE "$RE_DISCLAIMED")" "$(printf '%s\n' "$s" | grep -ciE "$RE_QODO_DOMINANT")"
		echo
		echo "V1/V2 still need REAL rows you independently know. I cannot supply them."
		echo "RE_LANE_NOTE: $RE_LANE_NOTE"
		;;
	--run)
		[ "$RE_LANE_NOTE" = "__UNSET__" ] && { echo "REFUSAL R3: export RE_LANE_NOTE (section B)." >&2; exit 3; }
		printf 'PR\tCATEGORY\tMATCHED\tNOTES\n'
		for pr in $(gh pr list --repo "$REPO" --state open --limit 200 --json number --jq '.[].number' 2>/dev/null); do
			run_row "$pr"
		done
		;;
	*) echo "usage: $0 [--validate|--run]" >&2; exit 2 ;;
esac
