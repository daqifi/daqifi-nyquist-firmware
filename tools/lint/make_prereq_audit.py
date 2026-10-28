#!/usr/bin/env python3
r"""Ask MAKE whether a guard's prerequisite variable was defined in time.

THE DEFECT
    make expands a PREREQUISITE list when it READS the rule. A variable
    defined later in the file is empty AT THAT POINT, so the dependency
    silently disappears: an edit to the guarded source reuses the stale
    binary and the guard never runs.

    The RECIPE is unaffected -- recipe lines expand at EXECUTION time, after
    the parse -- so every grep has the right path and every guard that DOES
    run is correct. A correct recipe with an absent dependency.

WHY THIS ASKS make INSTEAD OF READING THE MAKEFILE
    An earlier version of this tool parsed the Makefile itself, and an
    adversarial audit took it apart on the real file. Every one of these was
    reproduced, not hypothesised:

      * CONTINUATION LINES. It read only a rule's FIRST line, so in
            $(WAITLOOP_BIN): test_1056_wait_loop.c ... $(WAITLOOP_H) \\
                             $(USER_SPI_SRC) ... $(RECIPES)
        five prerequisite variables on the second line were invisible.
      * ${VAR} references. Its regex matched only $(VAR).
      * TAB-INDENTED SHELL ASSIGNMENTS read as make definitions. 220 recipe
        lines in that Makefile match `name=`, so shell temporaries (a, b, n,
        bad) registered as defined make variables -- making a genuinely
        undefined prerequisite of that name look fine. A false NEGATIVE.
      * Its make cross-check never affected the exit code, so the "two legs"
        it advertised were one leg and a printout.

    Those are four spellings of one mistake: modelling a language instead of
    asking its implementation. `make --warn-undefined-variables` warns at READ
    time, with file:line, for exactly the thing this gate is about, and it
    cannot be evaded by syntax because it IS make.

    Measured on a fixture carrying all three evasions:

        Makefile:6: warning: undefined variable 'LATE1'   <- ${} form
        Makefile:9: warning: undefined variable 'LATE2'   <- continuation line
        (no warning for a recipe-only reference)          <- correctly silent

    And goal-independent: prerequisite lists expand when each rule is read, so
    the same warnings appear whichever target is requested.

WHY env -i IS NOT OPTIONAL
    A variable present in the ENVIRONMENT is defined as far as make is
    concerned, so `LATE_SRC=x make ...` suppresses the warning for exactly the
    defect this looks for. Measured: with the name exported, 0 warnings; under
    `env -i`, 1. The subprocess therefore runs with a scrubbed environment,
    carrying only PATH.

USAGE
    python3 tools/lint/make_prereq_audit.py               # tests/host/Makefile
    python3 tools/lint/make_prereq_audit.py [Makefile]    # any other one
    python3 tools/lint/make_prereq_audit.py --self-test   # prove it can FIRE

    exit 0  no undefined-variable warnings
    exit 1  warnings, printed with ::error file=,line=:: annotations
    exit 2  could not run (no such Makefile, make unusable, or the self-test
            fixture failed to behave) -- never silently green

THE TWO WAYS ASKING make CAN STILL GO WRONG, AND WHY BOTH ARE REFUSALS
    Delegating to make removes the parser's blind spots but introduces two of
    its own, both of which used to produce a GREEN audit:

      * make STOPPING EARLY. A warning list is only a verdict on the whole
        file if make finished reading it. Measured on a fixture whose line 2
        was a `missing separator`: make died there, never read the genuinely
        undefined prerequisite on line 4, and this tool printed
        `warnings : 0 / (none)` and exited 0 -- a clean bill on a file make
        could not read. Only ONE non-zero exit was handled before (the one
        whose stderr happened to say "No rule to make target"), so every other
        way of failing was reported as cleanliness. ANY non-zero exit is now a
        refusal. The benign case is unaffected: the audited Makefile exits 0
        with 0 stderr lines, so this carves out nothing.
      * $(value X). `value` yields a variable's unexpanded text WITHOUT
        referencing it, so --warn-undefined-variables says nothing at all.
        Measured on one fixture: `ok: base.c $(value LATE)` warns 0 times,
        while the same rule written `${LATE}` warns once. A prerequisite list
        built that way is just as empty and completely invisible here. So a
        file using it gets NO VERDICT rather than a green one.

    Both are the same shape as the parser's: an answer that looks like a
    measurement but is the absence of one. The rule here is that this tool has
    three outcomes, not two -- clean, findings, and NO VERDICT.

AND THEN I REINTRODUCED A PARSER FOR THE REFUSALS THEMSELVES
    An adversarial audit took the refusals apart three ways on the real file,
    and all three were ONE mistake: having removed the Makefile parser for
    prerequisites, I wrote a new text scan to decide WHICH FILES to scan and
    WHICH SPELLING counts as `value`. Measured, every one of these printed
    `warnings : 0 / (none)` and exited 0 on a Makefile whose guard rule had a
    silently empty prerequisite list:

      * `include\` + newline + ` inc.mk`. The include regex was matched per
        PHYSICAL line, so neither line of a continued directive matched: the
        included file was never scanned AND never recorded as unfollowable.
      * `$(eval include inc.mk)`. The line starts with `$`, so an
        `^\s*include`-anchored regex never sees it, while make performs the
        include perfectly well (proved by the included file's rule appearing
        in `make -pn`).
      * `$(call value,LATE)`. `call` invokes the builtin by name, so this IS
        `$(value LATE)` -- the control prints `changed.c` for both -- but a
        regex demanding `value` immediately after `$(` cannot match it.

    So the include scan is GONE too. The file list now comes from make's own
    `MAKEFILE_LIST`, read out of `make -pn`: whatever route make took to read
    a file -- a continued directive, an eval, a computed name -- the file is
    in that list, because make put it there. There is no "unfollowable
    include" category left to get wrong, and a file make could NOT read makes
    make exit non-zero, which is already a refusal.

    ⚠ `make -np`'s variable dump is normally a CLEAN-BILL GENERATOR here and
    must not be used as evidence: it prints every variable's FINAL value after
    the whole file is parsed, so a late-defined prerequisite variable shows a
    perfectly good path for a rule that never had the dependency. That warning
    is about VALUES over TIME. `MAKEFILE_LIST` is a different kind of fact --
    "which files did you read?" has no timing component, and the post-parse
    answer is the complete one. Nothing else is taken from the dump.

    The `value` check stays a text scan, because no make flag warns on it, but
    it now matches the two positions in which make can name a function:
    directly (`$(value X)`, `${value X}`) and through `call`
    (`$(call value,X)`). Measured: 0 hits on the audited Makefile, and prose
    like "the return value of foo" does not match. It runs over LOGICAL lines
    -- backslash continuations joined, and `define` bodies merged -- so no
    spelling of "split the call across a newline" evades it; see
    logical_lines() for the four rounds that took.

    ⚠ IRREDUCIBLE RESIDUE, stated rather than discovered later: a function
    name ASSEMBLED at runtime (`V := val$(nothing)ue`, then
    `$(call $(V),LATE)`) contains the word nowhere and is undetectable by any
    text scan. That is accepted, and the reason is the tool's purpose: this
    gate detects an ACCIDENTAL late-defined prerequisite, and nobody writes
    that by accident. It is not a defence against someone deliberately hiding
    a stale dependency from the gate.

THREAT MODEL -- WHAT THIS TOOL CLAIMS, AND WHAT IT DOES NOT
    Stated plainly because nine rounds of findings have been argued without it,
    and because a reviewer cannot judge a finding without knowing the claim.

    IT GUARDS OUR Makefile AGAINST AN ACCIDENTAL LATE-DEFINITION REGRESSION.
    Someone moves a variable below the rule that uses it, the prerequisite list
    silently empties, and the guard stops re-running. That is the failure this
    exists to catch, it has happened here (fw#904), and `make
    --warn-undefined-variables` catches it by construction.

    IT IS NOT A PROOF AGAINST AN ADVERSARIAL MAKEFILE. Someone who WANTS to
    hide a stale dependency from this gate can: assemble a function name at
    runtime so no token scan sees it, or find a construct the policy does not
    yet refuse. The design answer to that is not a better scanner -- it is the
    POLICY: anything this tool cannot reason about is NO VERDICT (exit 2), so
    the way to defeat it is to make it refuse, which is loud, rather than to
    make it pass, which would be silent. Every round so far has moved a
    construct from the second category into the first.

    SO A FINDING AGAINST THIS TOOL IS SERIOUS IF it makes the tool report
    `warnings : 0` and exit 0 on a Makefile whose guard rule has lost a
    prerequisite -- a false CLEAN. It is comparatively minor if it makes the
    tool refuse something legal, because a refusal is visible and fixable. And
    it is out of scope if it requires deliberately hiding a dependency, since
    the gate protects against accident, not malice.

THE RESIDUAL CLASS, AND WHY IT IS DOCUMENTED RATHER THAN CHASED
    Rounds 9-11 of this PR's audit each found further Make constructs that
    reach a late-defined prerequisite by a route the scan does not see. They
    fall into four families, and the list is recorded because an undocumented
    residual invites exactly the trust this tool must not be given:

      1. DEFERRED EVALUATION -- text loaded or evaluated at expansion time.
      2. INDIRECTION THROUGH A COMPUTED NAME -- a function name assembled at
         runtime, which no token scan can resolve.
      3. RECURSION -- a submake, whose own makefiles this audit never reads.
      4. DEFINEDNESS WITHOUT REFERENCE -- asking whether a name is defined
         rather than using it, which emits no warning to see.

    ⛔ THE CONSTRUCTS THEMSELVES ARE NOT LISTED HERE. They are in
    `REFUSED_EXAMPLES`, with a near-miss control for each family in
    `POLICY_NEAR_MISSES`, and the self-test asserts every row against the
    named rule.

    ⛔⛔ AND THAT LIST IS NOT EXHAUSTIVE OVER MAKE. It is exhaustive over this
    tool's own RULES -- a self-test arm asserts every POLICY rule is exercised
    by some row -- which is a different and far weaker claim. Round 13 found
    SEVEN constructs the predicates do not recognise, and r14 raised six more.
    They are stated as SIX classes in the RESIDUAL CLASSES block above
    `POLICY_NEAR_MISSES`: call-indirection to a refused builtin, BOM or
    recipe-prefix lexing, shell-keyword or non-wrapper recursion, file/eval
    text loading, definedness tests other than origin/ifdef (`$(flavor)`), and
    includes outside the workflow's path filter. Those classes carry
    ACCEPTED-RESIDUAL dispositions on this PR; each is a false negative of a
    check `main` does not have, not a regression.

    That indirection is the fix for a defect this docstring HAD. Until r12 it
    listed `/usr/bin/make`, `command make` and an `origin()` definedness test
    as refused, and the policy refused none of the three -- prose ahead of
    code, for the second time in this PR. A hand-maintained list beside the
    predicates is a second copy of the claim, and a second copy can drift; the
    durable fix is to have one copy and assert it. Add a row to
    `REFUSED_EXAMPLES` without the matching predicate and the self-test fails
    on that row, so the claim and the behaviour cannot separate.

    What the four families BOUND, rather than enumerate: a construct that
    assembles the refused token out of fragments, or reaches it through a shell
    variable, is invisible to a token scan by construction. The recursion
    rule's wrapper set (`env`, `command`, `exec`, `nice`, `time`, `sudo`,
    `stdbuf`) is likewise a list, so a submake behind some other wrapper reads
    as an ordinary command. These are the residual, and they are the reason the
    paragraph below is stated rather than implied.

    ⭐ A MISS IN ANY OF THESE IS A FALSE NEGATIVE OF A NEW CHECK, NOT A
    REGRESSION. `main` has no such gate at all, so the comparison for "is this
    safe to land" is against ZERO coverage, not against a working check that
    this one weakens. Every refusal above strictly increases what the gate
    catches relative to main; what the residual class bounds is how much
    FURTHER it could go, not whether it is worse than before.

    Each family is also only reachable by writing the construct deliberately
    or by a Makefile style this repo does not use -- all four measure 0 hits on
    the audited file. Combined with the threat model above (accident, not
    adversary), that is why the list is published and the hardening stops here
    rather than continuing to enumerate Make.

WHAT THIS DOES NOT COVER. It answers "can this guard fire at all?" -- does the
rule's prerequisite list actually name the source it guards. It does NOT answer
"is this guard ever RE-EVALUATED?": a rule omitting $(RECIPES) (the Makefile
itself) does not re-run when the guard's own recipe is edited. run_947_tests is
currently the only one of the run_* rules omitting it, which is issue #1170
item (a) -- live on main, correctly not reported here, and a deliberate
follow-up rather than part of this tool.
"""
import contextlib
import io
import os
import re
import subprocess
import sys
import tempfile

DEFAULT_MAKEFILE = os.path.join("tests", "host", "Makefile")

# GNU make's own wording, captured with file:line so a finding is clickable.
WARN_RE = re.compile(
    r"^(?P<file>[^:]+):(?P<line>\d+):\s*warning:\s*"
    r"undefined variable ['\"](?P<var>[^'\"]+)['\"]")

# Reads a variable's text without REFERENCING it, so
# --warn-undefined-variables is silent whether or not it is defined yet. Both
# positions in which make can name a function: directly, and through `call` --
# `$(call value,X)` IS the builtin (measured: both spellings expand alike).
VALUE_RE = re.compile(r"[$][({]\s*(?:call\s+)?value\b")

# A `define` body holds REAL newlines, not continuations, and make still
# accepts a function call split across one -- so the body is one scanning unit.
# ⚠ ANY PREFIX FORM counts: `override define`, `export define`, `private
# define` are all defines, and an `^\s*define`-only regex missed every one.
DEFINE_RE = re.compile(
    r"^\s*(?:(?:override|export|private|unexport)\s+)*define\b")
ENDEF_RE = re.compile(r"^\s*endef\b")

# ⛔ THE POLICY, AND WHY THE TOOL HAS ONE NOW.
#
# Five audit rounds (r1, r3, r4, r5, r6) found the same defect: a hand-written
# Make lexer losing to Make's grammar. Each round fixed the instance and the
# next found the next spelling -- a continued rule, a continued include, a
# split $(call value, a define body, then `override define`, nested defines,
# backslash PARITY (`\\` at end of line is a literal backslash, not a
# continuation), and recursive $(MAKE). Fixing forms one at a time is a bet
# that Make's syntax can be enumerated, and the measured record on that bet is
# 0 for 5.
#
# So the burden is inverted. The scanner is correct over a SUBSET of Make, and
# the audited Makefile must stay inside it; anything outside is NO VERDICT.
# The direction of failure is what makes this sound: a policy predicate that
# MIS-FIRES refuses a legal Makefile -- loud, visible, fixable -- where a lexer
# that mis-parses passes an evasion silently, which is the entire defect class.
# Seeing a construct is also a far weaker requirement than interpreting it.
#
# ⚠ THE COST, STATED: this tool will refuse Makefiles that are legal Make.
# That is deliberate for a dev-tooling gate on ONE file. Measured on
# tests/host/Makefile (4102 lines, 1827 non-recipe): every predicate below
# matches 0 lines, so it costs nothing today. Whoever later needs one of these
# constructs either extends the policy deliberately or loses this gate's
# verdict -- and the refusal says which construct and why.
# ⛔ NO COMMENT OR RECIPE EXEMPTION, AND THAT IS THE POINT.
#
# r6 scoped the policy to non-recipe, comment-stripped lines, and r8 found both
# halves of that scoping wrong: a TAB-INDENTED ASSIGNMENT is not a recipe (make
# accepts one before any rule), and a `#` inside `$(subst #,...)` is not a
# comment, so the stripper truncated the line and hid what followed. The scope
# filter had become the lexer treadmill's new home.
#
# Both exemptions existed only because the `value` predicate was too broad --
# it matched the word anywhere after any `$(`, which hit two innocent lines.
# The precise predicate needs no exemption: every route to the builtin puts the
# literal token in a CALL POSITION. Measured over all 4102 lines of the audited
# Makefile with NO stripping and NO exemption: 0 hits for every predicate here.
#
# So is_recipe_line() and strip_make_comment() are GONE, and with them both r8
# findings. The policy reads logical lines exactly as they are written.
# ⛔ THE TWO HALVES OF A SUBMAKE INVOCATION, named so the recursion rule and
# its near-miss pins read against the same text rather than two copies of it.
#
# _MAKE_PREFIX: what may stand between the tab (or a shell operator) and the
# make token -- recipe sigils, variable assignments, and wrapper words. The
# wrapper set is a LIST, which means it is also a residual: a submake behind a
# wrapper nobody named here, or behind a shell variable, is not distinguishable
# from any other command. That bound is stated in the module docstring.
_MAKE_PREFIX = (r"[\s@+-]*(?:\w+=\S*\s+"
                r"|(?:env|command|exec|nice|time|sudo|stdbuf)\s+"
                r"(?:-\S+\s+)*)*")

# _MAKE_TOKEN: the make program itself. The path alternative must end in a
# slash, which is what keeps `cmake`, `/usr/bin/cmake` and `./cmake` out while
# admitting `/usr/bin/make`; `\b` after `make` is what keeps `makefile` out.
# `$(MAKE` carries NO trailing boundary, deliberately, so `$(MAKE_COMMAND)` is
# caught -- that `\b` was the whole hole in r9.
_MAKE_TOKEN = r"(?:(?:[\w.+-]*/)+g?make\b|g?make\b|\$[({]\s*MAKE)"

POLICY = (
    ("define body", DEFINE_RE,
     "a `define` body -- bodies hold real newlines, nest, and take prefix "
     "forms, and this scanner is only correct without them"),
    ("endef", ENDEF_RE,
     "an `endef` -- see `define body`"),
    ("value call", VALUE_RE,
     "a `value` call -- the builtin is invisible to "
     "--warn-undefined-variables however it is spelled, so it is never "
     "auditable here. NOTE: this is matched in COMMENTS and RECIPES too, "
     "deliberately -- the scanner no longer tries to decide what is a comment "
     "or a recipe, because getting that wrong is what hid a real `value` call. "
     "If this hit is a comment, REWORD THE COMMENT rather than looking for a "
     "defect that is not there"),
    # ⛔ ANY `call` WHOSE FIRST ARGUMENT IS NOT A LITERAL IDENTIFIER, plus
    # `call call,` and `call value,`. One predicate instead of three, because
    # enumerating the spellings is the treadmill: `$(call $(FN),X)`,
    # `$(call ${FN},X)` and the single-char `$(call $F,X)` all reach the
    # `value` builtin with the word appearing nowhere, and `$(call call,...)`
    # launders it through another level. Requiring a literal identifier
    # refuses every one of them without naming any.
    #
    # ⚠ Deliberately NOT a bare `$X` predicate: `$$n` appears 421 times in
    # this Makefile as an escaped SHELL variable, which is not a make
    # reference at all. A single-char reference only matters where it names a
    # FUNCTION, which is exactly where this looks.
    ("call with a non-literal function name",
     re.compile(r"[$][({]\s*call\s+"
                r"(?:(?!(?:[A-Za-z_][A-Za-z0-9_]*)\s*,)|(?:call|value)\s*,)"),
     "a `call` whose function name is not a literal identifier (or is `call` "
     "or `value`) -- it can reach the `value` builtin with the word appearing "
     "nowhere, so no token scan can see it"),
    # ⛔ RECURSION IS REFUSED, NOT TRACKED. r7 bound the file list to the
    # makefile actually invoked, which fixed reading the CHILD's list by
    # mistake -- and then the child's own sources were never scanned at all,
    # so a recursive build could hide anything in a submake. Rather than
    # recurse the scan (another parser), a file that recurses gets NO VERDICT.
    # `$(MAKE)` anywhere is unambiguous; `make` is matched only in COMMAND
    # position, because the word appears in English inside echo strings here.
    # ⛔ RECURSION, AFTER ANY PREFIX. r9 matched `make` only after [-@+], so
    # `CC=cc make`, `+env make`, `+gmake` and `+$(MAKE_COMMAND)` all walked
    # past it. The prefix set is now variable assignments and `env` as well,
    # and `$(MAKE` is matched WITHOUT a trailing word boundary so
    # `$(MAKE_COMMAND)` is caught too -- the `\b` was what let it through.
    # ⛔ RECURSION BY PATH OR BY WRAPPER WORD. r12 walked two more spellings
    # past r9's form, and the DOCSTRING had already claimed both were refused:
    # `/usr/bin/make` failed because the pattern required `make` at the start
    # of the token, and `command make` failed because the only wrapper forms
    # accepted were `VAR=value` and bare `env`. Both are now in the prefix.
    #
    # ⚠ The directory prefix is `(?:[\w.+-]*/)+`, which must end in a SLASH.
    # That is what keeps `cmake` out: with no slash the alternative cannot
    # apply at all, and `g?make\b` then has to match from the token's first
    # character, where it meets `c`. `/usr/bin/cmake` fails the same way after
    # backtracking. Pinned in POLICY_NEAR_MISSES.
    ("recursive make",
     re.compile(r"^\t" + _MAKE_PREFIX + _MAKE_TOKEN
                + r"|[;&|]\s*" + _MAKE_PREFIX + _MAKE_TOKEN),
     "a recipe invoking make (any of `make`, `gmake`, `$(MAKE...)`, a "
     "path-qualified `/usr/bin/make`, or one behind a wrapper word such as "
     "`command`, after any prefix) -- the child's own makefiles are not "
     "scanned, so this audit cannot speak for the whole build"),
    # ⛔ `$(origin X)` IS A DEFINEDNESS TEST, and it is the same hole as a
    # conditional reached by a different route: it reports whether and how a
    # variable is defined, and --warn-undefined-variables does NOT warn for
    # it, because asking about a name is not referencing it. So
    # `ifeq ($(origin LATE),undefined)` -- or a bare `X := $(origin LATE)` --
    # branches on a late definition with nothing on stderr.
    #
    # ⚠ The conditional rule already covered the `ifeq` spelling, but only
    # INCIDENTALLY: it fires on the `ifeq` token, not because `origin` was
    # recognised, so the assignment form was unrefused. Measured on
    # 55ac3485d: `X := $(origin LATE)` scored NO POLICY HIT.
    #
    # Case-sensitive on purpose -- make's builtins are lowercase, so a
    # variable named `$(ORIGIN)` is not this, and neither is `$(origin_dir)`,
    # where `\b` fails against the underscore. Both pinned as near-misses.
    ("origin definedness test", re.compile(r"[$][({]\s*origin\b"),
     "an `$(origin ...)` definedness test -- it reports whether a name is "
     "defined without referencing it, so --warn-undefined-variables stays "
     "silent and a late definition can be branched on unseen"),
    # ⛔ $(file ...) and $(eval ...) BOTH LOAD TEXT MAKE_FILE_LIST NEVER SEES.
    # `$(eval $(file <rules.mk))` reads a file and evaluates it as makefile
    # text without ever being an `include`, so it appears in no file list and
    # the scan cannot reach it. Neither function is worth modelling; both are
    # refused. 0 uses in the audited Makefile.
    ("file/eval text loading", re.compile(r"[$][({]\s*(?:file|eval)\b"),
     "`$(file ...)` or `$(eval ...)` -- either can introduce makefile text "
     "that never appears in MAKEFILE_LIST, so no file-list-driven scan can "
     "see it"),
    # ⛔ CONDITIONALS, and this one is NOT an adversarial construct: `ifdef
    # LATE` placed before LATE's definition silently drops the prerequisite,
    # which is an ORDINARY late-definition regression -- exactly the defect
    # class this gate exists for, reached by a route the warning cannot see
    # (ifdef tests definedness and does not WARN about it). Deciding whether
    # each tested name is defined later would be another Make model, so the
    # whole construct is refused. Measured: the audited Makefile contains 0
    # conditionals, so this costs nothing today.
    ("conditional", re.compile(r"^\s*(?:else\s+)?if(?:n?def|n?eq)\b"),
     "a conditional (ifdef/ifndef/ifeq/ifneq) -- `ifdef LATE` before LATE is "
     "defined drops the prerequisite silently and make does not warn, and "
     "resolving that per-name would mean modelling Make again"),
)

# ⛔⛔ THE REFUSAL LIST LIVES HERE, AND NOWHERE ELSE. This is the only
# enumeration of what the policy refuses; the module docstring points at it
# instead of repeating it, and the self-test asserts every row.
#
# WHY IT IS A TUPLE AND NOT PROSE. r12 found two constructs the DOCSTRING
# listed as refused -- `/usr/bin/make` and `command make` -- which the code did
# not refuse, plus an `origin()` test the docstring named in the same breath.
# That was the SECOND prose-ahead-of-code finding in this PR. Fixing the two
# instances while keeping a hand-maintained prose list would have produced a
# third, because a second copy of a claim is a thing that can drift; the only
# durable fix is to stop having one. A row added here without the matching
# predicate fails the self-test immediately, so the claim and the code cannot
# separate: the check's failure co-occurs with the thing it guards.
#
# Each row is (sample line, the POLICY name that must refuse it). Naming the
# RULE rather than just "something refused it" is deliberate -- `$(origin X)`
# inside an `ifeq` is refused by the conditional rule whether or not the origin
# rule exists, so a test that only asked "refused?" would have passed on
# 55ac3485d and proved nothing about the gap.
REFUSED_EXAMPLES = (
    ("\tmake -C sub all", "recursive make"),
    ("\tgmake -C sub all", "recursive make"),
    ("\t$(MAKE) -C sub all", "recursive make"),
    ("\t+$(MAKE_COMMAND) -f child.mk all", "recursive make"),
    ("\tCC=cc make all", "recursive make"),
    ("\t+env make all", "recursive make"),
    # The two r12 found, both previously claimed in prose and unrefused.
    ("\t/usr/bin/make -C sub all", "recursive make"),
    ("\tcommand make -C sub all", "recursive make"),
    # Same family, same route, so they are pinned rather than left to a later
    # round to re-find one at a time.
    ("\t/bin/make all", "recursive make"),
    ("\t./make all", "recursive make"),
    ("\tcommand -p make all", "recursive make"),
    ("\texec make all", "recursive make"),
    ("\tcd sub && make all", "recursive make"),
    ("\ttest -d sub && $(MAKE) -C sub", "recursive make"),
    # The origin family -- the assignment form is the one that was open.
    ("X := $(origin LATE)", "origin definedness test"),
    ("X := $(origin  LATE)", "origin definedness test"),
    ("X := ${origin LATE}", "origin definedness test"),
    ("ifeq ($(origin LATE),undefined)", "origin definedness test"),
    # Pre-existing families, pinned here so that every POLICY RULE is exercised
    # by some row and a rule deleted elsewhere cannot pass by being absent from
    # this table.
    #
    # ⛔ THAT IS COMPLETENESS OVER THE RULES, NOT OVER MAKE. This table says
    # what the eight predicates refuse; it says NOTHING about which constructs
    # ought to be refused, and r13 found seven it does not recognise. An
    # earlier wording here claimed the list was "the WHOLE claim", which reads
    # as exhaustive over Make and is false. See the RESIDUAL CLASSES block
    # above POLICY_NEAR_MISSES.
    ("define body\n", "define body"),
    ("endef", "endef"),
    ("X := $(value LATE)", "value call"),
    ("X := $(call value,LATE)", "value call"),
    ("X := $(call $(FN),LATE)", "call with a non-literal function name"),
    ("X := $(call call,value,LATE)", "call with a non-literal function name"),
    ("X := $(file <rules.mk)", "file/eval text loading"),
    ("$(eval include rules.mk)", "file/eval text loading"),
    ("ifdef LATE", "conditional"),
    ("ifndef LATE", "conditional"),
    ("else ifeq ($(A),$(B))", "conditional"),
)

# ⛔⛔ WHAT THIS GATE DOES NOT CATCH, stated because an undocumented residual
# invites exactly the trust this tool must not be given. Round 13 found seven
# constructs the predicates do not recognise and r14 found six more; they fall
# into SIX classes, and the classes are the durable statement -- enumerating
# members one round at a time is the treadmill this tool's whole design
# rejects. Class 5 was added in r14 after I mis-filed its member under class 1
# in r13; the count moved because a class was MISSING, not because a member
# was.
#
# ⛔ SEPARATE AND *NOT* IN THIS LIST: the tool EXECUTES part of the Makefile it
# audits, because `-n` does not suppress `+`-prefixed or `$(MAKE)` recipe
# lines. That is not a scanner false negative -- it was a false SAFETY CLAIM in
# run_make's docstring, and it is corrected there rather than accepted here.
#
# ⛔ A MISS IN ANY OF THESE IS A FALSE NEGATIVE OF A CHECK `main` DOES NOT
# HAVE. The comparison for "is this safe to land" is against ZERO coverage,
# not against a working check this one weakens. None of them is a regression.
#
#   1. CALL-INDIRECTION TO A REFUSED BUILTIN. The builtin rules match a
#      DIRECT invocation, so `$(call origin,X)`, `$(call eval,...)` and
#      `$(call file,...)` reach a refused builtin through a literal, legal
#      function name that the non-literal-call rule correctly admits.
#      Refusing every `$(call <builtin>,...)` means enumerating Make's
#      builtins, which is the model this tool exists not to be.
#   2. LEXING ASSUMPTIONS -- A BOM OR A CUSTOM RECIPE PREFIX. The conditional
#      rule is anchored with `^\s*`, and a UTF-8 BOM is not whitespace, so a
#      conditional on line 1 of a BOM'd file is unseen. The recursion rule is
#      anchored to a literal TAB, so `.RECIPEPREFIX := >` moves every recipe
#      out from under it.
#   3. RECURSION BEHIND A SHELL KEYWORD OR AN UNLISTED WRAPPER. The wrapper
#      set is a LIST (`env`, `command`, `exec`, `nice`, `time`, `sudo`,
#      `stdbuf`), so `if true; then $(MAKE) ...`, a `for ... do $(MAKE)`, a
#      submake reached through a shell variable, or one behind any wrapper
#      nobody named reads as an ordinary command.
#   4. FILE/EVAL TEXT LOADING, including through class 1. Text introduced by
#      `$(eval ...)`/`$(file ...)` never enters MAKEFILE_LIST, so no
#      file-list-driven scan can reach it -- and a token assembled from
#      fragments is invisible to a token scan by construction.
#   5. DEFINEDNESS TESTS OTHER THAN `origin`/`ifdef` -- `$(flavor X)` is the
#      measured member. It reports whether and how a name is defined without
#      referencing it, so --warn-undefined-variables stays silent exactly as
#      with `$(origin X)`, and it is NOT refused.
#      ⛔ THIS CLASS EXISTS BECAUSE I MIS-FILED IT. In r13 I dispositioned the
#      `$(flavor X)` finding under class 1, call-indirection -- which is wrong:
#      the finding was a DIRECT `$(flavor X)`, with no `call` in it at all. The
#      five classes as first written therefore did not name it, and r14 raised
#      it again. The family is "a builtin that queries definedness", and
#      `origin` is refused by name while its siblings are not; enumerating
#      them one per round is the treadmill, so the CLASS is named instead.
#   6. INCLUDES OUTSIDE THE WORKFLOW'S PATH FILTER. The audit measures every
#      file in make's MAKEFILE_LIST, but CI only RUNS on the paths
#      `.github/workflows/make-prereq.yml` names. An include whose path or
#      extension falls outside them (anything not `tests/host/Makefile`,
#      `tests/host/*.mk` or `tests/host/**/*.mk`) is measured but unwatched.
#      Tracked as #1178. Measured today: MAKEFILE_LIST is exactly ONE file and
#      the Makefile has 0 include directives, so the gap is forward-looking.
#
# Each class is also a reason it is not cheaply fixable: 1 and 4 need a model
# of Make's builtins, 2 needs a lexer, 3 needs to distinguish a shell command
# from a submake, and 5 is a CI-trigger property rather than a scanner one.
#
# ⚠ Deliberately a COMMENT and not a module-level tuple. The five names would
# read better as data, but this round's disposition is carry-forward-eligible
# only while the executable AST is unchanged, and a new assignment is a new
# node. The list has no reader in code -- it is a statement to a human about
# what the gate does not claim -- so prose costs nothing here.

# ⛔ EVERY REFUSAL NEEDS A NEAR-MISS, or a rule that refuses everything
# satisfies every row above. These must match NO policy rule at all.
#
# ⚠ `make-dist` is NOT here: `make\b` finds the boundary before the hyphen, so
# it IS refused today. That is a false positive, and it is left standing on
# purpose -- a refusal is this gate's loud direction, and narrowing the token
# to exclude it would risk admitting a real submake spelled with a suffix. It
# is pinned as a KNOWN false positive below so a future round does not read it
# as a fresh defect.
POLICY_NEAR_MISSES = (
    ("\tcmake -B build", "cmake is not make -- the path alternative needs a "
                         "slash, so it cannot apply, and g?make\\b then meets "
                         "the leading `c`"),
    ("\t/usr/bin/cmake -B build", "a path-qualified cmake fails the same way "
                                  "after backtracking"),
    ("\t./cmake -B build", "and so does a relative one"),
    ("\techo makefile", "`makefile` has no word boundary after `make`"),
    ("\techo 'run make later'", "the word make in an echo string is not a "
                                "command position"),
    ("X := $(ORIGIN)", "a variable named ORIGIN is not the lowercase builtin"),
    ("X := $(origin_dir)/f.c", "`origin_dir` fails \\b against the "
                               "underscore"),
    ("X := $(wildcard *.c)", "an ordinary function call is not refused"),
    ("EARLY := base.c", "a correctly-ordered assignment is the common case "
                        "and must stay silent"),
    ("\t$(CC) -o $@ $^", "an ordinary recipe line is not a submake"),
)

# Pinned as a KNOWN false positive, in the refusal (safe) direction. Asserted
# refused so that if a future narrowing of the make token lets it through, the
# self-test says so and the author has to decide deliberately.
POLICY_KNOWN_FALSE_POSITIVES = (
    ("\tmake-dist --all", "recursive make",
     "`make\\b` matches before the hyphen. Refusing a non-submake costs a "
     "NO VERDICT on a file nobody has; admitting a real submake costs the "
     "audit's whole claim, so the token is not narrowed to exclude it"),
)

# Backslash PARITY, which only a physical line can answer: `\\` at end of line
# is an escaped backslash and NOT a continuation, so phase 1's joiner would
# wrongly glue the next line on. Rejected rather than parsed.
TRAILING_BSLASH_RUN_RE = re.compile(r"\\{2,}[ \t]*$")

# make's own record of every file it read, out of its `-p` dump. See the module
# docstring for why this ONE line of that dump is evidence when the rest is a
# clean-bill generator: "which files did you read?" has no timing component.
MAKEFILE_LIST_RE = re.compile(r"^MAKEFILE_LIST\s*:?=\s*(?P<files>.*)$")


def classify_warnings(stderr):
    """(located, unlocated) from make's stderr. Pure, so arms pin the RETURN.

    ⛔ EXTRACTED BECAUSE THE DECISION IS THE THING WORTH PINNING. Forcing real
    make to emit a prefix-less warning is not reproducible in a fixture, so a
    structural pin would only have shown that the loop EXISTS -- never that it
    classifies. A mutant that drops the `unlocated` branch passes the former
    and fails the arms below.

    `located` are the ones WARN_RE can point at. `unlocated` is every other
    line mentioning a warning: make does not always emit a `file:line:` prefix,
    and a warning this tool cannot PARSE is not a warning it may IGNORE -- that
    is the silent-pass shape every finding in this file has had.
    """
    located, unlocated = [], []
    for ln in (stderr or "").splitlines():
        stripped = ln.strip()
        m = WARN_RE.match(stripped)
        if m:
            located.append(m.groupdict())
        elif "warning:" in stripped.lower():
            unlocated.append(stripped[:200])
    return located, unlocated


def run_make(makefile, goal="run"):
    """(warnings, stderr, returncode). Asks make; parses no Makefile syntax.

    `env -i` so an exported variable cannot define the defect away. `-n` so
    that ALMOST nothing is built: the warnings happen at parse time, before
    most recipes would run.

    ⛔⛔ `-n` DOES NOT MEAN NOTHING EXECUTES, AND AN EARLIER VERSION OF THIS
    DOCSTRING SAID IT DID. GNU make deliberately runs two kinds of recipe line
    even under `-n`:

      * a line prefixed with `+`, which means "run this even with -n"; and
      * a line invoking `$(MAKE)`, because make passes `-n` down and lets the
        sub-make really run.

    That is documented GNU behaviour, not a quirk, and THIS FILE ALREADY
    MEASURED IT -- see the comment in `gather_sources` about the child's
    database printing before the parent's, which only happens because the
    sub-make ran. The docstring claimed the opposite fifty lines away, so the
    file both knew and denied the fact (#1174 r14, raised HIGH by both legs).

    ⛔ SO THIS TOOL EXECUTES PART OF THE MAKEFILE IT AUDITS, and it cannot do
    otherwise: the policy needs make's own MAKEFILE_LIST, which only running
    make can produce. `report()` invokes make at least twice per run (here and
    in `gather_sources`), so such a line runs more than once.

    ⚠ THE OPERATIONAL RULE THAT FOLLOWS: point this only at a Makefile you
    would be willing to BUILD. That is not a new hazard in CI, where the
    `tests/host` job already builds and runs this Makefile, and the audited
    Makefile carries 0 `+` lines and 0 `$(MAKE)` lines today. The defect was
    the false safety claim, not a new capability.

    ⚠ NOT fixed by pre-scanning for `+`/`$(MAKE)` before invoking make. A text
    pre-scan is evadable by the very classes this PR accepted as residual --
    `.RECIPEPREFIX` moves what counts as a recipe line, and a BOM defeats an
    anchored rule -- so it would buy a safety CLAIM rather than a safety
    PROPERTY, which is the exact defect being fixed here. Stating the truth is
    worth more than a guard that can be walked around.
    """
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin")}
    proc = subprocess.run(
        ["make", "-n", "--warn-undefined-variables",
         "-f", os.path.basename(makefile), goal],
        cwd=os.path.dirname(makefile) or ".",
        capture_output=True, text=True, env=env,
    )
    # ⛔ EVERY `warning:` LINE COUNTS, LOCATED OR NOT. WARN_RE needs a
    # `file:line:` prefix, and make does not always emit one -- a warning from
    # a command-line or environment-originated variable, or any wording this
    # regex does not anticipate, arrived with no prefix and was DROPPED. A
    # warning the tool cannot parse is not a warning the tool may ignore:
    # that is the same silent-pass shape as every other finding here.
    #
    # So unlocated warnings come back separately and the caller fails on them
    # too. They cannot carry a ::error file=,line=:: annotation (there is
    # nothing to point at), which is exactly why they must be printed loudly
    # instead of quietly discarded.
    warnings, unlocated = classify_warnings(proc.stderr)
    return warnings, unlocated, proc.stderr, proc.returncode


def gather_sources(makefile, goal="run"):
    """(files whose TEXT must be scanned, reasons the list is untrustworthy).

    ⛔ THE FILE LIST COMES FROM MAKE, NOT FROM READING `include` LINES. Three
    separate evasions of the old text scan were reproduced on the real file --
    a continued `include\\` directive, `$(eval include ...)`, and an include
    naming a variable -- and every one of them produced a GREEN audit over a
    file whose text was never examined. Asking make removes the whole class:
    MAKEFILE_LIST holds every file make read, by whatever route, because make
    is what put them there.

    A missing or unparseable MAKEFILE_LIST is a REASON, never an empty list:
    an audit over zero files would otherwise be the cleanest result there is.

    ⛔⛔ AND THIS IS THE CALL THAT EXECUTES PART OF THE MAKEFILE. Asking make
    means RUNNING make, and `-pn` does not stop a `+`-prefixed recipe line or
    a `$(MAKE)` line -- GNU make runs both even under `-n`. The comment below
    about the child's database printing first is the measured PROOF of it: the
    sub-make really ran.

    ⛔ So the execution is UNAVOIDABLE here, not an ordering bug. The policy
    needs the file list, the file list comes from make, and make runs those
    lines on the way to producing it. There is no arrangement of this function
    that applies the policy before make has already run. See `run_make` for
    the operational rule that follows (audit only a Makefile you would build)
    and for why a pre-scan is not the fix. #1174 r14, HIGH on both legs.
    """
    root = os.path.dirname(os.path.abspath(makefile)) or "."
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin")}
    proc = subprocess.run(
        ["make", "-pn", "--warn-undefined-variables",
         "-f", os.path.basename(makefile), goal],
        cwd=root, capture_output=True, text=True, env=env,
    )
    # ⛔ BIND THE LIST TO THE MAKEFILE I INVOKED, not to whichever dump printed
    # first. A recursive `$(MAKE)` runs even under -n (make passes -n down and
    # the sub-make really runs), so the CHILD's database prints before the
    # parent's -- and taking the first MAKEFILE_LIST scanned only the child,
    # reporting clean on a parent it never read. That is not a lexer problem;
    # it is reading the right instrument's output, the same mistake as an
    # arbiter reasoning over another branch.
    #
    # The parent's list is the one whose FIRST entry is the file passed to -f.
    # Exactly one such list, or NO VERDICT: two identically-named makefiles in
    # a recursive build are not something to guess between.
    want = os.path.basename(makefile)
    candidates = []
    for ln in proc.stdout.splitlines():
        m = MAKEFILE_LIST_RE.match(ln)
        if m:
            files = m.group("files").split()
            if files and os.path.basename(files[0]) == want:
                candidates.append(files)
    listed = candidates[0] if len(candidates) == 1 else None
    if len(candidates) > 1:
        return [], [(makefile, f"make printed {len(candidates)} MAKEFILE_LISTs "
                               f"beginning with {want!r} (a recursive build "
                               f"with repeated makefile names), so which one "
                               f"describes the file under audit is ambiguous")]
    if listed is None:
        return [], [(makefile, "make printed no MAKEFILE_LIST, so WHICH files "
                               "it read is unknown -- scanning the named file "
                               "alone would be a guess")]

    files, problems, seen = [], [], set()
    for token in listed:
        path = os.path.abspath(os.path.join(root, token))
        if path in seen:
            continue
        seen.add(path)
        try:
            with open(path, "r", errors="replace") as fh:
                files.append((path, fh.read()))
        except OSError as exc:
            # make read it and we cannot: that is a refusal, not a skip.
            problems.append((path, f"make read this file but it is not "
                                   f"readable here: {exc.strerror}"))
    if not files and not problems:
        problems.append((makefile, "MAKEFILE_LIST named no readable file"))
    return files, problems


def auditable_lines(text):
    """[(lineno, text)] -- the ONE line set every scan here looks at.

    ⛔ ONE ITERATOR, AND NO EXEMPTIONS. Two separate things went wrong before.
    The policy and the `value` scan read DIFFERENT line sets, so a construct
    one exempted the other refused. And the exemptions were themselves a
    lexer: `is_recipe_line` called a TAB-INDENTED ASSIGNMENT a recipe (make
    accepts one before any rule), and `strip_make_comment` truncated at a `#`
    inside `$(subst #,...)`, hiding whatever followed -- the treadmill moved
    into the scope filter.

    Both are gone. Every scan reads logical lines as written, so a construct is
    in scope for everyone and there is nothing left to get wrong about what is
    a comment or a recipe. The predicates are precise enough not to need it:
    measured 0 hits for every one of them across all 4102 lines of the audited
    Makefile, with no stripping and no exemption.
    """
    return logical_lines(text)


def policy_violations(text):
    """[(lineno, kind, why, excerpt)] -- constructs this scanner refuses to
    reason about. Pure, so every predicate is pinned and mutated.

    Empty means the file is inside the subset the scanner is correct on. It is
    NOT a statement that the Makefile is good; it is a statement that the
    audit below is entitled to an opinion.
    """
    found = []
    # Parity is a property of a PHYSICAL line ending, so it is checked there.
    for lineno, raw in enumerate(text.splitlines(), 1):
        if TRAILING_BSLASH_RUN_RE.search(raw):
            found.append((lineno, "backslash parity", TRAILING_BSLASH_PARITY_WHY,
                          raw.strip()[:120]))
    # Everything else reads the shared auditable line set, so a construct
    # cannot hide behind a continuation and the two scans cannot diverge.
    for lineno, ln in auditable_lines(text):
        for kind, rx, why in POLICY:
            if rx.search(ln):
                found.append((lineno, kind, why, ln.strip()[:120]))
    return sorted(found)


TRAILING_BSLASH_PARITY_WHY = (
    "a line ending in two or more backslashes -- `\\\\` is an escaped "
    "backslash, NOT a continuation, and this scanner's joiner cannot tell "
    "the difference")


def logical_lines(text):
    """[(first physical line number, the LOGICAL line)] -- continuations joined.

    ⛔ SCAN LOGICAL LINES, NEVER PHYSICAL ONES. make joins a trailing backslash
    with the next line BEFORE it interprets anything, and a physical-line scan
    has now been defeated by that three separate times in this one tool:

      r1  the Makefile parser read only a rule's FIRST line, so five
          prerequisite variables on a continuation line were invisible.
      r3  `include\\` + newline + ` inc.mk` matched neither physical line, so
          the included file was never scanned and never reported.
      r4  `$(call \\` + newline + ` value,LATE)` splits the function name from
          `call`, so VALUE_RE matched neither line -- and the joined form is
          genuinely the builtin (measured: `joined=[x.c] direct=[x.c]`).
      r5  the same split inside a `define` BODY, where the newline is not a
          continuation at all, so phase 1 does not join it -- while make
          accepts the call anyway. Phase 2 below merges define bodies.

    Three spellings of one mistake, so this joins ONCE, here, and every scan
    runs over the result. The line number reported is the FIRST physical line
    of the logical line, which is where a reader has to go to edit it.

    Joined with a single SPACE, which is what make does (the backslash-newline
    and the next line's leading whitespace collapse to one space). That is also
    the safe direction: joining with nothing could GLUE two fragments into a
    keyword make never saw -- `$(va\\` + `lue X)` is `$(va lue X)` to make, not
    a `value` call at all.
    """
    def joined(parts):
        """First part as it stands; each continuation adds exactly one space."""
        out = parts[0]
        for part in parts[1:]:
            out += " " + part.lstrip()
        return out

    # Phase 1 -- backslash continuations.
    lines, buf, start = [], [], None
    for lineno, ln in enumerate(text.splitlines(), 1):
        if start is None:
            start = lineno
        if ln.endswith("\\"):
            buf.append(ln[:-1])
            continue
        buf.append(ln)
        lines.append((start, joined(buf)))
        buf, start = [], None
    if buf:                              # a trailing backslash at end of file
        lines.append((start, joined(buf)))

    # Phase 2 -- `define` bodies. A plain newline inside define..endef is NOT a
    # continuation, and phase 1 therefore leaves it alone -- but make accepts a
    # function call split across one, so `$(call` / newline / `value,X)` in a
    # define body stayed invisible after r4. Measured at r4's head: make exits
    # 0 with no warnings, the rule expands to `run: base.c` with the
    # prerequisite GONE, and moving the definition above the define brings it
    # back as `run: base.c changed.c` -- so it genuinely is the builtin.
    #
    # The body is emitted as ONE logical unit, reported at the `define` line,
    # which is where a reader goes to edit it. Joined with a space for the same
    # reason as phase 1: it cannot glue two fragments into a keyword.
    out, body, start = [], None, None
    for lineno, ln in lines:
        if body is None:
            if DEFINE_RE.match(ln):
                body, start = [ln], lineno
            else:
                out.append((lineno, ln))
            continue
        body.append(ln)
        if ENDEF_RE.match(ln):
            out.append((start, " ".join(p.strip() for p in body)))
            body, start = None, None
    if body:                             # an unterminated define at EOF
        out.append((start, " ".join(p.strip() for p in body)))
    return out


def find_evasions(makefile, goal="run"):
    """[(file, line, text)] for every `value` call, plus list-integrity problems.

    Returns (hits, problems). Either one means NO VERDICT: make's warnings
    cannot speak for a construct it does not report on, nor for text that was
    never read.
    """
    files, problems = gather_sources(makefile, goal)
    hits = []
    for path, text in files:
        # ⛔ THE SHARED LINE SET, not a second scope. A `value` mention in a
        # comment or a recipe is exempt here for exactly the reason the policy
        # exempts it, and scanning raw text made this scan refuse files the
        # policy had deliberately passed.
        for lineno, ln in auditable_lines(text):
            if VALUE_RE.search(ln):
                hits.append((path, lineno, ln.strip()[:120]))
    return hits, problems


def report(makefile, goal="run"):
    print(f"makefile : {makefile}")
    print(f"goal     : {goal}  (warnings are goal-independent -- prerequisite "
          f"lists expand when each rule is READ)")

    # ⚠ "FIRST" HERE MEANS BEFORE THE WARNINGS LEG, *NOT* BEFORE MAKE RUNS.
    # The very next statement calls gather_sources, which invokes make -- and
    # `-n` does not suppress a `+`-prefixed or `$(MAKE)` recipe line. So by the
    # time any policy rule is evaluated, make has already run and those lines
    # with it. That ordering cannot be improved: the policy's input IS make's
    # MAKEFILE_LIST. Read this comment as "the policy gates the VERDICT", not
    # "the policy gates EXECUTION" -- an earlier reading of it as the latter is
    # what made run_make's "nothing is built" claim look consistent (r14).
    #
    # ⛔ THE POLICY RUNS FIRST, AND OVER EVERY DISCOVERED SOURCE -- not just
    # the root. Applying it to the root alone left the narrowing with a hole
    # the exact size of an `include`: an included file carrying nested defines
    # plus a split `value` call was handed straight to the lexer the policy
    # exists to stop trusting, and the lexer closes a nested define at the
    # INNER endef. A subset the build graph only partly obeys is not a subset.
    #
    # Sources come from make's own MAKEFILE_LIST, so "every file make read" is
    # make's answer, not a guess from reading include lines.
    sources, src_problems = gather_sources(makefile, goal)
    for path, why in src_problems:
        print(f"::error file={os.path.basename(path)}::the set of files make "
              f"actually read could not be established, so part of this "
              f"Makefile may never have been scanned ({why})")
    if src_problems:
        print(f"ERROR: {len(src_problems)} file-list problem(s). NO VERDICT.")
        return 2
    policy_hits = [(path,) + hit
                   for path, text in sources
                   for hit in policy_violations(text)]
    for path, lineno, kind, why, excerpt in policy_hits:
        print(f"::error file={os.path.basename(path)},line={lineno}::"
              f"outside the audited subset ({kind}): {why} -- {excerpt}")
    if policy_hits:
        print(f"ERROR: {len(policy_hits)} construct(s) outside the subset this "
              f"checker is correct on, across {len(sources)} file(s) make "
              f"read. NO VERDICT -- this is a deliberate narrowing after five "
              f"rounds of a hand-written Make lexer losing to Make's grammar; "
              f"extend the policy on purpose, or keep the build inside it.")
        return 2

    evasions, unresolved = find_evasions(makefile, goal)
    for path, lineno, text in evasions:
        print(f"::error file={os.path.basename(path)},line={lineno}::"
              f"a `value` call reads a variable without referencing it, so "
              f"--warn-undefined-variables is silent here whether or not it "
              f"is defined yet -- this audit cannot see this line: {text}")
    for path, why in unresolved:
        print(f"::error file={os.path.basename(path)}::the set of files make "
              f"actually read could not be established, so part of this "
              f"Makefile may never have been scanned ({why})")

    warnings, unlocated, stderr, rc = run_make(makefile, goal)
    print(f"warnings : {len(warnings)}")
    for w in warnings:
        print(f"::error file={w['file']},line={w['line']}::"
              f"prerequisite variable '{w['var']}' is not defined at this "
              f"point, so this rule's prerequisite list was EMPTY when make "
              f"read it -- the guard will not re-run when its source changes")
    for ln in unlocated:
        print(f"::error file={os.path.basename(makefile)}::make warned without "
              f"a file:line prefix, so it cannot be located -- and an "
              f"unparseable warning is not an absent one: {ln}")
    if unlocated:
        print(f"unlocated: {len(unlocated)} warning(s) make emitted with no "
              f"file:line prefix")
    if not warnings and not unlocated:
        print("  (none)")

    # ANY non-zero exit means make stopped before the end of the file, so the
    # list above is a PREFIX of it, not a verdict on it. Reporting that as
    # clean is how a Makefile make cannot even read scored 0 findings.
    if rc != 0:
        print(f"ERROR: make exited {rc} and did not finish reading this "
              f"Makefile, so the {len(warnings)} warning(s) above cover only "
              f"the part it reached. NO VERDICT -- absence of more is not "
              f"evidence of none.")
        print(stderr.strip()[:500])
        return 2
    if evasions or unresolved:
        print(f"ERROR: {len(evasions)} `value` call(s) and {len(unresolved)} "
              f"file-list problem(s). NO VERDICT -- this audit is only sound "
              f"over text make reports on, in files make says it read.")
        return 2
    if warnings or unlocated:
        return 1
    return 0


# ---------------------------------------------------------------------------
# The fixture carries all three evasions the parser version missed, so a
# regression to any of them fails here rather than in review.
SELFTEST_MAKEFILE = """\
EARLY := early.c

ok: base.c $(EARLY)
\t@true

bad_brace: base.c ${LATE1}
\t@true

bad_continuation: base.c \\
                  $(LATE2)
\t@true

recipe_only: base.c
\tLOCALVAR=x; echo $(RECIPE_LATE)

LATE1 := late1.c
LATE2 := late2.c
RECIPE_LATE := r.c
"""

# ---------------------------------------------------------------------------
# Fixtures for the arms that exercise report()'s EXIT CODE rather than
# run_make's warning list. The first three each scored a clean `exit 0` before
# the refusals existed, which is why they are here.

# make dies on line 2's `missing separator` and never reaches line 3, where a
# genuinely undefined prerequisite sits. 0 warnings is therefore "make did not
# look", not "there is nothing to find".
SELFTEST_TRUNCATED = """\
ok: base.c
   @true
bad: base.c ${LATE2}
\t@true
LATE2 := late2.c
"""

# Exactly the defect this gate is for, written so make says nothing: measured
# 0 warnings here against 1 for the same rule spelled ${LATE}.
SELFTEST_VALUE = """\
ok: base.c $(value LATE)
\t@true
LATE := late.c
"""

# The same evasion one `include` away. make exits 0 and warns 0 times, so only
# a scan that FOLLOWS the include can see it.
SELFTEST_INCLUDER = """\
include inc.mk
ok: base.c $(FROM_INC)
\t@true
"""
SELFTEST_INCLUDED = """\
FROM_INC := inc.c
other: base.c $(value LATE3)
\t@true
LATE3 := l3.c
"""

# The control. Three "must refuse" arms are all satisfied by a tool that
# refuses everything; this is the arm that makes them mean something.
SELFTEST_CLEAN = """\
EARLY := early.c

ok: base.c $(EARLY)
\t@true
"""

# ---------------------------------------------------------------------------
# The three evasions of the REFUSALS, each reproduced on the real file by an
# adversarial audit and each scoring a clean exit 0 before this round. All
# three were one mistake: a text scan deciding which files to read and which
# spelling counts.

# `call` invokes the builtin BY NAME, so this is $(value LATE) -- measured,
# both spellings expand to the same text -- but it does not follow `$(`.
SELFTEST_VALUE_VIA_CALL = """\
ok: base.c $(call value,LATE)
\t@true
LATE := late.c
"""

# A CONTINUED include directive: neither physical line matches an
# `^\\s*include\\s+` regex, while make follows it perfectly well.
SELFTEST_INCLUDE_CONTINUED = "include\\\n inc.mk\nok: base.c\n\t@true\n"

# An include make performs through eval. The line starts with `$`, so an
# anchored include regex never sees it.
SELFTEST_INCLUDE_EVAL = """\
$(eval include inc.mk)
ok: base.c
\t@true
"""

# The included file carries the evasion, so it is only found if the file list
# is right. Its own rule also proves the include really happened.
SELFTEST_INCLUDED_VALUE = """\
guard: $(value LATE)
\t@true
LATE := guarded.c
"""

# A continuation SPLITTING `call` from `value`. The joined form really is the
# builtin -- measured, `joined=[x.c] direct=[x.c]` -- and neither physical line
# matches on its own. The third continuation defeat of a physical-line scan in
# this tool, which is why logical_lines() exists.
SELFTEST_VALUE_CONTINUED = "ok: base.c $(call \\\n value,LATE)\n\t@true\nLATE := late.c\n"

# ⛔ THE CONTROL FOR THE JOIN ITSELF. make replaces a backslash-newline with a
# SPACE, so a word cannot be split across one: `$(va\\` + `lue X)` is
# `$(va lue X)` to make and is no `value` call at all. A join that glued the
# fragments would invent a refusal for a Makefile make reads as clean.
SELFTEST_SPLIT_WORD = "ok: base.c $(va\\\nlue LATE)\n\t@true\nLATE := late.c\n"

# r5: a `define` body's newlines are NOT continuations, so phase 1 leaves them
# -- and make accepts the call split across one anyway. Measured: the rule
# expands with the prerequisite GONE and make warns 0 times.
SELFTEST_VALUE_IN_DEFINE = """\
define GUARDED_SRC
$(call
value,LATE)
endef

ok: base.c $(GUARDED_SRC)
\t@true
LATE := late.c
"""

# ⛔ And the control for THAT: an ordinary `define` carrying no `value` call
# must still pass. Merging bodies to find one evasion must not make every
# define-using Makefile unauditable.
SELFTEST_CLEAN_DEFINE = """\
EARLY := early.c

define GUARDED_SRC
$(EARLY)
endef

ok: base.c $(GUARDED_SRC)
\t@true
"""


def _fixture_exit(files, goal="ok"):
    """(exit code, captured output) from the REAL report() over a fixture tree.

    report()'s printing is captured and shown only when its arm fails: the arm
    is decided by the exit code, because that is what CI consumes.
    """
    with tempfile.TemporaryDirectory() as d:
        for name, text in files.items():
            with open(os.path.join(d, name), "w") as fh:
                fh.write(text)
        for n in ("base.c", "early.c", "late.c", "late2.c", "inc.c", "l3.c"):
            p = os.path.join(d, n)
            if not os.path.exists(p):
                open(p, "w").close()
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = report(os.path.join(d, "Makefile"), goal=goal)
        return rc, buf.getvalue()


def self_test():
    """Four arms that MUST or MUST NOT warn, and the result DECIDES the exit.

    The previous version printed a make cross-check that never affected its
    exit code, so it advertised a control it did not have. Every arm here is
    returned.
    """
    with tempfile.TemporaryDirectory() as d:
        mk = os.path.join(d, "Makefile")
        with open(mk, "w") as fh:
            fh.write(SELFTEST_MAKEFILE)
        for n in ("base.c", "early.c", "late1.c", "late2.c", "r.c"):
            open(os.path.join(d, n), "w").close()
        warnings, _unloc, stderr, _ = run_make(mk, goal="ok")
        got = {w["var"] for w in warnings}

        arms = [
            ("POSITIVE  ${VAR} form", "LATE1" in got, True),
            ("POSITIVE  continuation line", "LATE2" in got, True),
            ("NEGATIVE  recipe-only reference", "RECIPE_LATE" in got, False),
            ("NEGATIVE  correctly-ordered variable", "EARLY" in got, False),
        ]
        print("--- self-test ---")
        ok = True
        for name, actual, want in arms:
            good = (actual == want)
            ok = ok and good
            print(f"  {'PASS' if good else 'FAIL'}  {name}: "
                  f"warned={actual}, wanted={want}")

        # The arms above score make's WARNINGS. These score report()'s EXIT,
        # which is a different question and the one CI acts on.
        refusal_arms = [
            ("REFUSE    make stopped early -- no verdict, not 'none'",
             {"Makefile": SELFTEST_TRUNCATED}, 2),
            ("REFUSE    $(value X) in the Makefile itself",
             {"Makefile": SELFTEST_VALUE}, 2),
            ("REFUSE    $(value X) in an INCLUDED file",
             {"Makefile": SELFTEST_INCLUDER, "inc.mk": SELFTEST_INCLUDED}, 2),
            ("CONTROL   a clean Makefile still exits 0",
             {"Makefile": SELFTEST_CLEAN}, 0),
            # The three that defeated the refusals themselves. Each exited 0.
            ("REFUSE    $(call value,X) is the builtin too",
             {"Makefile": SELFTEST_VALUE_VIA_CALL}, 2),
            ("REFUSE    a CONTINUED include is still an include",
             {"Makefile": SELFTEST_INCLUDE_CONTINUED,
              "inc.mk": SELFTEST_INCLUDED_VALUE}, 2),
            # ⚠ FROM r10 THIS ARM PASSES VIA THE POLICY, not via the
            # file-list scan: `$(eval` is now refused outright. It is kept
            # because the construct must still be refused, but it no longer
            # exercises MAKEFILE_LIST -- the plain-`include` arms above and
            # below are what still do that. Recording it so nobody reads a
            # green here as evidence for the file-list path.
            ("REFUSE    $(eval include ...) is still an include",
             {"Makefile": SELFTEST_INCLUDE_EVAL,
              "inc.mk": SELFTEST_INCLUDED_VALUE}, 2),
            # ⛔ AND THE CONTROL FOR THOSE THREE: an include make reads, whose
            # text is clean, must still PASS. Without this, "refuse whenever
            # anything is included" would satisfy all three above -- and that
            # is a tool nobody could add an include to.
            ("CONTROL   a clean INCLUDED file still exits 0",
             {"Makefile": "include inc.mk\nok: base.c $(FROM_INC)\n\t@true\n",
              "inc.mk": "FROM_INC := inc.c\n"}, 0),
            # r4: a continuation SPLITTING `call` from `value`.
            ("REFUSE    a CONTINUED $(call value,X) is still that call",
             {"Makefile": SELFTEST_VALUE_CONTINUED}, 2),
            # ⛔ And the control for the JOIN: make turns a backslash-newline
            # into a SPACE, so `$(va\ / lue X)` is the variable `va lue X` --
            # which --warn-undefined-variables CATCHES (exit 1, a real
            # finding), and which is NOT a `value` call. A join that glued the
            # fragments would answer 2 here, inventing a refusal for a file
            # make reads perfectly well. Measured:
            #   Makefile:1: warning: undefined variable 'va lue LATE'
            ("CONTROL   a word split by a continuation is NOT a value call",
             {"Makefile": SELFTEST_SPLIT_WORD}, 1),
            # r5: the define body, which phase 1 alone could not reach.
            ("REFUSE    $(call value,X) split inside a define BODY",
             {"Makefile": SELFTEST_VALUE_IN_DEFINE}, 2),
            # r6: the POLICY. Every form is a refusal now, including the three
            # the lexer lost to -- and the define arms above now refuse via
            # the policy rather than via the body merge, which stays as
            # defence in depth.
            ("REFUSE    `override define` is a define",
             {"Makefile": "override define B\n$(call\nvalue,L)\nendef\n"
                          "ok: base.c $(B)\n\t@true\nL := late.c\n"}, 2),
            ("REFUSE    `export define` is a define",
             {"Makefile": "export define B\nx\nendef\nok: base.c\n\t@true\n"}, 2),
            ("REFUSE    a NESTED define",
             {"Makefile": "define A\ndefine B\nx\nendef\nendef\n"
                          "ok: base.c\n\t@true\n"}, 2),
            ("REFUSE    a line ending in TWO backslashes (parity)",
             {"Makefile": "V := a\\\\\nok: base.c $(V)\n\t@true\n"}, 2),
            # ⛔ NEAR-MISS CONTROLS. Each predicate must fire on the construct
            # and NOT on a token that merely contains its letters -- the
            # identifier-boundary and unrelated-site failures that pass a
            # mutant while proving nothing (conv-ts, ts#365 r1).
            ("CONTROL   a variable called `defines` is not a define",
             {"Makefile": "defines := base.c\nok: $(defines)\n\t@true\n"}, 0),
            ("CONTROL   `endefx` is not an endef",
             {"Makefile": "endefx := base.c\nok: $(endefx)\n\t@true\n"}, 0),
            ("CONTROL   ONE trailing backslash is still a continuation",
             {"Makefile": "V := base.c\nok: $(V) \\\n     base.c\n\t@true\n"}, 0),
            ("CONTROL   the word `value` in a COMMENT is not a value call",
             {"Makefile": "# the return value of $(FOO) is discarded\n"
                          "EARLY := base.c\nok: $(EARLY)\n\t@true\n"}, 0),
            ("CONTROL   the word `value` in a RECIPE is not a value call",
             {"Makefile": "EARLY := base.c\nok: $(EARLY)\n"
                          "\t@echo 'no calibration bank for this value'\n"}, 0),
            # ⛔ And its control: an ordinary define must stay auditable...
            # EXCEPT it no longer is, by policy. Stated as the deliberate cost.
            ("POLICY    even a CLEAN define is refused now, by design",
             {"Makefile": SELFTEST_CLEAN_DEFINE}, 2),
            # ⛔ r6's recursion finding, and the fixture is built to isolate
            # the ATTRIBUTION fix from everything else. The evasion lives in
            # the INCLUDED file, so only the MAKEFILE_LIST binding decides
            # whether it is ever scanned: the policy reads the given Makefile
            # (clean here) and the warnings leg sees no undefined reference.
            # Measured at b7a0d74f6: exit 0, because the child's database
            # prints FIRST and the old code took it -- scanning child.mk and
            # calling the audit clean on a parent it never read.
            ("REFUSE    a recursive $(MAKE) must not redirect the file list",
             {"Makefile": "include inc.mk\nok: base.c $(FROM_INC)\n"
                          "\t@$(MAKE) -f child.mk all\n",
              "inc.mk": "FROM_INC := inc.c\nguard: base.c $(value LATE)\n"
                        "\t@true\nLATE := late.c\n",
              "child.mk": "all:\n\t@true\n"}, 2),
            # ⛔ r7 HIGH: the policy must hold over the WHOLE build graph. The
            # root here is policy-clean, and the INCLUDED file carries nested
            # defines plus a split value call -- the construct the lexer gets
            # wrong (it closes at the inner endef). Root-only policy handed
            # this straight to that lexer.
            # ⚠ THE SPLIT CALL SITS AFTER THE INNER endef, and that placement
            # is the whole fixture. My first attempt put it inside the inner
            # body, where the old merge still produced one line containing
            # `$(call value` -- so find_evasions caught it anyway and the arm
            # was INERT (old head also exited 2, proving nothing). Here the
            # premature close at the inner endef STRANDS the call: `$(call`
            # alone on one logical line, `value,LATE)` on the next, and
            # neither matches. Measured at 8b7ca3a08: exit 0, `(none)`.
            ("REFUSE    an INCLUDED file is inside the policy too",
             {"Makefile": "include inc.mk\nok: base.c $(FROM_INC)\n\t@true\n",
              "inc.mk": "FROM_INC := inc.c\ndefine OUTER\ndefine INNER\nx\n"
                        "endef\n$(call\nvalue,LATE)\nendef\nLATE := late.c\n"},
             2),
            # ⛔ r8: THE EXEMPTIONS ARE GONE, so these two now REFUSE -- the
            # opposite of what r7 pinned, and deliberately. r8 showed the
            # exemptions were a lexer (a tab-indented ASSIGNMENT is not a
            # recipe; a `#` inside $(subst #,...) is not a comment), and the
            # refusal message tells a future author to reword the comment
            # rather than hunt a defect that is not there.
            ("POLICY    a literal $(value X) in a COMMENT refuses, by design",
             {"Makefile": "EARLY := base.c\n"
                          "# $(value LATE) in a comment is never expanded\n"
                          "ok: base.c $(EARLY)\n\t@true\n"}, 2),
            ("POLICY    a literal $(value X) in a RECIPE refuses too",
             {"Makefile": "EARLY := base.c\nok: base.c $(EARLY)\n"
                          "\t@echo 'no bank for this $(value LATE)'\n"}, 2),
            # ⛔ AND THE SHAPES THAT MUST STILL PASS, which is what makes the
            # two above a narrowing rather than a blunt instrument. Both are
            # measured in the real Makefile: the word in prose (43 lines), and
            # a guard pinning an exact FIRMWARE log string, which is NOT
            # rewordable -- the word `value` there belongs to the firmware.
            ("CONTROL   the WORD value in prose still passes",
             {"Makefile": "EARLY := base.c\n"
                          "# the return value of $(EARLY) is discarded\n"
                          "ok: base.c $(EARLY)\n\t@true\n"}, 0),
            ("CONTROL   a recipe pinning a firmware string still passes",
             {"Makefile": "EARLY := base.c\nok: base.c $(EARLY)\n"
                          "\t@check $(EARLY) 'LOG_E(\"USECal %d: no "
                          "calibration bank for this value\",' 1\n"}, 0),
            # r8 note (a): $(eval) with the $$ form, which defers the call.
            # The literal `$(value` substring is present in `$$(value`, so the
            # call-position predicate sees it -- proved rather than assumed.
            ("REFUSE    $(eval X := $$(value L)) is still a value call",
             {"Makefile": "EARLY := base.c\n$(eval X := $$(value LATE))\n"
                          "ok: base.c $(EARLY)\n\t@true\nLATE := late.c\n"}, 2),
            # r8 note (a): a call whose NAME is computed reaches the builtin
            # with the word appearing nowhere. Both bracket spellings.
            ("REFUSE    $(call $(FN),X) -- computed function name",
             {"Makefile": "FN := value\nok: base.c $(call $(FN),LATE)\n"
                          "\t@true\nLATE := late.c\n"}, 2),
            ("REFUSE    $(call ${FN},X) -- the other bracket spelling",
             {"Makefile": "FN := value\nok: base.c $(call ${FN},LATE)\n"
                          "\t@true\nLATE := late.c\n"}, 2),
            ("CONTROL   an ordinary $(call fn,args) still passes",
             {"Makefile": "double = $(1)$(1)\nEARLY := base.c\n"
                          "ok: base.c $(EARLY)\n\t@echo $(call double,x)\n"}, 0),
            # r9: the remaining `call` spellings, all refused by ONE predicate
            # (first argument must be a literal identifier) rather than by
            # enumerating them -- enumerating is the treadmill.
            ("REFUSE    $(call $F,X) -- SINGLE-CHAR computed name",
             {"Makefile": "F := value\nok: base.c $(call $F,LATE)\n"
                          "\t@true\nLATE := late.c\n"}, 2),
            ("REFUSE    $(call call,value,X) -- laundered through `call`",
             {"Makefile": "ok: base.c $(call call,value,LATE)\n"
                          "\t@true\nLATE := late.c\n"}, 2),
            # ⚠ AND THE CONTROL THAT KEEPS THAT PREDICATE HONEST: `$$n` is an
            # escaped SHELL variable, not a make reference, and it appears 421
            # times in the audited Makefile. A bare `$X` predicate would have
            # refused the whole file.
            ("CONTROL   escaped shell $$n in a recipe still passes",
             {"Makefile": "EARLY := base.c\nok: base.c $(EARLY)\n"
                          "\t@n=3; echo \"$$n items\"\n"}, 0),
            # r9: recursion is REFUSED now, not tracked -- r7's file-list
            # binding fixed reading the CHILD's list and then left the child's
            # own sources unscanned.
            ("REFUSE    a recipe invoking $(MAKE)",
             {"Makefile": "EARLY := base.c\nok: base.c $(EARLY)\n"
                          "\t@$(MAKE) -f child.mk all\n",
              "child.mk": "all:\n\t@true\n"}, 2),
            ("REFUSE    a recipe invoking bare `make`",
             {"Makefile": "EARLY := base.c\nok: base.c $(EARLY)\n"
                          "\t@make -f child.mk all\n",
              "child.mk": "all:\n\t@true\n"}, 2),
            # ⚠ The word `make` in English, inside a recipe's echo text, is not
            # an invocation -- there are 3 such lines in the audited Makefile,
            # so a bare \\bmake\\b predicate would have refused it.
            ("CONTROL   the word 'make' in prose still passes",
             {"Makefile": "EARLY := base.c\nok: base.c $(EARLY)\n"
                          "\t@echo 'this hook exists to make a write fail'\n"},
             0),
            # r10: the four prefixes that walked past r9's recursion refusal.
            ("REFUSE    env-prefixed `CC=cc make`",
             {"Makefile": "EARLY := base.c\nok: base.c $(EARLY)\n"
                          "\tCC=cc make -f child.mk all\n",
              "child.mk": "all:\n\t@true\n"}, 2),
            ("REFUSE    `+env make`",
             {"Makefile": "EARLY := base.c\nok: base.c $(EARLY)\n"
                          "\t+env make -f child.mk all\n",
              "child.mk": "all:\n\t@true\n"}, 2),
            ("REFUSE    `+gmake`",
             {"Makefile": "EARLY := base.c\nok: base.c $(EARLY)\n"
                          "\t+gmake -f child.mk all\n",
              "child.mk": "all:\n\t@true\n"}, 2),
            # ⚠ $(MAKE_COMMAND) is why the `\\b` after MAKE had to go: `MAKE`
            # to `_` is not a word boundary, so r9's pattern never matched it.
            ("REFUSE    `+$(MAKE_COMMAND)`",
             {"Makefile": "MAKE_COMMAND := make\nEARLY := base.c\n"
                          "ok: base.c $(EARLY)\n"
                          "\t+$(MAKE_COMMAND) -f child.mk all\n",
              "child.mk": "all:\n\t@true\n"}, 2),
            # ⚠ AND THE CONTROLS FOR THAT PREDICATE: a command whose name
            # merely CONTAINS make, and an ordinary $(VAR) in command
            # position, must both still pass -- `$(CC) -o x x.c` is the shape
            # every real recipe uses.
            ("CONTROL   `cmake` is not `make`",
             {"Makefile": "EARLY := base.c\nok: base.c $(EARLY)\n"
                          "\t@cmake --version\n"}, 0),
            ("CONTROL   an ordinary $(VAR) command still passes",
             {"Makefile": "CC := cc\nEARLY := base.c\nok: base.c $(EARLY)\n"
                          "\t@$(CC) --version\n"}, 0),
            # r10: $(file ...) loads makefile text that is in NO file list.
            ("REFUSE    $(eval $(file <rules.mk)) -- text outside any list",
             {"Makefile": "EARLY := base.c\n$(eval $(file <rules.mk))\n"
                          "ok: base.c $(EARLY)\n\t@true\n",
              "rules.mk": "guard: base.c $(value LATE)\n\t@true\n"
                          "LATE := late.c\n"}, 2),
            # r10: an ORDINARY late-definition case the warning cannot see,
            # because ifdef tests definedness rather than referencing it.
            ("REFUSE    `ifdef LATE` before LATE is defined",
             {"Makefile": "EARLY := base.c\nifdef LATE\nok: base.c $(LATE)\n"
                          "else\nok: base.c $(EARLY)\n\t@true\nendif\n"
                          "LATE := late.c\n"}, 2),
            ("REFUSE    any conditional at all, by policy",
             {"Makefile": "EARLY := base.c\nifeq ($(EARLY),base.c)\n"
                          "ok: base.c $(EARLY)\n\t@true\nendif\n"}, 2),
        ]
        for name, files, want_rc in refusal_arms:
            got, out = _fixture_exit(files)
            good = (got == want_rc)
            ok = ok and good
            print(f"  {'PASS' if good else 'FAIL'}  {name}: "
                  f"exit={got}, wanted={want_rc}")
            if not good:
                for ln in out.strip().splitlines():
                    print(f"        | {ln}")

        # ⛔ A WARNING THE TOOL CANNOT PARSE IS NOT ONE IT MAY IGNORE, and
        # these pin the DECISION rather than the loop's existence. make does
        # not always emit a `file:line:` prefix, and forcing it to do so in a
        # fixture is not reproducible -- so the classifier is pure and gets
        # canned stderr. A mutant that drops the `unlocated` branch passes any
        # structural pin and fails here. ALL OF THESE FAIL ON 317acfc94, where
        # a prefix-less warning was silently dropped and the audit still
        # printed `(none)` and exited 0.
        _canned = (
            "Makefile:7: warning: undefined variable 'LATE'\n"
            "make: warning: jobserver unavailable: using -j1\n"
            "warning: Clock skew detected\n"
            "Makefile:9: *** missing separator.  Stop.\n")
        _loc, _unloc = classify_warnings(_canned)
        arms2 = [
            ("located warnings still parse", [w["var"] for w in _loc],
             ["LATE"]),
            ("a prefix-less warning is KEPT, not dropped", len(_unloc), 2),
            ("and it is not mistaken for a located one", len(_loc), 1),
            ("a non-warning error line is neither",
             any("missing separator" in u for u in _unloc), False),
            ("empty stderr classifies as nothing at all",
             classify_warnings(""), ([], [])),
            ("and so does None, rather than raising",
             classify_warnings(None), ([], [])),
        ]
        for name, actual, want in arms2:
            good = (actual == want)
            ok = ok and good
            print(f"  {'PASS' if good else 'FAIL'}  CLASSIFY  {name}: "
                  f"got {actual!r}, wanted {want!r}")

        # ⛔ THE REFUSAL LIST IS ASSERTED, NOT DESCRIBED. These arms are what
        # makes REFUSED_EXAMPLES the single copy of the claim: a construct
        # named there and not refused by the named rule fails here, so the
        # docstring (which now points at the tuple) cannot run ahead of the
        # code again. ALL THE /usr/bin/make, `command make` AND $(origin ...)
        # ROWS FAIL ON 55ac3485d, where the docstring claimed all three and
        # the policy refused none.
        #
        # The rule NAME is asserted, not merely "something refused it":
        # `$(origin X)` in an `ifeq` is refused by the conditional rule
        # regardless, so a looser arm would have passed before the fix.
        for sample, want_rule in REFUSED_EXAMPLES:
            hits = [kind for kind, rx, _why in POLICY if rx.search(sample)]
            good = want_rule in hits
            ok = ok and good
            print(f"  {'PASS' if good else 'FAIL'}  REFUSE-LIST "
                  f"{sample.strip()!r}: wanted rule {want_rule!r}, "
                  f"hits={hits}")

        # And the controls, without which a rule that refused every line would
        # satisfy every arm above.
        for sample, why in POLICY_NEAR_MISSES:
            hits = [kind for kind, rx, _why in POLICY if rx.search(sample)]
            good = not hits
            ok = ok and good
            print(f"  {'PASS' if good else 'FAIL'}  NEAR-MISS "
                  f"{sample.strip()!r}: must match nothing ({why}), "
                  f"hits={hits}")

        # A known false positive, pinned in the direction it already fails, so
        # a later narrowing of the token cannot silently admit a real submake.
        for sample, want_rule, why in POLICY_KNOWN_FALSE_POSITIVES:
            hits = [kind for kind, rx, _why in POLICY if rx.search(sample)]
            good = want_rule in hits
            ok = ok and good
            print(f"  {'PASS' if good else 'FAIL'}  KNOWN-FP "
                  f"{sample.strip()!r}: still refused by {want_rule!r} "
                  f"({why[:60]}...), hits={hits}")

        # ⛔ AND EVERY RULE MUST BE EXERCISED BY THE LIST. Without this, a rule
        # could be deleted from POLICY and no arm above would fail -- the
        # refusal rows name rules, so a missing rule makes its own rows fail,
        # but a rule that NO row names could vanish unnoticed. This is the arm
        # that makes the table complete over POLICY's RULES rather than a
        # sample of them. ⛔ It says nothing about Make's constructs: r13 found
        # seven the predicates do not recognise, so "every rule is exercised"
        # must not be read as "every construct is refused". See the RESIDUAL
        # CLASSES block above POLICY_NEAR_MISSES.
        _named = {want for _s, want in REFUSED_EXAMPLES}
        _defined = {kind for kind, _rx, _why in POLICY}
        _unexercised = _defined - _named
        good = not _unexercised
        ok = ok and good
        print(f"  {'PASS' if good else 'FAIL'}  REFUSE-LIST covers every "
              f"POLICY rule: unexercised={sorted(_unexercised)}")
        _phantom = _named - _defined
        good = not _phantom
        ok = ok and good
        print(f"  {'PASS' if good else 'FAIL'}  REFUSE-LIST names no rule "
              f"that does not exist: phantom={sorted(_phantom)}")

        # A finding without file:line is not actionable.
        located = all(w["file"] and w["line"].isdigit() for w in warnings)
        print(f"  {'PASS' if located else 'FAIL'}  every warning carries "
              f"file:line")
        ok = ok and located

        if not warnings:
            print("  FAIL  the fixture produced NO warnings at all -- either "
                  "this make lacks --warn-undefined-variables, or the fixture "
                  "stopped modelling the defect")
            print(stderr.strip()[:300])
            ok = False

        print()
        print("SELF-TEST " + ("PASS" if ok else "FAIL"))
        return 0 if ok else 2


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        sys.exit(self_test())
    path = os.path.abspath(sys.argv[1] if len(sys.argv) > 1
                           else DEFAULT_MAKEFILE)
    if not os.path.isfile(path):
        print(f"ERROR: no such Makefile: {path}")
        print("       Run from the repository root, or pass the path.")
        sys.exit(2)
    sys.exit(report(path))
