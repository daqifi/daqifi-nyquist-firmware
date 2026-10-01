#!/usr/bin/env bash
MTK="$1"; V='merge'
r_for(){ INPUT=$(printf '%s' "$1" | jq -Rs '{tool_input:{command:.}}'); export INPUT; RESTS=''; . "$MTK" >/dev/null 2>&1; printf '%s' "$RESTS"; }
printf '%-12s %-24s %s\n' 'REFUSED CHAR' 'RESTS' 'ARM STATUS'
for spec in 'dquote:"' "squote:'" 'backslash:\' 'dollar:$' 'backtick:`' 'obrace:{' 'cbrace:}'; do
  n=${spec%%:*}; c=${spec#*:}
  cmd="gh pr $V 99 --body X${c}Y"
  r=$(r_for "$cmd")
  if [ "${r#*"$c"}" != "$r" ]; then s='REACHABLE'; else s='*** UNREACHABLE'; fi
  printf '%-12s %-24s %s\n' "$n ($c)" "<$r>" "$s"
done
