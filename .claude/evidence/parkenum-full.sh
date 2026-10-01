#!/usr/bin/env bash
# Phase 1 of the unpark-condition enumeration for lane nq-b's 14 firmware rows.
# Read-only. Nothing is written to any board.
#
# SELECTION IS DONE IN jq, NOT grep, deliberately: measured 2026-10-01, the agent
# Bash tool's ugrep 7.8.4 classifies GitHub comment payloads as BINARY (they carry
# NEL/U+0085, so `iconv -f UTF-8 -t UTF-8` exits 1), silently skips them, and exits
# rc=1 -- indistinguishable from "no match", and the error points CLEAN. jq has no
# binary-classification step. Also run me as `bash x.sh` so `grep` is GNU grep.
#
# Payload-verified: every row's comment count is checked against .comments before
# anything is emitted. An unverified row emits UNDETERMINED and never zero.
set -u
R=daqifi/daqifi-nyquist-firmware
OUT="${OUT:-/tmp/claude-1000/-mnt-c-daqifi-wt-nq-b/f5691ce5-d530-4dee-a361-8f3f79c524c2/scratchpad/full}"
mkdir -p "$OUT"
# Broad net, applied inside jq against the raw body. PARKED|HELD|HOLD catch the
# declarations; unpark|would unpark catch the conditions; Retract|Correction catch
# the chained-correction shape that makes the LAST statement the live one.
SEL='PARK|Park|park|HELD|HOLD|Held|unpark|Unpark|SKIPPING|Retract|retract|Correction|correction|operator'
for pr in 1137 1130 1129 1124 1115 1110 1106 1101 1099 1096 1094 1092 1078 1077; do
  f="$OUT/$pr.txt"; : > "$f"
  want=""
  for i in 1 2 3 4 5; do
    want=$(gh api "repos/$R/issues/$pr" --jq '.comments' 2>/dev/null)
    case "$want" in ''|*[!0-9]*) want=""; sleep 5;; *) break;; esac
  done
  if [ -z "$want" ]; then echo "PR $pr: UNDETERMINED -- .comments never returned a number in 5 tries" | tee -a "$f"; continue; fi

  got=0; body=""
  for i in 1 2 3 4; do
    body=$(gh api "repos/$R/issues/$pr/comments" --paginate 2>/dev/null \
      | jq -r --arg sel "$SEL" '.[] | select(.body|test($sel)) |
          "===COMMENT " + .created_at + " | updated " + .updated_at + " | " + .user.login + "\n" + .body')
    got=$(gh api "repos/$R/issues/$pr/comments" --paginate 2>/dev/null | jq -r 'length')
    case "$got" in ''|*[!0-9]*) got=0;; esac
    [ "$got" -ge "$want" ] && break
    body=""; sleep 5
  done
  if [ "$got" -lt "$want" ]; then
    echo "PR $pr: UNDETERMINED -- fetched $got of $want comments after 4 tries" | tee -a "$f"; continue
  fi

  { echo "########## PR $pr  (comments verified: $got of $want)"
    gh pr view "$pr" --repo "$R" --json labels,headRefOid,mergeStateStatus,state,isDraft,baseRefName,headRefName \
      --jq '"LABELS: " + ([.labels[].name]|join(" | ")) + "\nHEAD: " + .headRefOid + "\nBRANCH: " + .headRefName + " -> " + .baseRefName + "\nSTATE: " + .state + " draft=" + (.isDraft|tostring) + " mergeState=" + .mergeStateStatus' 2>/dev/null
    echo; printf '%s\n' "$body"
  } >> "$f"
  echo "PR $pr: ok, $(wc -c < "$f") bytes, $got/$want comments"
  sleep 2
done
