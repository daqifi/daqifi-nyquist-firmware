#!/usr/bin/env bash
# Conditions for the 25 open test-suite rows lacking the `parked` label. READ-ONLY.
# Selection in jq (never grep over raw text); no byte truncation; payload-verified
# against .comments with retries, because a 503 already cost ts#312 one attempt.
# Net includes SYNONYMS and CANCELLATIONS -- sk#222 said HELD and no detector caught it.
set -u
R=daqifi/daqifi-python-test-suite
P=/tmp/claude-1000/-mnt-c-daqifi-wt-nq-b/f5691ce5-d530-4dee-a361-8f3f79c524c2/scratchpad
OUT="$P/ts25"; mkdir -p "$OUT"
SEL='unpark|Unpark|UNPARK|what would|what unparks|What unparks|PARK|Park|park|HELD|HOLD|Held|held|on hold|do not merge|Do not merge|not merging|NOT merging|needs|Needs|requires|Requires|waiting on|waits on|blocked on|Blocked on|operator|ruling|decision|re-derive|rederive|re-run|stale|SKIP|Retract|retract|Correction|correction|cleared|satisfied|terminal|abandon|superseded'
for pr in $(cat "$P/unlabelled.txt"); do
  f="$OUT/$pr.txt"; : > "$f"
  want=""
  for i in 1 2 3 4 5; do
    want=$(gh api "repos/$R/issues/$pr" --jq '.comments' 2>/dev/null)
    case "$want" in ''|*[!0-9]*) want=""; sleep 5;; *) break;; esac
  done
  if [ -z "$want" ]; then echo "ts#$pr: UNDETERMINED -- .comments never returned a number" | tee -a "$f"; continue; fi
  got=0
  for i in 1 2 3 4; do
    all=$(gh api "repos/$R/issues/$pr/comments" --paginate 2>/dev/null)
    got=$(printf '%s' "$all" | jq -r 'length' 2>/dev/null); case "$got" in ''|*[!0-9]*) got=0;; esac
    if [ "$got" -ge "$want" ]; then
      { echo "##### ts#$pr (verified $got/$want)"
        printf '%s' "$all" | jq -r --arg sel "$SEL" '.[] | select(.body|test($sel)) |
          "===COMMENT " + .created_at + " | upd " + .updated_at + " | " + .user.login + "\n" + .body'
      } >> "$f"
      break
    fi
    sleep 5
  done
  [ "$got" -lt "$want" ] && { echo "ts#$pr: UNDETERMINED -- fetched $got of $want" | tee -a "$f"; continue; }
  echo "ts#$pr ok $(wc -c < "$f")B $got/$want"
  sleep 1
done
