# #908: pin the COMPOSITION of the generation bracket with #1048's critical
# section at a calibration setter. Emits the ordered event sequence for one
# function; the caller asserts it EQUALS the one legal arrangement.
#
# ⚠ WHY THIS EXISTS, given that two guard sets already run on these setters.
# Neither of them can see the composition, and the gap was authored by the
# MERGE of #1048 into this branch rather than by anyone's edit -- so there is
# no line in either diff to object to:
#
#   cal_order_904.awk (from #1048) tokenises only taskENTER_CRITICAL /
#   taskEXIT_CRITICAL / .CalM / .CalB. daqifi_settings_BeginCalWrite emits NO
#   token there, so `EVX` scores identically whether the generation bracket is
#   outside the critical section, inside it, or absent altogether.
#
#   This branch's own 908 guards check that the setters CONTAIN the bracket
#   (begin=1 end=1), never where it sits relative to the section.
#
# So each set is satisfied by an arrangement the other was meant to exclude.
#
# Letters chosen so none collides:
#   G = daqifi_settings_BeginCalWrite   (generation opens)
#   E = taskENTER_CRITICAL
#   V = the 64-bit value store (.CalM or .CalB)
#   X = taskEXIT_CRITICAL
#   H = daqifi_settings_EndCalWrite     (generation closes)
#
# The ONLY accepted sequence is G E V X H, and both halves are load-bearing:
# the generation must span the whole logical write (or a reader can sample an
# EVEN generation mid-write and believe it), and the critical section must be
# the innermost thing around the store (or the 64-bit pair can tear -- #904).
#
# ⛔ THE CALLER MUST TEST FOR EQUALITY WITH "GEVXH", NEVER FOR THE ABSENCE OF A
# BAD PATTERN. A function this script cannot find yields the EMPTY string,
# which is "no bad pattern" and would pass an absence check vacuously. Measured
# while writing this: passing the wrong spelling of the function name
# (SCPI_ADCChanCalmSetClaimed, which is not what the definition is called)
# returns "" rather than an error.
#
# Run over comment-stripped source, which also normalises line endings:
#   $(CC) -fpreprocessed -E -P -x c - < src > stripped
# This works on a Harmony .c because it neither resolves #includes nor expands
# macros. Usage:
#   awk -v fn=<function> -f cal_compose_908.awk <stripped source>

BEGIN { depth = 0; inside = 0; seq = ""; last = "" }

# The DEFINITION, not the prototype: a declaration ends in `;`, and both
# setters are forward-declared.
#
# ⚠ `[[:space:]]`, NOT `[ \t]`, because `[ \t]` does not include `\r`. Where
# that bites, measured on SCPIADC.c rather than reasoned about:
#
#   intended pipeline (preprocessed input)   [ \t] -> GEVXH   [[:space:]] -> GEVXH
#   raw CRLF source                          [ \t] -> VGEVXH  [[:space:]] -> GEVXH
#
# So the intended pipeline is IMMUNE: `cc -fpreprocessed -E -P` normalises line
# endings as well as stripping comments (2075 CR-bearing lines in, 0 out on
# this file), so no `\r` ever reaches this script when it is driven as the
# recipe drives it. The hazard is only for someone running it on raw source.
#
# ⛔ And note the failure is NOT the empty string, which is what I assumed
# before measuring: `[ \t]` on raw CRLF returns `VGEVXH`, because the
# declaration line is mistaken for the definition and the scan starts early,
# picking up a store ahead of the real body. A wrong-but-non-empty sequence
# FAILS the equality check, which is the safe direction. `[[:space:]]` is still
# correct, and the equality check is what makes either spelling survivable --
# but the two defend against different things and neither substitutes for the
# other.
!inside && $0 ~ ("(^|[ \t*])" fn "[ \t]*\\(") && $0 !~ /;[[:space:]]*$/ {
    inside = 1
    brace_seen = 0
}

inside {
    n = gsub(/\{/, "{")
    depth += n
    if (n > 0) brace_seen = 1

    # BY COLUMN, so several events on one line come out in SOURCE order. This
    # is the mistake cal_order_904.awk made and had corrected in review: a
    # per-line test reports the order the TESTS are written in, not the order
    # the code executes in, so a one-line setter scored as compliant whatever
    # it actually did.
    rest = $0
    while (match(rest, /daqifi_settings_BeginCalWrite|daqifi_settings_EndCalWrite|taskENTER_CRITICAL|taskEXIT_CRITICAL|(\.|->)Cal[MB]/)) {
        tok = substr(rest, RSTART, RLENGTH)
        if      (tok ~ /BeginCalWrite$/)      ch = "G"
        else if (tok ~ /EndCalWrite$/)        ch = "H"
        else if (tok == "taskENTER_CRITICAL") ch = "E"
        else if (tok == "taskEXIT_CRITICAL")  ch = "X"
        else                                  ch = "V"
        # Adjacent repeats collapse: `a.CalM = b.CalM` names the field twice
        # and multiplicity is not the property under test -- only transitions
        # are. Non-adjacent repeats are NOT collapsed, so a second critical
        # section still shows as a second E.
        if (ch != last) { seq = seq ch; last = ch }
        rest = substr(rest, RSTART + RLENGTH)
    }

    depth -= gsub(/\}/, "}")
    if (brace_seen && depth <= 0) inside = 0
}

END { print seq }
