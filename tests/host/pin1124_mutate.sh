#!/usr/bin/env bash
# #1124 -- mutation harness for the path-canonicalisation pin in this Makefile.
#
# WHY THIS EXISTS. The pin is not one check but several arms, and a conflict
# resolution that UNIONS two versions of this Makefile can drop an arm while
# every surviving arm still passes on clean source. `make run` then goes green
# and the loss is invisible to the only check anyone runs after a merge.
# PRESENT and EFFECTIVE are different properties, and only a mutant separates
# them. Run this after resolving any conflict in tests/host/Makefile.
#
# Usage:  bash tests/host/pin1124_mutate.sh [WORKTREE]
#         (WORKTREE defaults to the git tree containing $PWD)
#
# ---------------------------------------------------------------------------
# THREE ISOLATIONS THAT ARE NOT OPTIONAL. Each one was measured to be
# load-bearing while this harness was being written, and each initially gave a
# FALSE PASS -- the harness said the pin was effective when the arm was gone:
#
#  1. ASSERT THE DIAGNOSTIC, NOT "an error appeared". #1029's
#     gen_1029_log_budget.py independently pins sd_BuildBucketPath's bucket==0
#     line and runs as a BUILD PREREQUISITE, so it aborts before this pin's
#     recipe is reached. A harness accepting any ^ERROR reported M5 as caught
#     with this pin's arm fully excised -- it was reading another ticket's
#     diagnostic.
#  2. RELAX #1029 FOR M5, so the build actually reaches this pin.
#  3. FORCE THE BINARY TO REBUILD. If a pin aborts the build, a stale
#     run_*_tests from the previous run is still on disk and reports 0 failed.
#
# And the guard in expect(): a pattern that is empty, or starts/ends with an
# escaped alternation, has an EMPTY ALTERNATIVE and therefore matches every
# string -- a clean-bill generator. That exact mistake produced false pass
# number three here, so the pattern is now validated before it is used.
#
# And false pass number four, in the other direction: each expected substring
# must appear on the FIRST ^ERROR line, because that is all first_error()
# captures. A substring taken from the diagnostic's second line never matches
# and the arm reads as broken when it is fine.
# ---------------------------------------------------------------------------
set -u

W="${1:-$(git rev-parse --show-toplevel 2>/dev/null || true)}"
[ -n "$W" ] && [ -d "$W/tests/host" ] || { echo "FATAL: '$W' is not a worktree with tests/host."; exit 2; }
W="$(cd "$W" && pwd -P)"
F="$W/firmware/src/services/sd_card_services/sd_card_manager.c"
G="$W/tests/host/gen_1029_log_budget.py"
M="$W/tests/host/Makefile"
for f in "$F" "$G" "$M"; do
    [ -f "$f" ] || { echo "FATAL: $f missing."; exit 2; }
done

echo "################################################################"
echo "# OPERATING ON: $W"
echo "#   HEAD $(git -C "$W" rev-parse HEAD 2>/dev/null | cut -c1-9)  branch $(git -C "$W" rev-parse --abbrev-ref HEAD 2>/dev/null)"
echo "################################################################"
if [ -n "$(git -C "$W" status --porcelain -- "$F" "$G" "$M" 2>/dev/null)" ]; then
    echo "FATAL: uncommitted changes in the files this mutates. Commit or set them"
    echo "       aside first -- restore here is a byte copy and will not arbitrate."
    exit 2
fi

BF=$(mktemp); BG=$(mktemp)
cp "$F" "$BF"; cp "$G" "$BG"
restore() { cp "$BF" "$F"; cp "$BG" "$G"; }
trap 'restore; rm -f "$BF" "$BG"' EXIT INT TERM
cd "$W/tests/host" || exit 2

pass=0; fail=0

# A pattern that could match anything is worse than no check.
pattern_is_unsafe() {
    [ -z "$1" ] && return 0
    case "$1" in
        *"
"*)        return 0 ;;   # embedded newline: grep treats it as several patterns
        '\|'*|*'\|') return 0 ;;   # leading/trailing alternation: empty branch
    esac
    return 1
}

# Force a rebuild so a stale binary can never answer for a mutant (isolation 3).
first_error() {
    rm -f run_924_tests
    timeout 600 make run 2>&1 | grep -m1 '^ERROR' || echo "<<NO ERROR>>"
}

expect() { # expect <label> <PATTERN|CLEAN>
    local label="$1" want="$2" got
    got="$(first_error)"
    if [ "$want" = CLEAN ]; then
        if [ "$got" = "<<NO ERROR>>" ]; then
            echo "  OK   $label (clean, as required)"; pass=$((pass+1))
        else
            echo "  FAIL $label expected no error, got: $got"; fail=$((fail+1))
        fi
    elif pattern_is_unsafe "$want"; then
        echo "  FAIL $label: the expect pattern itself would match anything."
        fail=$((fail+1))
    elif printf '%s' "$got" | grep -qF -- "$want"; then
        echo "  OK   $label (this pin's own diagnostic)"; pass=$((pass+1))
    else
        echo "  FAIL $label did NOT come from this pin."
        echo "         wanted substring: $want"
        echo "         got:              $got"
        fail=$((fail+1))
    fi
}

sub() { # sub <file> <literal-find> <literal-replace>   (exit 9 if absent)
    python3 -c '
import sys
p, a, b = sys.argv[1], sys.argv[2], sys.argv[3]
t = open(p).read()
if a not in t:
    sys.exit(9)
open(p, "w").write(t.replace(a, b, 1))' "$1" "$2" "$3"
}

echo "== setup: the baseline must be clean, or every result below is noise =="
expect "baseline" CLEAN
if [ "$fail" -ne 0 ]; then
    echo "ABORT: baseline is not clean; mutants would be meaningless."
    exit 1
fi

echo "== M1: revert one join to a raw \"%s/, operand on the CONTINUATION line =="
echo "==     (the shape that defeated an operand-keyed enumeration: 2 of 8)  =="
if sub "$F" 'snprintf(path, sizeof(path), "%.*s/%s" SD_MANIFEST_EXT,
                           (int)SdManifest_CanonicalDirLen(gpSDCardSettings->directory),
                           gpSDCardSettings->directory,
                           gSDCardData.baseFilename)' 'snprintf(path, sizeof(path), "%s/%s" SD_MANIFEST_EXT,
                           gpSDCardSettings->directory,
                           gSDCardData.baseFilename)'; then
    expect "M1 count arm" "path construction is no longer"
else
    echo "  FAIL M1 anchor absent -- source drifted from this harness."; fail=$((fail+1))
fi
restore

echo "== M5: revert ONLY sd_BuildBucketPath's bucket==0 arm. raw=0, canon=8,  =="
echo "==     accessor=8 and the function still CALLS the accessor, so every   =="
echo "==     count arm is blind and only the per-arm exact form can see it.   =="
if sub "$G" '"%\.\*s"\s*,\s*dirLen\s*,\s*dir\s*\)\s*;' '"%\.?\*?s"\s*,\s*(?:dirLen\s*,\s*)?dir\s*\)\s*;' \
   && sub "$F" 'written = snprintf(out, outLen, "%.*s", dirLen, dir);' \
               'written = snprintf(out, outLen, "%s", dir);'; then
    expect "M5 per-arm exact-form arm" "sd_BuildBucketPath() in"
else
    echo "  FAIL M5 anchors absent -- source or #1029's generator drifted."; fail=$((fail+1))
fi
restore

echo "== M3: NEGATIVE CONTROL. A raw \"%s/ join inside a COMMENT must NOT fire =="
echo "==     -- a pin that trips on prose is as useless as one that is blind. =="
if sub "$F" '/* #1124: canonicalise the walk ROOT.' \
            '/* old form: snprintf(p, n, "%s/%s", dir, f);
     * #1124: canonicalise the walk ROOT.'; then
    expect "M3 negative control" CLEAN
else
    echo "  FAIL M3 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== restored =="
expect "post-restore baseline" CLEAN

echo
echo "RESULT: $pass passed, $fail failed"
if [ "$fail" -ne 0 ]; then
    echo "THE PIN IS PRESENT BUT NOT EFFECTIVE -- an arm was lost. Do not merge."
fi
[ "$fail" -eq 0 ]
