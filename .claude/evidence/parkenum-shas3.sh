#!/usr/bin/env bash
# Phase 2 v3 -- MULTI-REPO. v2 was sound about "absent from the firmware repo" and
# that is not the question. A park comment cites companion SHAs: test-suite heads,
# claude-skills PRs, wiki commits. Asking ONE repo and printing ABSENT conflates
# "this commit does not exist" with "this commit is not in the repo I asked" --
# the same wrong-repo conflation SWEEP-audit-provenance-check.sh was written for,
# where an audit confidently PASSed having read a different repository.
# A cross-repo companion SHA is NOT a dangling reference.
set -u
REPOS="daqifi/daqifi-nyquist-firmware daqifi/daqifi-python-test-suite daqifi/daqifi-python-core cptkoolbeenz/claude-skills"
N_AGREE=3
for s in "$@"; do
  case "${#s}" in 32) printf '  %-42s %s\n' "$s" "NOT_SHA_SHAPE(md5)"; continue;; esac
  case "$s" in *[!0-9a-f]*) printf '  %-42s %s\n' "$s" "NOT_SHA_SHAPE(non-hex)"; continue;; esac
  found=""; absent=""
  for R in $REPOS; do
    n=0; ok=0
    for i in $(seq 1 "$N_AGREE"); do
      b=$(gh api "repos/$R/commits/$s" --jq '.sha' 2>&1); rc=$?
      if [ "$rc" -eq 0 ] && [ -n "$b" ]; then case "$b" in "$s"*) ok=1; break;; esac
      else case "$b" in *'"status":"422"'*|*'"status":"404"'*|*'No commit found'*) n=$((n+1));; esac; fi
      sleep 1
    done
    [ "$ok" -eq 1 ] && { found="$R"; break; }
    [ "$n" -ge "$N_AGREE" ] && absent="$absent ${R##*/}"
  done
  if [ -n "$found" ]; then printf '  %-42s RESOLVES in %s\n' "$s" "$found"
  else printf '  %-42s NOT FOUND in:%s  -- UNDETERMINED (wiki repos are not API-reachable)\n' "$s" "$absent"; fi
done
