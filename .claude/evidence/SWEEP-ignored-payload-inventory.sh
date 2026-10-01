#!/usr/bin/env bash
# Inventory of gitignored BINARY payload across the worktree fleet. READ-ONLY.
# Nothing is removed, moved or cleaned -- the finding being tested is that the
# ignored payload IS the artifact, so a tidy-up would be the hazard.
#
# ⛔ SCOPED BY PAYLOAD, NOT BY NAME. The brief said "every bench-* tree"; there is
# exactly ONE, so that key answers itself ("a one-off") and would be wrong for the
# reason this whole finding is about: a tree's NAME is a claim about its purpose,
# the payload is the fact. build-1055, nq-c-fw*, nq-b-fw* and audit-* are all trees
# where a build or a flash happened.
#
# DISCRIMINATOR: a firmware image OUTSIDE the standard build output paths
# (firmware/daqifi.X/{dist,build}/) is a DELIBERATELY STAGED image -- the `ab/`
# shape -- rather than rebuildable noise.
#
# `git --no-optional-locks` throughout, so no tree's index mtime moves.
# Status checks are UNPIPED: `$?` after a pipeline is the LAST command's status,
# and a size walk is exactly the shape where `du | tail` gets misread. Fifth
# instance of that defect was caught in a check written while being careful.
set -u
R=/mnt/c/daqifi/wt/nq-b
OUT="${1:-/tmp/claude-1000/-mnt-c-daqifi-wt-nq-b/f5691ce5-d530-4dee-a361-8f3f79c524c2/scratchpad/payload}"
mkdir -p "$OUT"

echo "### STAGED IMAGES (*.hex/*.bin/*.elf) OUTSIDE firmware/daqifi.X/{dist,build}"
for root in /mnt/c/daqifi/wt /mnt/c/Users/User/Documents/GitHub/daqifi-nyquist-firmware/.claude/worktrees; do
  [ -d "$root" ] || continue
  find "$root" -maxdepth 6 -type f \( -name '*.hex' -o -name '*.bin' -o -name '*.elf' \) \
       ! -path '*/firmware/daqifi.X/dist/*' ! -path '*/firmware/daqifi.X/build/*' \
       ! -path '*/.git/*' -printf '%s\t%TY-%Tm-%Td %TH:%TM\t%p\n' 2>/dev/null
done | sort -t'	' -k3 > "$OUT/staged.tsv"
rc_find=$?
echo "  rows: $(wc -l < "$OUT/staged.tsv")   (find pipeline rc=$rc_find -- sort's status, stated so it is not read as find's)"
awk -F'\t' '{split($3,p,"/"); tree=p[5]; if(p[4]=="worktrees")tree=p[7];
             bytes[tree]+=$1; n[tree]++}
   END{for(t in bytes) printf "  %-26s %2d files  %10.2f MB\n", t, n[t], bytes[t]/1048576}' \
   "$OUT/staged.tsv" | sort -k4 -rn

echo
echo "### PER-TREE: porcelain vs ignored, for every tree holding staged images"
awk -F'\t' '{split($3,p,"/"); t=p[1]"/"p[2]"/"p[3]"/"p[4]"/"p[5]; if(p[4]=="worktrees")t=p[1]"/"p[2]"/"p[3]"/"p[4]"/"p[5]"/"p[6]"/"p[7]; print t}' \
  "$OUT/staged.tsv" | sort -u > "$OUT/trees.txt"
while IFS= read -r T; do
  [ -d "$T" ] || continue
  # UNPIPED so $? belongs to git, not to a downstream filter
  porc=$(git --no-optional-locks -C "$T" status --porcelain 2>/dev/null); rc_p=$?
  np=$(printf '%s' "$porc" | /usr/bin/grep -ac . 2>/dev/null); [ -z "$porc" ] && np=0
  ign=$(git --no-optional-locks -C "$T" status --porcelain --ignored 2>/dev/null); rc_i=$?
  ni=$(printf '%s' "$ign" | /usr/bin/grep -ac '^!!' 2>/dev/null); [ -z "$ign" ] && ni=0
  head=$(git --no-optional-locks -C "$T" rev-parse --short HEAD 2>/dev/null)
  printf '  %-46s head=%-10s porcelain=%-4s(rc=%s)  ignored_entries=%-4s(rc=%s)\n' \
    "${T#/mnt/c/}" "$head" "$np" "$rc_p" "$ni" "$rc_i"
done < "$OUT/trees.txt"
