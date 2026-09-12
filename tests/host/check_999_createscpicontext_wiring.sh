#!/bin/bash
# ==========================================================================
# check_999_createscpicontext_wiring.sh
#
# Guards the actual BODY of CreateSCPIContext() (issue #999,
# firmware/src/services/SCPI/SCPIInterface.c) against the aliasing
# regression the fix closes: both SCPI transports (USB, WiFi/TCP) sharing
# one parse buffer / error queue. Invoked from tests/host/Makefile's
# SCPI999_BIN recipe -- see that recipe's comment for why it exists
# (SCPIInterface.c is not includable/linkable on the host, so
# test_999_scpi_context_storage_isolation.c can prove the FIX'S SHAPE but
# never executes the real function).
#
# History this script exists to close (three adversarial-audit rounds so
# far -- read this before "fixing" a false positive by loosening a check;
# every relaxation below undid a specific proven bypass):
#
#   Round 1: eleven Makefile greps checked only DECLARATIONS/SIGNATURES in
#     SCPIInterface.h/.c, UsbCdc.c, wifi_tcp_server.c -- nothing inspected
#     CreateSCPIContext()'s own body. An adversarial audit inserted
#     `static ScpiContextStorage shared; storage = &shared;` inside the
#     body, reintroducing #999 exactly, and it passed every guard.
#
#   Round 2 (first body-aware guard, three inline Makefile greps): forbid
#     reassigning `storage`, forbid a local ScpiContextStorage instance,
#     require storage->inputBuffer/errorQueue literally present anywhere in
#     the body. An independent counter-audit defeated it three ways and
#     found three false positives:
#       V2b: alias `s = &gShared` and use `s->inputBuffer`/`s->errorQueue`
#            in the real SCPI_Init() call, while the literal substrings
#            "storage->inputBuffer" / "storage->errorQueue" survive in a
#            NEARBY COMMENT -- the old check just grepped "does this text
#            appear anywhere in the body", comments included.
#       V3:  call SCPI_Init() correctly with the real storage (satisfying
#            every round-2 check), then AFTER that call re-seat
#            `daqifiScpiContext.buffer.data` and call SCPI_ErrorInit() a
#            second time onto file-static arrays. Nothing in round 2 looked
#            past the SCPI_Init() call.
#       V4:  split `storage = &shared;` across two physical lines -- grep
#            is line-based, so neither line alone matched the pattern.
#     False positives against legitimate code:
#       FP2: a prose comment mentioning "ScpiContextStorage instance"
#            tripped the "local instance declared" check.
#       FP3: `*storage = (ScpiContextStorage){0};` (writing THROUGH the
#            pointer into the caller's real struct -- harmless) tripped the
#            "storage reassigned" check, which didn't distinguish `storage =`
#            (reassigning the pointer) from `*storage =` (writing what it
#            already points to).
#       FP4: the old body extraction ran from the signature to EOF, relying
#            on CreateSCPIContext being the last function in the file; any
#            future function appended after it got blamed on
#            CreateSCPIContext.
#
#   Round 3 (this script's first version): bounded the body to the first
#     column-0 `}` (closed FP4), stripped `//` comments before flattening
#     to one line (closed FP2, V4), excluded `*storage =` from the
#     reassignment check (closed FP3), scoped the wiring check to the
#     SCPI_Init() call's own text via a non-greedy `SCPI_Init\(.*?\);`
#     match (closed V2b), and added checks forbidding `.buffer.data`,
#     `error_queue`, and a direct `SCPI_ErrorInit(` call anywhere in the
#     body (closed V3 as originally written). A second independent
#     counter-audit found this version still had gaps:
#       N1 (BLOCKING -- proven end-to-end through `make run`): a SECOND,
#          separate `SCPI_Init()` call after the first, correct one, wired
#          to a different (e.g. file-scope shared) pool. The non-greedy
#          `SCPI_Init\(.*?\);` match returns ALL matches, not just one, so
#          `grep -q 'storage->inputBuffer'` against the concatenated output
#          was satisfied by the FIRST (correct) call while the SECOND,
#          actually-live call rewired the context onto shared storage.
#          Same bug class as V3 -- "correct call, then reseat" -- just via
#          a second SCPI_Init() instead of touching the internals directly,
#          which is exactly the case checks (a)-(c) and the original
#          check (d) were not built to see.
#       N7/N8: `*(&storage) = &gShared;` and a pointer-to-pointer
#          (`ScpiContextStorage** pp = &storage; *pp = &gShared;`) both
#          reassign `storage` exactly like V1, but neither matches the
#          check-(a) regex's literal `storage[[:space:]]*=` shape.
#       N4b: the "first column-0 `}`" heuristic (not real brace counting)
#          is itself defeatable by a legally-placed column-0 `}` appearing
#          BEFORE the function's true end (e.g. inside a deliberately
#          mis-indented nested block) -- the body gets truncated early and
#          a V3-style mutation placed after that point is invisible to
#          every check.
#       N2/N3: only `//` line comments were stripped, not `/* */` block
#          comments -- so (N3) a legitimate block comment mentioning
#          `buffer.data` or `error_queue` false-positives check (d), and
#          (N2) a block comment containing a stray `);` between the real
#          `SCPI_Init(` and its true closing `);` can truncate the
#          non-greedy capture early, hiding the call's real arguments from
#          check (c).
#
# This script (round 4, current) closes all of the above:
#   - Body extraction now does REAL brace-depth counting from the opening
#     `{` (not "trust the first column-0 `}`"), so it is correct regardless
#     of indentation style or where nested blocks place their own closing
#     braces (closes N4b, and FP4/"to EOF" stays closed for the same
#     structural reason).
#   - BOTH `//` line comments AND `/* ... */` block comments (including
#     ones spanning multiple lines) are stripped before flattening (closes
#     N2, N3, and keeps FP2 closed for block-comment phrasing too).
#   - Check (a) additionally forbids `&storage` (taking the parameter's own
#     address) anywhere in the body -- production has no legitimate reason
#     to do this, and it's how a pointer-to-pointer or a doubly-indirect
#     write can reassign `storage` without matching the direct
#     `storage[[:space:]]*=` shape (closes N7/N8).
#   - A NEW check requires CreateSCPIContext() to call SCPI_Init() EXACTLY
#     ONCE. #999's actual fix calls it exactly once; a second call -- with
#     any storage, correctly wired or not -- is the mechanism, not just one
#     instance of it, so this closes N1 by construction rather than by
#     pattern-matching the second call's specific shape (closes N1).
#
# Round 5 (this state): a THIRD independent adversarial audit found check
# (c)'s wiring test was still a CONTAINMENT test, not an equality test --
# `grep -q 'storage->inputBuffer'` against the SCPI_Init() call's whole
# extracted text passes as long as that substring appears ANYWHERE in the
# call, including inside the untaken branch of a ternary:
#   SCPI_Init(..., cond ? gSharedStorage.inputBuffer : storage->inputBuffer,
#             ..., cond ? gSharedStorage.errorQueue : storage->errorQueue, ...);
# contains both literal substrings while a true `cond` hands the context
# SHARED storage at runtime -- reintroducing #999 exactly, guard green. This
# is the same containment-vs-equality gap round 4 already fixed at the
# CALLER level (tests/host/Makefile's CreateSCPIContext() checks) for a
# dead-keep-alive bypass; it was never applied to this script's OWN
# SCPI_Init()-call check. Fixed by extracting the call's arguments by real
# paren-depth counting (extract_call_args.pl, shared with the Makefile's
# caller checks so both call sites use one audited parser instead of two
# independently-maintained ones) and requiring one WHOLE argument to equal
# `storage->inputBuffer` / `storage->errorQueue` exactly, plus an explicit
# reject of any argument containing `?` at all -- CreateSCPIContext() has no
# legitimate reason to pass SCPI_Init() a conditional argument.
#
# What this script does NOT establish -- read this before trusting a green
# run more than it proves (see CLAUDE.md's scpi_claim_path.py section for
# why naming the residue matters more than another round of patches): it is
# still textual, not a real link/execution of CreateSCPIContext(). Known
# out-of-scope classes, named rather than silently absent:
#   - A helper function CreateSCPIContext() calls that performs equivalent
#     rewiring internally (e.g. `ScpiRebindToCommonPool(&daqifiScpiContext)`
#     defined elsewhere in the file) -- no token this script forbids need
#     appear in CreateSCPIContext()'s own body for this to work.
#   - A `union` over the two storages, pointer arithmetic against a shared
#     arena, a macro that expands to a second SCPI_Init()-equivalent
#     sequence without the literal token `SCPI_Init(`, or any other
#     sufficiently indirect aliasing mechanism.
#   - This script's checks operate on the SOURCE TEXT of ONE function; they
#     say nothing about what a DIFFERENT function (or a build-system /
#     linker trick) could do to the same effect.
#
# FOUR SPECIFIC BYPASSES ARE PROVEN OPEN. An adversarial audit of PR #1016
# demonstrated each with a valid-C edit that reintroduces #999 while every
# check here still reports green. They are recorded rather than patched,
# because five rounds of patching this script produced five rounds of new
# bypasses and the honest fix is the seam below, not a sixth regex:
#   1. A STRING LITERAL forges whole arguments. extract_call_args.pl is not
#      literal-aware, so commas inside "..." split into extra arguments. A
#      literal crafted to emit `storage->inputBuffer` and `storage->errorQueue`
#      as separate arguments satisfies both equality checks while the real
#      backing-storage arguments point at a shared pool. Note this is
#      fail-OPEN, which contradicts that file's own fail-CLOSED reasoning --
#      that reasoning covers literal PARENS, which truncate, not COMMAS.
#   2. A `// }` comment line truncates body extraction. The brace-depth walk
#      runs BEFORE comment stripping, so anything after such a line is
#      invisible to every check below.
#   3. The Makefile caller checks test PRESENCE, not POSITION. The expected
#      token anywhere in the argument list passes, including in the wrong
#      parameter slot with the right label elsewhere as a void*.
#   4. `(storage) = &shared;` evades the reassignment ban, which requires the
#      `=` to follow the bare identifier. A parenthesised lvalue is ordinary C.
#
# So: treat a green run as evidence against HONEST REGRESSION only. It is not
# evidence against a determined or half-finished refactor, and it never was --
# what changed is that the four routes above are now named instead of implied.
# Closing these needs the real link this repo has twice done for similarly
# non-includable UUTs (AD7609Scale.h split out of AD7609.h for #889,
# JSON_StringEscape.h split out of JSON_Encoder.c for #164): split the pure
# wiring logic into a small header with no FreeRTOS/Harmony dependency so a
# host test can EXECUTE it instead of grepping its shape.
#
# Usage: check_999_createscpicontext_wiring.sh <path-to-SCPIInterface.c>
# Exit 0 = all checks pass. Exit 1 with a diagnostic on stderr = a check
# failed (prints WHICH one and why, per this repo's convention for guards
# like this -- see BENCH_BIN/SUSPEND_BIN in the Makefile).
# ==========================================================================
set -u

SRC="${1:?usage: $0 <path-to-SCPIInterface.c>}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
ARG_EXTRACTOR="$SCRIPT_DIR/extract_call_args.pl"

fail() {
    local msg="$1"
    shift
    echo "ERROR: $msg" >&2
    for line in "$@"; do
        echo "       $line" >&2
    done
    exit 1
}

if [ ! -f "$SRC" ]; then
    fail "cannot read $SRC" "check_999_createscpicontext_wiring.sh's argument is wrong."
fi

FUNC_SIG_RE='^scpi_t CreateSCPIContext\(scpi_interface_t\* interface, void\* user_context,'

# ---- 1) locate the signature, then extract the body by REAL brace ----
#         counting (not "trust the first column-0 `}`" -- round-3's
#         version did that and an independent audit (N4b) pointed out a
#         legally-placed early column-0 `}` truncates it silently).
#
# SCPIInterface.c is CRLF-terminated (Windows-authored firmware source).
# Strip \r BEFORE any line-oriented matching, not after: a trailing \r
# breaks line-anchored regexes (`^...$`) in ways that fail silently rather
# than loudly (round 3's original bug, found via its own FP4 regression
# test).
LF_SRC=$(tr -d '\r' < "$SRC")

SIG_LINE=$(printf '%s\n' "$LF_SRC" | grep -nE "$FUNC_SIG_RE" | head -1 | cut -d: -f1)
if [ -z "$SIG_LINE" ]; then
    fail "could not locate CreateSCPIContext() in $SRC (signature moved?)." \
         "Re-check #999's fix shape before trusting this guard."
fi

RAW=$(printf '%s\n' "$LF_SRC" | awk -v start="$SIG_LINE" '
    NR < start { next }
    {
        print $0
        opens  = gsub(/\{/, "{")
        depth += opens
        closes = gsub(/\}/, "}")
        depth -= closes
        if (depth > 0) seen = 1
        if (seen && depth <= 0) { done = 1; exit }
    }
    END {
        if (!done) exit 1
    }
')
AWK_STATUS=$?
if [ -z "$RAW" ] || [ "$AWK_STATUS" -ne 0 ]; then
    fail "could not brace-balance CreateSCPIContext()'s body in $SRC" \
         "(found the signature at line $SIG_LINE but never returned to brace" \
         "depth 0). Its shape moved -- re-check this guard before trusting it."
fi

# ---- 2) strip comments (both // and /* */, block comments may span -----
#         multiple lines -- round-3 only stripped // and an audit (N2/N3)
#         found both a false positive and a capture-truncation bypass
#         through a /* */ block comment), THEN flatten to one line so a
#         mutation split across physical lines can't dodge a single-line
#         regex (closes round-3's V4).
NOLINECOMMENT=$(printf '%s\n' "$RAW" | sed -E 's@//.*$@@')
# Strip C block comments with the standard POSIX-regex-safe construct
# (matches balanced-enough /* ... */ spans without needing a non-greedy
# quantifier, which grep/sed don't portably support): a '/*' followed by
# any run of non-'*' characters, then one-or-more '*' each optionally
# followed by a non-'/' non-'*' run, ending in '*/'.
NOCOMMENT=$(printf '%s' "$NOLINECOMMENT" | perl -0pe 's{/\*.*?\*/}{}gs')
FLAT=$(printf '%s' "$NOCOMMENT" | tr '\n' ' ')

# ---- check (a): storage must never be reassigned, directly or via its ---
#      own address. Neutralize legitimate `storage->field` and
#      `*storage =` (write-through, not reassignment -- round-3's FP3)
#      before looking for `storage <ws>= ` (not `==`). Separately forbid
#      `&storage` anywhere: production never takes this pointer's own
#      address, and doing so is how `*(&storage) = ...` or a
#      pointer-to-pointer indirection reassigns it without matching the
#      direct-assignment shape (closes N7/N8).
CHECK_A=$(printf '%s' "$FLAT" | sed -E 's/storage->/STORAGE_FIELD_/g; s/\*storage([[:space:]]*=)/STORAGE_DEREF_ASSIGN\1/g')
if printf '%s' "$CHECK_A" | grep -qE '(^|[^A-Za-z0-9_])storage[[:space:]]*=[^=]'; then
    fail "CreateSCPIContext() in $SRC reassigns its 'storage' parameter." \
         "This is the exact #999 regression shape an adversarial audit" \
         "reintroduced: substituting alternate backing storage re-aliases" \
         "every real caller-supplied buffer/queue without touching any" \
         "declaration, signature or caller. Do not reassign storage inside" \
         "CreateSCPIContext()."
fi
if printf '%s' "$FLAT" | grep -qE '&storage([^A-Za-z0-9_]|$)'; then
    fail "CreateSCPIContext() in $SRC takes the address of its 'storage'" \
         "parameter (&storage). Production code never does this -- it is" \
         "how storage can be reassigned indirectly (a pointer-to-pointer," \
         "or *(&storage) = ...) without matching the direct 'storage ='" \
         "reassignment shape above. Do not take storage's own address."
fi

# ---- check (b): no function-local ScpiContextStorage instance -----------
if printf '%s' "$FLAT" | grep -qE 'ScpiContextStorage[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*[;=]'; then
    fail "CreateSCPIContext() in $SRC declares a local ScpiContextStorage" \
         "instance inside its own body (e.g. 'static ScpiContextStorage" \
         "shared;'). #999's fix takes storage from the CALLER ONLY -- a" \
         "function-local instance is the audit's mutation shape. Remove it."
fi

# ---- check (c): SCPI_Init() must be called EXACTLY ONCE, and that call --
#      must wire storage's fields. Round 3 scoped the wiring check to "the
#      SCPI_Init() call's own text" via a non-greedy match, but never
#      counted how many such calls existed -- a second, live call with
#      different (e.g. shared) storage passed anyway, because the FIRST
#      (correct) call's text was in the same concatenated match output
#      (round-4's N1, proven end-to-end through `make run`; this is the
#      same "correct call, then reseat" class as the buffer.data/
#      SCPI_ErrorInit checks below, just via a second SCPI_Init() instead
#      of touching the internals directly -- closing it by requiring
#      exactly one call is more robust than trying to pattern-match every
#      possible shape a second call could take).
CALL_COUNT=$(printf '%s' "$FLAT" | grep -oE 'SCPI_Init[[:space:]]*\(' | wc -l)
if [ "$CALL_COUNT" -eq 0 ]; then
    fail "CreateSCPIContext() in $SRC no longer calls SCPI_Init() -- #999's" \
         "fix shape may have moved; re-check before relaxing this guard."
elif [ "$CALL_COUNT" -gt 1 ]; then
    fail "CreateSCPIContext() in $SRC calls SCPI_Init() $CALL_COUNT times" \
         "(expected exactly 1)." \
         "A second call -- even with correct storage->inputBuffer/errorQueue" \
         "wiring in the first -- re-seats the context's buffer/error-queue" \
         "onto whatever the second call's storage is. This is the exact" \
         "#999 regression class the buffer.data/error_queue/SCPI_ErrorInit" \
         "checks below exist for, reached through a second SCPI_Init() call" \
         "instead of touching the internals directly."
fi
# Extract SCPI_Init()'s own arguments by real paren-depth counting (not
# "grep the call's text and hope no other SCPI_Init(...)-shaped text is
# nearby") -- round 5, closing the containment-vs-equality gap described in
# this script's header. FLAT is already comment-stripped (step 2 above), and
# CALL_COUNT above already proved there is exactly one SCPI_Init() call in
# it, so extracting "the first" is extracting "the only" one.
if [ ! -f "$ARG_EXTRACTOR" ]; then
    fail "cannot find $ARG_EXTRACTOR (extract_call_args.pl must live next to" \
         "check_999_createscpicontext_wiring.sh)."
fi
INIT_ARGS=$(printf '%s' "$FLAT" | perl "$ARG_EXTRACTOR" SCPI_Init)
if [ -z "$INIT_ARGS" ]; then
    fail "CreateSCPIContext() in $SRC's SCPI_Init() call's arguments could" \
         "not be extracted (unbalanced parens/brackets?). Re-check before" \
         "relaxing this guard."
fi
if printf '%s\n' "$INIT_ARGS" | grep -qF '?'; then
    fail "CreateSCPIContext()'s SCPI_Init() call in $SRC has a" \
         "conditional/ternary expression in one of its arguments. #999's" \
         "fix requires the input-buffer and error-queue arguments to be" \
         "the caller's own storage->inputBuffer/storage->errorQueue" \
         "unconditionally -- a ternary can make either argument resolve to" \
         "different (e.g. shared) storage at runtime while its OTHER" \
         "branch still contains the expected text, which is exactly how a" \
         "containment check (rather than an equality check) can be" \
         "defeated. Remove the conditional."
fi
if ! printf '%s\n' "$INIT_ARGS" | grep -qxF 'storage->inputBuffer'; then
    fail "CreateSCPIContext()'s SCPI_Init() call in $SRC no longer passes" \
         "storage->inputBuffer as one of its OWN arguments, exactly (an" \
         "aliased pointer, a ternary merely containing the text, or the" \
         "literal text surviving only in a comment elsewhere, doesn't" \
         "count -- one whole argument must equal it)."
fi
if ! printf '%s\n' "$INIT_ARGS" | grep -qxF 'storage->errorQueue'; then
    fail "CreateSCPIContext()'s SCPI_Init() call in $SRC no longer passes" \
         "storage->errorQueue as one of its OWN arguments, exactly (an" \
         "aliased pointer, a ternary merely containing the text, or the" \
         "literal text surviving only in a comment elsewhere, doesn't" \
         "count -- one whole argument must equal it)."
fi

# ---- check (d): nothing may re-seat the context internals directly ------
# #999's fix relies SOLELY on ONE SCPI_Init() call (which owns buffer.data
# and the error queue internally -- parser.c/error.c) for exactly this
# reason: code that re-seats those fields directly, or calls
# SCPI_ErrorInit() itself, reintroduces #999 the same way a second
# SCPI_Init() call does (check (c) above), just without going back through
# SCPI_Init(). Production CreateSCPIContext() has no legitimate reason to
# touch any of these three tokens at all.
if printf '%s' "$FLAT" | grep -q 'buffer\.data'; then
    fail "CreateSCPIContext() in $SRC touches .buffer.data directly." \
         "SCPI_Init() owns that wiring internally (parser.c) --" \
         "CreateSCPIContext must not re-seat it after the call, which is" \
         "how #999 can be reintroduced even with a correct SCPI_Init() call."
fi
if printf '%s' "$FLAT" | grep -q 'error_queue'; then
    fail "CreateSCPIContext() in $SRC references the context's internal" \
         "error_queue directly. SCPI_Init()/SCPI_ErrorInit() own that" \
         "wiring internally (error.c) -- CreateSCPIContext must not touch" \
         "it (note: the storage struct's OWN field is camelCase" \
         "'errorQueue', not this snake_case 'error_queue' -- they are" \
         "deliberately different tokens so this check can't collide with" \
         "legitimate storage->errorQueue usage)."
fi
if printf '%s' "$FLAT" | grep -qE 'SCPI_ErrorInit[[:space:]]*\('; then
    fail "CreateSCPIContext() in $SRC calls SCPI_ErrorInit() directly." \
         "#999's fix relies solely on SCPI_Init() (which calls" \
         "SCPI_ErrorInit() internally) -- a second/separate call here is" \
         "how #999 can be reintroduced after a correct SCPI_Init() call."
fi

exit 0
