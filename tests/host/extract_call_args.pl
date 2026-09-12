#!/usr/bin/perl
# ==========================================================================
# extract_call_args.pl <function_name>
#
# Reads C source text from stdin -- already comment-stripped and flattened
# to one line by the caller -- and prints the FIRST call `<function_name>
# ( ... )`'s top-level, comma-separated arguments, one per line, each
# trimmed of leading/trailing whitespace with all remaining internal
# whitespace collapsed out (so "&  gUsbScpiStorage" and "&gUsbScpiStorage"
# compare equal, and so does a mutation that splits an expression across
# what were physical lines before flattening).
#
# Written for issue #999 (PR #1016, round 5) to replace two guards an
# adversarial audit proved bypassable: tests/host/Makefile's
# CreateSCPIContext() caller checks, and
# check_999_createscpicontext_wiring.sh's SCPI_Init() wiring check (c).
# Both used to do `grep -q 'TOKEN'` against the WHOLE call's text, which
# passes as soon as TOKEN appears ANYWHERE in the call -- including inside
# the untaken branch of a ternary:
#
#   CreateSCPIContext(&scpi_interface, &gRunTimeUsbSttings,
#                      cond ? &gSharedScpiStorage : &gUsbScpiStorage);
#
# contains the literal substring "&gUsbScpiStorage" while a true `cond`
# hands the context SHARED storage at runtime -- reintroducing #999 exactly,
# with the guard reporting green. Extracting each argument's own exact text
# by real paren/bracket-depth counting (not regex-guessing where the call
# ends) lets a caller require EQUALITY to an expected token instead of mere
# containment: the ternary's own argument text is
# "cond?&gSharedScpiStorage:&gUsbScpiStorage" as a whole, which can never
# equal either of its own branches exactly. Same class as the earlier
# "dead keep-alive" bypass this PR was already reworked once to close (a
# literal reference kept alive elsewhere in the file satisfied a whole-file
# grep while the real call pointed somewhere else) -- this generalizes that
# fix from "scope to the call site" to "scope to the ARGUMENT, compared
# whole, not as a substring".
#
# Usage:
#   printf '%s' "$flattened_comment_stripped_text" \
#     | extract_call_args.pl SomeFunction
#
# Prints nothing and exits 1 if:
#   - the function name is not found as a call (word-boundary anchored, so
#     a name that is a SUFFIX of another identifier, e.g. a hypothetical
#     "ReCreateSCPIContext", cannot be mistaken for the real call), or
#   - the call's parens (or a nested bracket/paren inside an argument)
#     never balance back to zero -- an unbalanced input is "could not
#     verify", which callers MUST treat as a failure, never as "clean".
#
# This script does not itself judge whether an argument is correct -- it
# only tells a caller what the argument texts ARE. It also does not strip
# comments; callers must flatten + strip comments first (both existing
# call sites already do, using the same //-then-block-comment technique --
# see the Makefile and check_999_createscpicontext_wiring.sh), because a
# comment containing a comma would otherwise be miscounted as an argument
# boundary.
#
# It is likewise NOT string- or char-literal aware: parens, brackets and
# commas inside "..." or '...' are counted as if they were code. Neither
# real call site passes a literal today, and the failure direction is
# fail-CLOSED for both consumers -- a literal paren truncates the argument
# list, so the whole argument they require by EQUALITY goes missing and the
# guard turns RED. So adding a string argument to either call breaks the
# build loudly rather than blinding the guard, which is the safe direction
# but is still a limitation and not a property to rely on. Making the scan
# literal-aware is the fix if a literal argument ever becomes legitimate.
# test_extract_call_args.sh pins this behaviour in two cases marked
# LIMITATION so it cannot change silently.
# ==========================================================================
use strict;
use warnings;

my $fn = shift @ARGV;
if (!defined($fn) || $fn eq '') {
    print STDERR "usage: $0 <function_name> < flattened_comment_stripped_text\n";
    exit 1;
}

local $/;
my $text = <STDIN>;
$text = '' unless defined $text;

# Word-boundary anchor: a preceding character that IS an identifier
# character disqualifies the match (so "XCreateSCPIContext(" doesn't count
# as a call to "CreateSCPIContext"). Whitespace is allowed between the name
# and its opening paren.
unless ($text =~ /(?<![A-Za-z0-9_])\Q$fn\E\s*\(/) {
    exit 1;
}
my $paren_start = $+[0] - 1;   # index of the '(' the match just consumed

my $depth = 0;
my $arg_start = $paren_start + 1;
my @args;
my $len = length($text);
my $closed = 0;
for (my $i = $paren_start; $i < $len; $i++) {
    my $c = substr($text, $i, 1);
    if ($c eq '(' || $c eq '[') {
        $depth++;
    } elsif ($c eq ')' || $c eq ']') {
        $depth--;
        # The scan starts AT the call's own '(' (so depth is 1 after the
        # first character) and stops the moment depth returns to 0, which
        # means depth can never go negative here -- a guard for that case
        # would be unreachable. Trailing text after the call, including a
        # closer belonging to an enclosing expression, is simply never read.
        if ($depth == 0) {
            push @args, substr($text, $arg_start, $i - $arg_start);
            $closed = 1;
            last;
        }
    } elsif ($c eq ',' && $depth == 1) {
        push @args, substr($text, $arg_start, $i - $arg_start);
        $arg_start = $i + 1;
    }
}
exit 1 unless $closed;

for my $a (@args) {
    $a =~ s/^\s+|\s+$//g;
    $a =~ s/\s+//g;
    print "$a\n";
}
