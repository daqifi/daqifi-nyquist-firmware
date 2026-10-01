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
http_status() {   # $1=repo $2=sha -> prints the numeric HTTP status, or nothing
	local out
	out=$(gh api -i "repos/$1/commits/$2" 2>&1)
	# NOTE: deliberately not `|| echo`, and no `$?` after a pipeline -- both of those
	# turn "could not run" into a verdict. An empty status means NO ANSWER.
	printf '%s\n' "$out" | sed -n '1s@^HTTP/[0-9.]* \([0-9][0-9][0-9]\).*@\1@p' | head -1
}

KNOWN_GOOD=$(gh api "repos/$REPO" --jq '.default_branch' 2>/dev/null)
if [ -z "$KNOWN_GOOD" ]; then
	echo "CALIBRATION FAILED: cannot read repos/$REPO -- every row below would be UNDETERMINED." >&2
	echo "Fix access/network first. Reporting nothing rather than a sweep of unknowns." >&2
	exit 3
fi
POS=$(http_status "$REPO" "$KNOWN_GOOD")
NEG=$(http_status "$REPO" "0000000000000000000000000000000000000000")
printf 'CALIBRATION  known-good(%s)=%s   known-bad(40 zeros)=%s\n' "$KNOWN_GOOD" "${POS:-NO_ANSWER}" "${NEG:-NO_ANSWER}"
if [ "$POS" != "200" ] || [ "$NEG" = "200" ]; then
	echo "CALIBRATION FAILED: the instrument does not distinguish present from absent. STOPPING." >&2
	exit 3
fi
echo "CALIBRATION OK -- both directions proven. Proceeding."
echo

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
	S=$(http_status "$REPO" "$HEAD")
	case "${S:-}" in
		200) V="OK" ;;
		422) V="*** WRONG_REPO -- head_sha is not in $REPO" ; rc=1 ;;
		404) V="UNDETERMINED (404: repo missing or no access)" ; rc=1 ;;
		"")  V="UNDETERMINED (no HTTP status: network/auth/rate-limit)" ; rc=1 ;;
		*)   V="UNDETERMINED (HTTP $S)" ; rc=1 ;;
	esac
	printf '%-46s %-12s %-11s %-6s %s\n' "$(basename "$f")" "${BASE:0:12}" "$TOT" "$GATE" "$V"
done
echo
echo "A WRONG_REPO row on a PASS artifact is a merge license produced over the wrong"
echo "repository. Treat the gate as void regardless of what it says, and re-run the"
echo "audit with \`repo\` passed explicitly."
echo "UNDETERMINED rows are NOT clean -- the instrument did not answer. Re-run them."
exit $rc
