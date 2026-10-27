# #904: emit the ORDERED event sequence inside one function body, one letter
# per event, so a guard can assert the SHAPE of the critical section instead of
# COUNTING its parts.
#
#     E = taskENTER_CRITICAL      M = a .CalM access
#     X = taskEXIT_CRITICAL       B = a .CalB access
#
# ⚠ WHY A SEQUENCE AND NOT A COUNT. The guard this replaces counted
# occurrences of `taskENTER_CRITICAL` with `grep -c`, and a commented-out call
# still contains the string. Verified on this tree: commenting out BOTH calls
# in daqifi_settings_LoadADCCalSettings left the stores completely unguarded
# and the build GREEN. A presence-only source guard guards nothing.
#
# A sequence closes four distinct failures that a count cannot. Measured, each
# by mutating the real firmware and re-running the whole strip+extract pipe:
#
#     correct tree                     -> EMBX   (the only accepted shape)
#     both calls commented out (//)    -> MB
#     ENTER commented out (/* */)      -> MBX
#     EXIT deleted                     -> EMB
#     ENTER moved AFTER the CalM store -> MEBX
#
# ⭐ The last one is the reason this is a sequence rather than a
# comment-stripped count: MEBX has the SAME TOKEN COUNTS as EMBX. Every
# counting guard, stripped or not, passes it while the first store sits
# outside the critical section. Only order sees it.
#
# RUN OVER COMMENT-STRIPPED SOURCE: `$(CC) -fpreprocessed -E -P -x c -`.
# `-fpreprocessed` is what makes this usable on a Harmony .c at all -- it does
# not resolve the #includes (nothing here can), it does not expand macros, so
# `taskENTER_CRITICAL()` is seen as written; it only removes comments.
# Line numbers do NOT survive that transform, which is exactly why this keys on
# sequence and never on line arithmetic.
#
# Usage: awk -v fn=<function> -f cal_order_904.awk <stripped source>

BEGIN { depth = 0; inside = 0; seq = "" }

# The function's DEFINITION, not its prototype: a declaration ends in `;`, and
# both SCPI setters here are forward-declared, so anchoring on the first
# textual match would scan the prototype and emit nothing. (The same trap that
# makes the shared CNT_IN_FN return 0 for a forward-declared function.)
!inside && $0 ~ ("(^|[ \t*])" fn "[ \t]*\\(") && $0 !~ /;[ \t]*$/ {
    inside = 1
    brace_seen = 0
}

inside {
    n = gsub(/\{/, "{")
    depth += n
    if (n > 0) brace_seen = 1

    # ⚠ BY COLUMN, NOT BY RULE ORDER. Four separate `if`s tested in a fixed
    # sequence emit in THE CHECKER'S order, not the SOURCE's, so every event
    # on one line came out E,M,B,X whatever the line actually said:
    #
    #     cell.CalM = v; taskENTER_CRITICAL(); cell.CalB = v; taskEXIT_CRITICAL();
    #
    # is M,E,B,X in source order -- the CalM store sits OUTSIDE the section --
    # and the old form scored it EMBX and PASSED it. An order checker that
    # imposed its own order: the defect it exists to catch, one level down.
    #
    # match() returns the LEFTMOST match, so consuming the line and repeating
    # walks the events in column order. Alternatives cannot overlap, so no
    # longest-match tie-breaking is involved.
    #
    # BOTH `.CalM` and `->CalM` count as an access. The four guarded writers
    # all use the `.` form today and the `->` form appears only in the #1054
    # readers, so this changes no verdict here -- it is so that a writer
    # refactored to a channel pointer (`chan->CalM = v`) stays VISIBLE. The
    # narrow form failed CLOSED on that refactor (no token, so the sequence
    # mismatched and the build broke), which is the safe direction but a
    # spurious failure rather than a real check.
    rest = $0
    while (match(rest, /taskENTER_CRITICAL|taskEXIT_CRITICAL|(\.|->)CalM|(\.|->)CalB/)) {
        tok = substr(rest, RSTART, RLENGTH)
        if      (tok == "taskENTER_CRITICAL") ch = "E"
        else if (tok == "taskEXIT_CRITICAL")  ch = "X"
        else if (tok ~ /CalM$/)               ch = "M"
        else                                  ch = "B"
        # ADJACENT REPEATS COLLAPSE. `a.CalM = b.CalM;` names the field TWICE
        # on one line, and both accesses need the section -- but emitting MM
        # would make the expected string depend on how many times a statement
        # happens to name the field, so `x = y.CalM` vs a temp would change it
        # for no change in safety. Only TRANSITIONS carry the property: a
        # second access that fell OUTSIDE the section would land on the other
        # side of an E or X and still alter the sequence (E M X M, not E M X).
        if (ch != last) { seq = seq ch; last = ch }
        rest = substr(rest, RSTART + RLENGTH)
    }

    depth -= gsub(/\}/, "}")
    if (brace_seen && depth <= 0) inside = 0
}

END { print seq }
