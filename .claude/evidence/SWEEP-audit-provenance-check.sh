#!/usr/bin/env bash
# audit-provenance sweep -- does a PASS artifact's head_sha resolve in the PR's OWN repo?
#
# Catches the still-live fail-open in adversarial-audit.js:
#   :77  const REPO = A.repo || 'ORG/REPO'      <- default when `repo` is not passed
#   :84  shape regex that 'ORG/REPO' SATISFIES  <- correctly owner/name shaped
#   :114 const REPO_PATH = A.repoPath || '.'    <- this then picks the repo actually diffed
# Result: a confident artifact, gate PASS, over a repository the PR has nothing to do with.
# One was found 2026-10-01 covering 369,515 bytes of an unrelated repo's documentation.
#
# ⛔⛔ THE ONE DESIGN POINT THAT MATTERS, AND THE OBVIOUS VERSION GETS IT WRONG:
#
#   THE INTENDED REPO MUST COME FROM OUTSIDE THE ARTIFACT.
#
# The artifact's own `repo` field is THE SUSPECT FIELD. Checking head_sha against
# artifact.repo is two fields from one source agreeing -- it reports OK for a
# wrong-repo audit, because base, head and repo were all resolved together against
# the same wrong checkout. So `repo` is passed in by the CALLER, from the PR.
# (If you pass the artifact's own repo, this script tells you nothing. It refuses to
# read that field at all, deliberately.)
#
# USAGE -- one PR per invocation, repo stated explicitly:
#   bash SWEEP-audit-provenance-check.sh <owner/repo> <artifact.json> [more.json ...]
# e.g.
#   bash SWEEP-audit-provenance-check.sh daqifi/daqifi-python-test-suite .claude/evidence/audit-ts*.json
#
# OUTCOMES -- four, never collapsed to two. A 422 and a 503 are both "no answer"
# and only one of them means wrong-repo:
#
#   OK           HTTP 200  -- head_sha exists in the stated repo. Provenance OK ON THIS AXIS ONLY.
#   WRONG_REPO   HTTP 422  -- "No commit found for SHA". THE FINDING.
#   UNDETERMINED HTTP 404  -- repo missing or no access. NOT a wrong-repo result.
#   UNDETERMINED no status line, or any other code (401/403/5xx/network/auth/rate-limit)
#   UNDETERMINED the artifact carries no head_sha at all
#
# ⚠ UNDETERMINED IS NOT A PASS. It means the instrument did not answer. Re-run it;
# do not record it as clean. This script exits 0 only when every row is OK.
#
# ⛔⛔ KNOWN GAP, STATED BECAUSE I HIT IT MYSELF WITHIN THE HOUR:
#
#   THIS CHECKS THE HEAD. IT DOES NOT CHECK THE RANGE.
#
# A correct `head_sha` in the correct repo with a WRONG `base_sha` still produces a
# confident audit of the wrong scope. On 2026-10-01 I ran an audit with base set to
# the merge-base of a branch that was 176 commits divergent: the range was 269 files
# and +93,068 lines where the PR's own diff was 3 files and +50. Every field this
# script inspects was correct. The audit timed out at 12.2% coverage and returned
# a BLOCK whose reason was "audit did not run".
#
# So read the two extra columns below and apply judgement they cannot apply for you:
#   TOTAL_BYTES  -- compare against the PR's real diff size: `gh pr diff <N> | wc -c`.
#                   An order-of-magnitude gap means the range is wrong even when the
#                   head is right. (4,913,643 vs ~3,000 was the case above.)
#   BASE_SHA     -- should be the PR's base or its merge-base, NOT an ancestor far
#                   behind it. `git rev-list --count <base>..<head>` should be the
#                   PR's commit count, not the branch's whole divergence.
#
# Nothing in adversarial-audit.js validates that base..head is the PR's range — the
# caller's scoping is trusted, the same way `repo` is. The head check closes one axis.

set -u

REPO="${1:?usage: $0 <owner/repo> <artifact.json> [...]}"; shift
case "$REPO" in
	ORG/REPO) echo "refusing: 'ORG/REPO' is the placeholder, not a repo" >&2; exit 2 ;;
	*/*) ;;
	*) echo "refusing: '$REPO' is not owner/name" >&2; exit 2 ;;
esac
[ $# -ge 1 ] || { echo "usage: $0 <owner/repo> <artifact.json> [...]" >&2; exit 2; }

# ---- CALIBRATION: prove BOTH directions before trusting any row ------------
# A pass/fail instrument that has only been seen passing is not an instrument.
# Known-positive: the stated repo's default-branch head MUST resolve -> expect 200.
http_status_once() {   # $1=repo $2=sha -> prints the numeric HTTP status, or nothing
	local out
	out=$(gh api -i "repos/$1/commits/$2" 2>&1)
	# NOTE: deliberately not `|| echo`, and no `$?` after a pipeline -- both of those
	# turn "could not run" into a verdict. An empty status means NO ANSWER.
	printf '%s\n' "$out" | sed -n '1s@^HTTP/[0-9.]* \([0-9][0-9][0-9]\).*@\1@p' | head -1
}

# ⛔⛔ ONE CALL CANNOT BOOK A WRONG_REPO, AND THIS IS A MEASURED CORRECTION.
# A genuine wrong-repo returns 422 on every attempt. The FLAKE is an intermittent
# EMPTY status (no HTTP line at all) -- measured by conv-fw at 20% on one row with
# rate_limit showing 5000/5000, and by nq-a at 40% across five. A single sample
# therefore cannot distinguish "not in this repo" from "the call did not land", and
# the dangerous direction is booking a FALSE FINDING against a sound artifact.
# (That happened: a lane booked a wrong-repo on one sample, then constructed an
# explanation for it with contradicting evidence in the same output.)
#
# So: sample until CONSENSUS. Need N_AGREE identical non-empty statuses; empties are
# retried, never counted as a verdict. Disagreement is UNDETERMINED, not a vote.
N_AGREE=3
N_TRIES=6
http_status() {   # $1=repo $2=sha -> prints "<status> <agree>/<nonempty>" or " 0/<n>"
	local s i empty=0 nonempty=0
	declare -A seen=()
	for ((i=0; i<N_TRIES; i++)); do
		s=$(http_status_once "$1" "$2")
		if [ -z "$s" ]; then empty=$((empty+1)); sleep 1; continue; fi
		nonempty=$((nonempty+1))
		seen[$s]=$(( ${seen[$s]:-0} + 1 ))
		[ "${seen[$s]}" -ge "$N_AGREE" ] && { printf '%s %s/%s' "$s" "${seen[$s]}" "$nonempty"; return; }
	done
	# no status reached consensus
	local best='' bestn=0 k
	for k in "${!seen[@]}"; do [ "${seen[$k]}" -gt "$bestn" ] && { best=$k; bestn=${seen[$k]}; }; done
	printf '%s %s/%s' "${best:-}" "$bestn" "$nonempty"
}

KNOWN_GOOD=$(gh api "repos/$REPO" --jq '.default_branch' 2>/dev/null)
if [ -z "$KNOWN_GOOD" ]; then
	echo "CALIBRATION FAILED: cannot read repos/$REPO -- every row below would be UNDETERMINED." >&2
	echo "Fix access/network first. Reporting nothing rather than a sweep of unknowns." >&2
	exit 3
fi
read -r POS POSA <<<"$(http_status "$REPO" "$KNOWN_GOOD")"
read -r NEG NEGA <<<"$(http_status "$REPO" "0000000000000000000000000000000000000000")"
printf 'CALIBRATION  known-good(%s)=%s [%s]   known-bad(40 zeros)=%s [%s]   consensus=%s\n' \
	"$KNOWN_GOOD" "${POS:-NO_ANSWER}" "$POSA" "${NEG:-NO_ANSWER}" "$NEGA" "$N_AGREE"
if [ "${POS:-}" != "200" ] || [ "${NEG:-}" = "200" ] || [ -z "${NEG:-}" ]; then
	echo "CALIBRATION FAILED: the instrument does not distinguish present from absent," >&2
	echo "or could not reach consensus. STOPPING rather than sweeping unknowns." >&2
	exit 3
fi
echo "CALIBRATION OK -- both directions proven at consensus $N_AGREE. Proceeding."
echo

# ---- optional RANGE test -----------------------------------------------------
# ⛔ THE BYTE TOLERANCE IS WITHDRAWN. Two tolerances were fitted and both falsified:
# a percentage band turned out to measure diff size, and a |delta| <= 16 B
# replacement failed on a real corpus (+48, +14, -40 on SOUND artifacts -- both
# signs, not fixed, not per-file). The residual is UNMODELLED: nobody has yet
# identified what the audit counts that `git diff | wc -c` does not. A tolerance
# fitted to agreeing samples without a mechanism is a guess wearing a number.
#
# What survives is an ORDER-OF-MAGNITUDE RATIO test, which stays correct whatever
# the residual turns out to be: observed signal ~9x (and 242x in the worst case),
# observed noise ~0.1%. Bounds [0.5, 2.0] -- a factor of two either way is still
# "same order", and nothing legitimate has come close to leaving it.
RATIO_LO=0.5
RATIO_HI=2.0
PR_BYTES=""
if [ "${1:-}" = "--pr" ] && [ -n "${2:-}" ]; then
	PRN="$2"; shift 2
	PR_BYTES=$(gh pr diff "$PRN" --repo "$REPO" 2>/dev/null | wc -c)
	[ "${PR_BYTES:-0}" -gt 0 ] \
		&& echo "RANGE REFERENCE  gh pr diff $PRN | wc -c = $PR_BYTES bytes; flagging total_bytes outside [${RATIO_LO}x, ${RATIO_HI}x]" \
		|| { echo "RANGE REFERENCE unavailable for PR $PRN -- range column reported, NOT judged." >&2; PR_BYTES=""; }
	echo
fi

# ---- the sweep -------------------------------------------------------------
rc=0
printf '%-46s %-12s %-11s %-6s %s\n' ARTIFACT BASE_SHA TOTAL_BYTES GATE VERDICT
for f in "$@"; do
	[ -f "$f" ] || { printf '%-46s %-12s %-11s %-6s %s\n' "$(basename "$f")" - - - "UNDETERMINED (not a file)"; rc=1; continue; }
	# handle BOTH envelope shapes: workflow-wrapped (.result) and unwrapped
	read -r HEAD GATE BASE TOT <<EOF
$(jq -r '(.result // .) | ((.head_sha // "-") + " " + (.gate // "-") + " " + (.base_sha // "-") + " " + ((.total_bytes // "-")|tostring))' "$f" 2>/dev/null)
EOF
	if [ -z "${HEAD:-}" ] || [ "$HEAD" = "-" ] || [ "$HEAD" = "null" ]; then
		printf '%-46s %-12s %-11s %-6s %s\n' "$(basename "$f")" "${BASE:0:12}" "${TOT:--}" "${GATE:--}" "UNDETERMINED (no head_sha)"
		rc=1; continue
	fi
	read -r S AGREE <<<"$(http_status "$REPO" "$HEAD")"
	case "${S:-}" in
		200) V="OK [$AGREE]" ;;
		422) V="*** WRONG_REPO -- head_sha is not in $REPO [$AGREE consistent]" ; rc=1 ;;
		404) V="UNDETERMINED (404: repo missing or no access) [$AGREE]" ; rc=1 ;;
		"")  V="UNDETERMINED (no consensus in $N_TRIES tries: network/auth/rate-limit)" ; rc=1 ;;
		*)   V="UNDETERMINED (HTTP $S) [$AGREE]" ; rc=1 ;;
	esac
	# RANGE test, only when a reference exists. Order of magnitude, not a tolerance.
	if [ -n "$PR_BYTES" ] && [ "$TOT" != "-" ] && [ "$TOT" != "null" ] && [ -n "$TOT" ]; then
		R=$(awk -v a="$TOT" -v b="$PR_BYTES" 'BEGIN{ if (b>0) printf "%.3f", a/b; else print "na" }')
		OUT=$(awk -v r="$R" -v lo="$RATIO_LO" -v hi="$RATIO_HI" 'BEGIN{ print (r+0<lo || r+0>hi) ? "1" : "0" }')
		[ "$OUT" = "1" ] && { V="$V  *** RANGE ${R}x of the PR diff -- base is wrong"; rc=1; } \
		                 || V="$V  range ${R}x"
	fi
	printf '%-46s %-12s %-11s %-6s %s\n' "$(basename "$f")" "${BASE:0:12}" "$TOT" "$GATE" "$V"
done
echo
echo "A WRONG_REPO row on a PASS artifact is a merge license produced over the wrong"
echo "repository. Treat the gate as void regardless of what it says, and re-run the"
echo "audit with \`repo\` passed explicitly."
echo "UNDETERMINED rows are NOT clean -- the instrument did not answer. Re-run them."
exit $rc
