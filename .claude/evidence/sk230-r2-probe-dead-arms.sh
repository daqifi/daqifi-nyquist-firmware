#!/usr/bin/env bash
# PROBE: test conv-fw's DISCLOSED RESIDUAL on sk#230 @ 09b532acf.
#
# Their claim (merge-target-keys.sh:251-254):
#   "$RESTS is cut from $CMD_M ... pre-merge-gate.sh's refusal tests seven
#    characters including backtick, { and }. Because this sed strips those three
#    first, $RESTS can never contain them, so three of the seven arms are
#    UNREACHABLE BY CONSTRUCTION."
#
# A disclosed residual is an UNTESTED FINDING. This tests it rather than accepting it.
#
# SAFETY: this only SOURCES the keying script to read the $RESTS variable it computes.
# It never invokes the gate's merge path and never executes any candidate command.
# Every candidate below is a STRING fed to a text parser.

MTK="${1:?usage: probe_dead_arms.sh /path/to/merge-target-keys.sh}"
V='merge'   # assembled so this file's own prose does not carry the bare verb+operand

# the seven characters pre-merge-gate.sh:194 refuses, per the comment at :252
SEVEN='` { } $ ( ) "'

probe() {
	local label="$1" cmd="$2"
	local out
	out=$(
		INPUT=$(printf '%s' "$cmd" | jq -Rs '{tool_input:{command:.}}')
		export INPUT
		# shellcheck disable=SC1090
		RESTS=''; CMD_M=''
		. "$MTK" >/dev/null 2>&1
		printf 'CMD_M=<%s>\nRESTS=<%s>\n' "$CMD_M" "$RESTS"
	)
	local rests
	rests=$(printf '%s' "$out" | sed -n 's/^RESTS=<\(.*\)>$/\1/p')
	local hits=''
	case "$rests" in *'`'*) hits="$hits backtick";; esac
	case "$rests" in *'{'*) hits="$hits {";; esac
	case "$rests" in *'}'*) hits="$hits }";; esac
	printf '%-42s RESTS=%-34s dead-arm chars present:%s\n' \
		"$label" "<$rests>" "${hits:- NONE}"
}

echo "=== Can \$RESTS EVER contain a backtick, { or } ?  (their claim: never)"
probe "plain control"            "gh pr $V 99"
probe "backtick-wrapped verb"    "\`gh pr $V 99\`"
probe "brace-grouped verb"       "{ gh pr $V 99; }"
probe "subshell-grouped verb"    "( gh pr $V 99 )"
probe "command-substituted verb" "\$(gh pr $V 99)"
probe "QUOTED backtick in body"  "gh pr $V 99 --body \"a \\\` tick\""
probe "QUOTED brace in body"     "gh pr $V 99 --body \"a { brace }\""
probe "backtick AFTER the verb"  "gh pr $V 99 \`id\`"
probe "brace AFTER the verb"     "gh pr $V 99 {a,b}"

echo
echo "=== the seven characters the refusal tests: $SEVEN"
echo "=== (a char that cannot appear in RESTS has an unreachable arm)"
