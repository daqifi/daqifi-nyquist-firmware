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

    if ($0 ~ /taskENTER_CRITICAL/) seq = seq "E"
    if ($0 ~ /\.CalM/)             seq = seq "M"
    if ($0 ~ /\.CalB/)             seq = seq "B"
    if ($0 ~ /taskEXIT_CRITICAL/)  seq = seq "X"

    depth -= gsub(/\}/, "}")
    if (brace_seen && depth <= 0) inside = 0
}

END { print seq }
