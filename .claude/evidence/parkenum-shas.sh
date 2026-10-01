#!/usr/bin/env bash
# Phase 2: resolve every SHA named in a park/unpark comment, asked of GitHub.
# NOT asked of the local checkout: it is ~15 days stale, so a local miss would
# report "does not resolve" for a commit that exists. The remote is authoritative.
# Payload-verified: a resolution is accepted only when the returned .sha PREFIXES
# the queried one -- an HTTP 200 with an unexpected body is not a resolution.
set -u
R=daqifi/daqifi-nyquist-firmware
D=/tmp/claude-1000/-mnt-c-daqifi-wt-nq-b/f5691ce5-d530-4dee-a361-8f3f79c524c2/scratchpad/full
python3 - "$D" > /tmp/claude-1000/-mnt-c-daqifi-wt-nq-b/f5691ce5-d530-4dee-a361-8f3f79c524c2/scratchpad/shas.txt <<'PY'
import re,os,sys
D=sys.argv[1]
PARK=re.compile(r'PARK|Parked|HELD|unpark|Unpark',re.I)
for pr in "1137 1130 1129 1124 1115 1110 1106 1101 1099 1096 1094 1092 1078 1077".split():
    t=open(os.path.join(D,pr+".txt"),encoding='utf-8',errors='replace').read()
    live=""
    for ln in t.split("\n"):
        if ln.startswith("HEAD: "): live=ln[6:].strip()
    seen=[]
    for blk in t.split("===COMMENT ")[1:]:
        if not PARK.search(blk): continue
        for m in re.finditer(r'\b([0-9a-f]{7,40})\b', blk):
            s=m.group(1)
            if s not in seen and not s.isdigit(): seen.append(s)
    print(pr, "LIVE="+live, "NAMED="+",".join(seen[:8]))
PY
while read -r pr livef namedf; do
  live="${livef#LIVE=}"; named="${namedf#NAMED=}"
  echo "### fw#$pr  live=$live"
  IFS=, ; for s in $named; do unset IFS
    [ -z "$s" ] && continue
    got=$(gh api "repos/$R/commits/$s" --jq '.sha' 2>/dev/null)
    case "$got" in
      "$s"*) verdict="RESOLVES" ;;
      "")    verdict="DOES NOT RESOLVE (no .sha returned)" ;;
      *)     verdict="MISMATCH got=$got" ;;
    esac
    eq="-"; case "$live" in "$s"*) eq="== LIVE HEAD";; esac
    printf '    %-42s %-34s %s\n' "$s" "$verdict" "$eq"
    sleep 1
  done; unset IFS
done < /tmp/claude-1000/-mnt-c-daqifi-wt-nq-b/f5691ce5-d530-4dee-a361-8f3f79c524c2/scratchpad/shas.txt
