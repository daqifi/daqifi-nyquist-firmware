#!/bin/bash
# ### MARKER-nqb-merge-line-accounting v2
#
# WHAT IT CATCHES: a merge resolution that silently DROPS a line one parent added.
# That is the failure mode where "the file still ran" for weeks -- a 37-line
# fail-closed guard lost in a resolution, undetected because nothing referenced it.
#
# WHY v1 WAS UNUSABLE: comparing a parent against the result reports every line the
# OTHER parent legitimately superseded. On a real docs conflict that was 11 false
# alarms for 1 real one. A gate with that ratio does not get run. v2 asks the 3-way
# question instead, against the MERGE BASE:
#
#   LOST-FROM-BRANCH : a line the BRANCH added vs base, absent from the result
#   LOST-FROM-MAIN   : a line MAIN   added vs base, absent from the result
#   INVENTED         : a line in the result that is in NEITHER parent
#
# LOST-* should be ZERO unless you can name why. INVENTED is expected and should be
# SMALL -- it is exactly the text you wrote to reconcile both sides, and every entry
# should be a line you can point at in your own resolution.
#
# All three sides are read as GIT OBJECTS, never working-tree files: with
# core.autocrlf=true the working tree is CRLF and the object store is LF, so a
# working-tree comparison reports every line as changed. CR is stripped anyway so the
# check is ending-agnostic.
#
# Usage: account-lines.sh <repo> <MAIN-rev> <BRANCH-rev> <RESULT-rev> <path>...
#
# ⭐ READ THE DENOMINATORS, NOT THE RC (nq-c, after running it on a prose conflict).
# "LOST-FROM-MAIN 0 of 797" is the useful output: a meaningful zero with a real
# denominator. A NONZERO RC ON A PROSE / SAME-LINE CONFLICT IS EXPECTED -- both sides
# rewrote the same lines and your resolution superseded both. A NONZERO RC ON A CLEANLY
# AUTO-MERGED FILE IS THE ALARM. The next reader meets RC=1 first, so do not read it as
# failure; read which file it came from.
#
# ⛔ AND IT CANNOT SEE A SAME-LINE DROP. Where both sides rewrote the same lines, a
# dropped contribution lives INSIDE an "invented" line. Mutation-tested: 4/3/4 identical
# for a correct resolution and for a mutant missing a contribution. Pair it with an
# invariant that spans both sides -- arithmetic identity, a provenance/payment-name set,
# a cross-list check, or best, the compiled artifact.

set -u
R="$1"; MAIN="$2"; BR="$3"; RES="$4"; shift 4
BASE=$(git -C "$R" merge-base "$MAIN" "$BR") || { echo "no merge base"; exit 2; }
# ⛔ GUARD: RESULT must be a COMMITTED merge of both parents. Passing an uncommitted
# HEAD (i.e. before `git commit` of the resolution) compares main's additions against
# the BRANCH and reports every line main added as lost -- 797 of 797 in the run that
# prompted this guard. A number that large is alarming enough to be acted on, so the
# script refuses it rather than printing it.
for _p in "$MAIN" "$BR"; do
  if ! git -C "$R" merge-base --is-ancestor "$_p" "$RES" 2>/dev/null; then
    echo "⛔ REFUSING: RESULT ($RES) does not contain $_p as an ancestor."
    echo "   A merge resolution must be COMMITTED before accounting -- otherwise every"
    echo "   line the other parent added reports as lost. Commit, then pass the merge commit."
    exit 2
  fi
done
# ⛔ GUARD: a dirty tree means the file on disk is not what RESULT names.
if [ -n "$(git -C "$R" status --porcelain --untracked-files=no)" ]; then
  echo "⚠ WARNING: working tree has uncommitted changes; RESULT=$RES may not match the files you edited."
fi


echo "merge-base: $BASE"
rc=0
show() { git -C "$R" show "$1:$2" 2>/dev/null | sed 's/\r$//' | grep -vE '^[[:space:]]*$' | sort -u; }
for f in "$@"; do
  echo "=== $f"
  b=$(show "$BASE" "$f"); m=$(show "$MAIN" "$f"); r=$(show "$BR" "$f"); x=$(show "$RES" "$f")
  if [ -z "$x" ]; then echo "  ⛔ RESULT HAS NO SUCH FILE"; rc=1; continue; fi
  # lines each side ADDED relative to base
  addM=$(comm -23 <(printf '%s\n' "$m") <(printf '%s\n' "$b"))
  addB=$(comm -23 <(printf '%s\n' "$r") <(printf '%s\n' "$b"))
  for pair in "MAIN:$addM" "BRANCH:$addB"; do
    tag="${pair%%:*}"; add="${pair#*:}"
    [ -z "$add" ] && { printf '  %-6s added 0 lines vs base\n' "$tag"; continue; }
    lost=$(comm -23 <(printf '%s\n' "$add" | sort -u) <(printf '%s\n' "$x"))
    n=$(printf '%s' "$lost" | grep -c .)
    printf '  LOST-FROM-%-6s %s   (of %s lines that side added)\n' "$tag" "$n" "$(printf '%s' "$add" | grep -c .)"
    [ "$n" -gt 0 ] && { printf '%s\n' "$lost" | cut -c1-150 | sed 's/^/      | /'; rc=1; }
  done
  # lines in the result that neither parent has
  inv=$(comm -23 <(printf '%s\n' "$x") <(printf '%s\n' "$m"$'\n'"$r" | sort -u))
  ni=$(printf '%s' "$inv" | grep -c .)
  printf '  INVENTED (in neither parent): %s  <- must each be a line you WROTE to reconcile\n' "$ni"
  [ "$ni" -gt 0 ] && printf '%s\n' "$inv" | cut -c1-150 | sed 's/^/      + /'
done
echo "### ACCOUNTING-RC=$rc   (0 = no parent's addition was dropped)"
exit $rc
