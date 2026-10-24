#!/bin/bash
# ### MARKER-nqb-merge-line-accounting v4
#
# Accounts for lines a merge resolution may have silently dropped. v3 replaces v2's
# DISTINCT-SET comparison with MULTIPLICITY-AWARE counting, because v2 reduced all four
# sides with `sort -u` and therefore could not see a dropped REPEATED line -- the
# commonest shape of the loss it exists to catch. v2 printed
# "LOST-FROM-MAIN 0 ... RC=0 (0 = no parent's addition was dropped)" over a merge that
# dropped a real payment line. LIMITATIONS.md named that hazard; v2 implemented it.
#
# Usage: account-lines.sh <repo> <MAIN-rev> <BRANCH-rev> <RESULT-rev> <path>...
#
# For each distinct line L, with b/m/r/x = its COUNT in base/main/branch/result:
#   FLOOR  = max(m, r)        copies the result must have under ANY union reading
#   TARGET = m + r - b        copies it must have if both sides' changes were INDEPENDENT
# and the verdict is
#   x <  FLOOR   -> fewer copies than one parent alone had. ⛔ THE LABEL DEPENDS ON
#                    WHETHER THE PATH AUTO-MERGED, and that is not cosmetic -- it is what
#                    makes the finding sound. On a path git merges CLEANLY the auto-merge
#                    result is well-defined, so any shortfall is human editing: a
#                    DEFINITE LOSS. On a CONFLICTED path the resolution had to rewrite
#                    text, and a vanished line may have been SUPERSEDED by a
#                    differently-punctuated equivalent rather than dropped, so it is
#                    reported UNACCOUNTED and needs a read. v3 called both "DEFINITE
#                    LOSS" and so fired identically on a correct resolution and a lossy
#                    one -- caught by its own control, which is why the control exists.
#   x <  TARGET  -> ⚠ AMBIGUOUS. A loss if the two sides made INDEPENDENT changes
#                    (two payments, two list entries); correct if they made the SAME
#                    change. Only PROVENANCE decides -- but the band names the exact
#                    lines that need the read, which is what v2 could not do.
#   otherwise    -> accounted for.
# The conflicted/clean split comes from `git merge-tree --write-tree`, whose stage lines
# name each conflicting path. If merge-tree cannot be run the tool treats every path as
# CONFLICTED -- the direction that asks for a read rather than granting a clean bill.
# INVENTED stays separate: lines in neither parent, expected on a real resolution.
#
# EXIT: 0 clean · 1 DEFINITE loss (clean-merge path) · 2 refused
#       3 needs a read (unaccounted on a conflicted path, or multiplicity-ambiguous)
#       4 invented only
#       (highest severity wins; 2 is always immediate)
#
# All sides are read as GIT OBJECTS, never working-tree files: with core.autocrlf=true
# the working tree is CRLF and the object store LF, so a working-tree comparison reports
# every line as changed. CR is stripped, so the check is ending-agnostic.
set -u
[ $# -lt 5 ] && { echo "usage: $0 <repo> <MAIN> <BRANCH> <RESULT> <path>..."; exit 2; }
R="$1"; MAIN="$2"; BR="$3"; RES="$4"; shift 4
BASE=$(git -C "$R" merge-base "$MAIN" "$BR") || { echo "no merge base"; exit 2; }
# ⛔ RESULT must be a COMMITTED merge of both parents. A pre-commit HEAD compares main's
# additions against the BRANCH and reports every line main added as lost -- 797 of 797 in
# the run that prompted this guard, a number alarming enough to be acted on.
for _p in "$MAIN" "$BR"; do
  git -C "$R" merge-base --is-ancestor "$_p" "$RES" 2>/dev/null || {
    echo "⛔ REFUSING: RESULT ($RES) does not contain $_p as an ancestor."
    echo "   Commit the resolution first, then pass the merge commit."; exit 2; }
done
[ -n "$(git -C "$R" status --porcelain --untracked-files=no 2>/dev/null)" ] &&
  echo "⚠ WARNING: working tree has uncommitted changes; RESULT=$RES may not match the files you edited."

echo "merge-base: $BASE"
# ⛔ Which paths did git itself fail to merge? That decides whether a shortfall is a loss
# or a supersession. Parse the stage lines; never trust $? after a pipeline.
MT=$(git -C "$R" merge-tree --write-tree "$MAIN" "$BR" 2>/dev/null)
if [ -z "$MT" ]; then
  CONFLICTED_ALL=1
  echo "⚠ merge-tree unavailable -- treating every path as CONFLICTED (asks for a read; never grants a clean bill)"
else
  CONFLICTED_ALL=0
  CONFLICTS=$(printf '%s\n' "$MT" | awk '$1 ~ /^[0-7]{6}$/ && $3 ~ /^[123]$/ { sub(/^[^\t]*\t/, ""); print }' | sort -u)
  echo "paths git could not auto-merge: $(printf '%s' "$CONFLICTS" | grep -c . || true)"
fi
worst=0
body() { git -C "$R" show "$1:$2" 2>/dev/null | sed 's/\r$//' | grep -vE '^[[:space:]]*$'; }
exists() { git -C "$R" cat-file -e "$1:$2" 2>/dev/null; }
blob()   { git -C "$R" rev-parse --verify --quiet "$1:$2" 2>/dev/null; }
for f in "$@"; do
  echo "=== $f"
  # ⛔ EXISTENCE IS ASKED OF GIT, NOT INFERRED FROM FILTERED CONTENT. v2 used
  # `[ -z "$content" ]` and so reported "RESULT HAS NO SUCH FILE" for a file that
  # legitimately exists but is empty/all-whitespace, AND for one both parents correctly
  # deleted -- a false alarm in a tool whose own doc argues noise is a correctness property.
  if ! exists "$RES" "$f"; then
    bb=$(blob "$BASE" "$f"); mb=$(blob "$MAIN" "$f"); rb=$(blob "$BR" "$f")
    if [ -z "$mb" ] && [ -z "$rb" ]; then
      echo "  ✅ absent in RESULT and in BOTH parents -- a correct deletion"
    elif { [ -z "$mb" ] && [ -n "$rb" ] && [ "$rb" = "$bb" ]; } ||
         { [ -z "$rb" ] && [ -n "$mb" ] && [ "$mb" = "$bb" ]; }; then
      echo "  ✅ one parent DELETED it and the other left it identical to base -- the deletion wins, correct"
    else
      echo "  ⛔ ABSENT FROM RESULT and a surviving parent MODIFIED it -- that parent's changes are gone"
      echo "     base=${bb:0:9} main=${mb:0:9} branch=${rb:0:9}"
      worst=1
    fi
    continue
  fi
  if [ "$CONFLICTED_ALL" = 1 ] || printf '%s\n' "${CONFLICTS:-}" | grep -qxF "$f"; then
    MODE=CONFLICTED; echo "  (git could NOT auto-merge this path -- a shortfall may be SUPERSEDED, not lost)"
  else
    MODE=CLEAN;      echo "  (git auto-merges this path cleanly -- any shortfall is human editing)"
  fi
  { body "$BASE" "$f" | sed 's/^/B\t/'
    body "$MAIN" "$f" | sed 's/^/M\t/'
    body "$BR"   "$f" | sed 's/^/R\t/'
    body "$RES"  "$f" | sed 's/^/X\t/'; } | awk -v mode="$MODE" '
    { t=substr($0,1,1); L=substr($0,3); c[t SUBSEP L]++; seen[L]=1 }
    END{
      defl=0; amb=0; inv=0; unac=0
      for (L in seen) {
        b=c["B" SUBSEP L]+0; m=c["M" SUBSEP L]+0; r=c["R" SUBSEP L]+0; x=c["X" SUBSEP L]+0
        dm = m - b; dr = r - b
        if (dm < 0 || dr < 0) {
          # a side deliberately removed copies -- that removal is a contribution, so both
          # deltas apply and there is no same-change reading to be ambiguous about
          floor = b + dm + dr; if (floor < 0) floor = 0; target = floor
        } else {
          # both sides only added: FLOOR is the same-change reading (an addition by one
          # side covers the other), TARGET the independent reading (both are real payments)
          floor = b + (dm > dr ? dm : dr); target = b + dm + dr
        }
        if (x < floor) {
          if (mode == "CLEAN") { defl++; printf "  ⛔ DEFINITE LOSS   have %d, need >=%d  (base %d main %d branch %d)\n      | %s\n", x,floor,b,m,r,substr(L,1,130) }
          else                 { unac++; printf "  ⚠ UNACCOUNTED      have %d, one parent had %d -- conflicted path, may be superseded  (base %d main %d branch %d)\n      ? %s\n", x,floor,b,m,r,substr(L,1,130) }
        }
        else if (x < target)  { amb++;  printf "  ⚠ AMBIGUOUS        have %d, independent-change union wants %d  (base %d main %d branch %d)\n      ? %s\n", x,target,b,m,r,substr(L,1,130) }
        if (m==0 && r==0 && x>0) { inv++; printf "  + INVENTED (in neither parent) x%d\n      + %s\n", x, substr(L,1,130) }
      }
      printf "  ---- DEFINITE-LOSS %d · UNACCOUNTED %d · AMBIGUOUS %d · INVENTED %d   [%s path]\n", defl, unac, amb, inv, mode
      printf "SEV %d\n", (defl>0 ? 1 : ((unac>0 || amb>0) ? 3 : (inv>0 ? 4 : 0)))
    }' > /tmp/._al.$$ 2>&1
  grep -v '^SEV ' /tmp/._al.$$
  sev=$(awk '/^SEV /{print $2}' /tmp/._al.$$); rm -f /tmp/._al.$$
  # severity precedence: a DEFINITE loss outranks ambiguity, which outranks invented-only
  rank() { case "$1" in 1) echo 3;; 3) echo 2;; 4) echo 1;; *) echo 0;; esac; }
  [ "$(rank "${sev:-0}")" -gt "$(rank "$worst")" ] && worst="${sev:-0}"
done
echo "### ACCOUNTING-EXIT=$worst   (0 clean · 1 DEFINITE loss · 3 needs a read · 4 invented only)"
exit "$worst"
