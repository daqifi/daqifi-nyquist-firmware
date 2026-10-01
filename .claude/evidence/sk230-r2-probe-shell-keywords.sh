#!/usr/bin/env bash
# Verify the blind leg's finding 0 on the RESTORED cut set.
# ⛔ Each probe runs in a SUBSHELL: merge-target-keys.sh calls `exit`, which when
# sourced would terminate this harness and make the remaining rows silently vanish.
MTK="$1"; V='merge'
probe(){
  local cmd="$1" label="$2" out
  out=$( INPUT=$(printf '%s' "$cmd" | jq -Rs '{tool_input:{command:.}}'); export INPUT
         CMD_M=''; . "$MTK" >/dev/null 2>&1; printf 'CMD_M=%s' "$CMD_M" )
  local cm=${out#CMD_M=}
  if [ -z "$cm" ]; then printf '%-34s %-44s %s\n' "$label" "$cmd" '*** EMPTY CMD_M -> gate exits 0 -> BYPASS'
  else printf '%-34s %-44s %s\n' "$label" "$cmd" "detected <$cm>"; fi
}
printf '%-34s %-44s %s\n' SHAPE COMMAND RESULT
probe "gh pr $V 99"                           "control (plain)"
probe "if true; then gh pr $V 99; fi"         "if/then (blind leg case)"
probe "for i in 1; do gh pr $V 99; done"      "for/do"
probe "while true; do gh pr $V 99; done"      "while/do"
probe "if false; then :; else gh pr $V 99; fi" "if/else"
probe "{ gh pr $V 99; }"                      "brace group (r1, restored)"
probe "\`gh pr $V 99\`"                       "backtick (r1, restored)"
