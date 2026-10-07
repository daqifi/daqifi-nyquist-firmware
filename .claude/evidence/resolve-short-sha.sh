#!/usr/bin/env bash
# Resolve a SHORT citation to its FULL sha via the API, then compare full-to-full.
# ⛔ WHY: a 9-character collision was found live on fw#976 --
#   real 578f32be8850dc603d57631229e2dfc083f277fa
#   also 578f32be8d80de5cf8ba4b55c3dd4e28e4b2c3ef   <- 9 shared chars
# A prefix MISMATCH still proves inequality (identical commits have identical
# prefixes), so prefix-based STALE calls are sound. A prefix MATCH proves nothing,
# so prefix-based CURRENT calls are NOT. The error is permissive -- exactly the
# direction that licenses an unaudited merge.
# An unresolvable short SHA is UNRESOLVED: a third state, never a mismatch.
# N_AGREE retries because the endpoint flakes (~1 blank in 5 observed).
set -u
N_AGREE=3
resolve() { # repo short -> prints full sha, or ABSENT / UNRESOLVED
  local R="$1" s="$2" i b rc n_abs=0
  for i in $(seq 1 "$N_AGREE"); do
    b=$(gh api "repos/$R/commits/$s" --jq '.sha' 2>&1); rc=$?
    if [ "$rc" -eq 0 ] && [ ${#b} -eq 40 ]; then printf '%s' "$b"; return 0; fi
    case "$b" in *'"status":"422"'*|*'"status":"404"'*|*'No commit found'*) n_abs=$((n_abs+1));; esac
    sleep 2
  done
  [ "$n_abs" -ge "$N_AGREE" ] && printf 'ABSENT' || printf 'UNRESOLVED'
}
printf '%-9s %-10s %-42s %-42s %s\n' row cited resolved live verdict
while read -r label repo pr short; do
  [ -z "${label:-}" ] && continue
  full=$(resolve "$repo" "$short")
  live=$(gh pr view "$pr" --repo "$repo" --json headRefOid --jq .headRefOid 2>/dev/null)
  case "$full" in
    ABSENT)     v="CITATION ABSENT -- commit does not exist" ;;
    UNRESOLVED) v="UNRESOLVED -- third state, NOT a mismatch" ;;
    *) if [ "$full" = "$live" ]; then v="CURRENT (resolved, full-to-full)"
       else v="STALE (resolved, full-to-full)"; fi ;;
  esac
  printf '%-9s %-10s %-42s %-42s %s\n' "$label" "$short" "$full" "${live:-????}" "$v"
  sleep 1
done
