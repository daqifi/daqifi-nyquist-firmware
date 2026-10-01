#!/usr/bin/env bash
# Phase 2 CORRECTED (v2). Two instrument bugs fixed, both found by their own output.
#
# v0 was unsound: it reported `b083336c95a5...` (40 chars) ABSENT and `b083336c9`
# (the SAME commit) RESOLVES. Cause: `--jq '.sha'` on an error body yields empty,
# so EVERY failure mode mapped to "absent". A single lookup is not evidence of absence.
#
# v1 was wrong in the safe direction: it parsed the status line with
# `tr -dc '0-9' | cut -c1-3`, and "HTTP/2.0 200 OK" -> "20200" -> "202", matching no
# arm, so every SHA came back TRANSIENT/UNDETERMINED. Wrong, but it refused to answer
# rather than answering falsely -- which is the failure direction to prefer.
#
# v2 reads gh's EXIT STATUS plus gh's own error body, which carries "status":"NNN".
# Rules kept from SWEEP-audit-provenance-check.sh, which already encoded them:
#   * N_AGREE consistent 422/404s required before reporting absence;
#   * the four outcomes are NEVER collapsed into resolves/not-resolves;
#   * a 32-hex string is an MD5, not a commit SHA (a bench attestation md5 was swept
#     up by a [0-9a-f]{7,40} regex and would have been reported as a vanished commit).
set -u
R=daqifi/daqifi-nyquist-firmware
N_AGREE=3
classify() {
  local s="$1" n_abs=0 ok=0 other=0 i body rc
  case "${#s}" in 32) echo "NOT_SHA_SHAPE(md5-length, never a commit)"; return;; esac
  case "$s" in *[!0-9a-f]*) echo "NOT_SHA_SHAPE(non-hex)"; return;; esac
  for i in $(seq 1 "$N_AGREE"); do
    body=$(gh api "repos/$R/commits/$s" --jq '.sha' 2>&1); rc=$?
    if [ "$rc" -eq 0 ] && [ -n "$body" ]; then
      case "$body" in "$s"*) ok=$((ok+1));; *) other=$((other+1));; esac
    else
      case "$body" in
        *'"status":"422"'*|*'"status":"404"'*|*'No commit found'*) n_abs=$((n_abs+1)) ;;
        *) other=$((other+1)) ;;
      esac
    fi
    sleep 2
  done
  if   [ "$ok"    -ge 1 ];          then echo "RESOLVES"
  elif [ "$n_abs" -ge "$N_AGREE" ]; then echo "ABSENT ($n_abs/$N_AGREE consistent 422/404)"
  else echo "UNDETERMINED (ok=$ok absent=$n_abs other=$other)"
  fi
}
for arg in "$@"; do printf '  %-44s %s\n' "$arg" "$(classify "$arg")"; done
