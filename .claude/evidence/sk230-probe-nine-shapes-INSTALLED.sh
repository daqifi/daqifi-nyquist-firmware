#!/usr/bin/env bash
# conv-fw's five additional shapes, verified against a given parser. Read-only:
# sources the parser in a SUBSHELL and reads CMD_M. No candidate command executed.
MTK="$1"; V='merge'
probe(){ local out cm
  out=$( INPUT=$(printf '%s' "$1" | jq -Rs '{tool_input:{command:.}}'); export INPUT
         CMD_M=''; . "$MTK" >/dev/null 2>&1; printf 'CMD_M=%s' "$CMD_M" )
  cm=${out#CMD_M=}
  [ -z "$cm" ] && printf '%-44s %s\n' "$1" '*** BYPASS (empty CMD_M -> exit 0)' \
               || printf '%-44s %s\n' "$1" "detected <$cm>"; }
probe "gh pr $V 99"
probe "if gh pr $V 99; then :; fi"
probe "while gh pr $V 99; do :; done"
probe "until gh pr $V 99; do :; done"
probe "! gh pr $V 99"
probe "if false; then :; elif gh pr $V 99; then :; fi"
