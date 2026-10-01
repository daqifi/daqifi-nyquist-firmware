#!/usr/bin/env bash
# Unpark conditions for every `parked` row in daqifi-python-test-suite. READ-ONLY.
#
# SELECTION IN jq, NEVER grep over raw text: the agent Bash tool's ugrep classifies
# a payload containing invalid UTF-8 as binary, skips it, and exits rc=1 -- identical
# to "no match". jq has decoded the JSON string before any regex runs.
# NO BYTE TRUNCATION: `cut -c` is byte-based and slicing a 4-byte emoji mid-character
# is what created that invalid UTF-8 in the first place. Shaping is left to the reader.
#
# BROAD NET including SYNONYMS, because sk#222 said HELD and no park detector caught
# it, and a cancellation cannot be regexed at all. This produces CANDIDATES for a
# human read, not a classification.
set -u
R=daqifi/daqifi-python-test-suite
OUT="${1:-/tmp/claude-1000/-mnt-c-daqifi-wt-nq-b/f5691ce5-d530-4dee-a361-8f3f79c524c2/scratchpad/ts}"
mkdir -p "$OUT"
SEL='PARK|Park|park|HELD|HOLD|Held|held|unpark|Unpark|UNPARK|what would|What unparks|what unparks|waiting on|waits on|blocked on|needs|requires|operator|ruling|decision|re-derive|rederive|do not merge|Do not merge|not merging|SKIPPING|Retract|retract|Correction|correction|cleared|satisfied|stale'
for pr in $(gh pr list --repo "$R" --state open --label parked --limit 100 --json number --jq '.[].number' | sort -n); do
  f="$OUT/$pr.txt"; : > "$f"
  want=""
  for i in 1 2 3 4 5; do
    want=$(gh api "repos/$R/issues/$pr" --jq '.comments' 2>/dev/null)
    case "$want" in ''|*[!0-9]*) want=""; sleep 4;; *) break;; esac
  done
  if [ -z "$want" ]; then echo "PR $pr: UNDETERMINED -- .comments never returned a number" | tee -a "$f"; continue; fi
  got=0; body=""
  for i in 1 2 3 4; do
    all=$(gh api "repos/$R/issues/$pr/comments" --paginate 2>/dev/null)
    got=$(printf '%s' "$all" | jq -r 'length' 2>/dev/null); case "$got" in ''|*[!0-9]*) got=0;; esac
    if [ "$got" -ge "$want" ]; then
      body=$(printf '%s' "$all" | jq -r --arg sel "$SEL" '.[] | select(.body|test($sel)) |
        "===COMMENT " + .created_at + " | upd " + .updated_at + " | " + .user.login + "\n" + .body')
      break
    fi
    sleep 4
  done
  if [ "$got" -lt "$want" ]; then echo "PR $pr: UNDETERMINED -- fetched $got of $want" | tee -a "$f"; continue; fi
  { echo "##### ts#$pr  (comments verified $got/$want)"; printf '%s\n' "$body"; } >> "$f"
  echo "ts#$pr ok $(wc -c < "$f")B ${got}/${want}"
  sleep 1
done
