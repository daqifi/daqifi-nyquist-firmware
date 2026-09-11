# Strip C comments from stdin, tracking block-comment state ACROSS lines.
#
# Why this exists as a file rather than a sed expression in the Makefile:
# the rule it replaces was line-anchored -- it deleted any line whose first
# non-space characters were "/*", "*" or "//". That erases a real executable
# statement written as a leading dereference:
#
#     *(&pSDCardSettings->mode) = SD_CARD_MANAGER_MODE_NONE;
#
# and it erases the code half of  /* note */ stmt();  . Both were reproduced
# against the real pin recipe: the tripwire returned 0 (no drift) for a
# firmware edit it exists to catch, while an ordinary token change returned 2.
# A regression gate that fails toward SILENCE is worse than none, so the rule
# is now state-based: text is removed only when it is genuinely inside /* */
# or after //, never because of how a line happens to begin.
#
# Known limitation, stated rather than hidden: comment markers inside STRING
# LITERALS are treated as comments. The pinned slice contains no such literal
# (checked), and the failure direction is a hash that ignores part of a string
# rather than one that ignores a statement -- but a future edit introducing
# "http://" or similar into the slice would need this revisited.
{
    line = $0; out = ""; i = 1; n = length(line)
    while (i <= n) {
        two = substr(line, i, 2)
        if (inComment) {
            if (two == "*/") { inComment = 0; i += 2 } else { i++ }
        } else if (two == "/*") {
            inComment = 1; i += 2
        } else if (two == "//") {
            break
        } else {
            out = out substr(line, i, 1); i++
        }
    }
    print out
}
