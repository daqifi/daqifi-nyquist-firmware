#!/usr/bin/env bash
# Classify the 33 NEWLY BLOCKED lines per the pre-registered criterion.
# ⛔ SAFETY: `bash -n` PARSES ONLY. No corpus line is ever executed and no git push
# is ever invoked. Every line is treated as text.
SRC="${1:?usage: classify33.sh /path/to/newblocked.txt}"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

n=0; art_parse=0; art_quote=0; genuine=0; undec=0
: > "$TMP/genuine.txt"

# grep -c '' counts the unterminated final line; read -r with the || [ -n ] idiom
# does the same for the loop, so row 33 is not silently dropped.
while IFS= read -r line || [ -n "$line" ]; do
	n=$((n+1))
	printf '%s\n' "$line" > "$TMP/l.sh"

	# TEST 1 -- parse only, never run
	if ! bash -n "$TMP/l.sh" 2>"$TMP/perr"; then
		art_parse=$((art_parse+1))
		printf '%2d  HARVEST_ARTIFACT  parse-fail: %s\n' "$n" "$(head -1 "$TMP/perr" | sed 's/.*line [0-9]*: //' | cut -c1-60)"
		continue
	fi

	# TEST 2 -- does a `git push` survive quote-stripping?
	stripped=$(printf '%s' "$line" | sed -e "s/'[^']*'/''/g" -e 's/"[^"]*"/""/g')
	if ! printf '%s' "$stripped" | grep -qE '(^|[^[:alnum:]_-])git[[:space:]]+push'; then
		art_quote=$((art_quote+1))
		printf '%2d  HARVEST_ARTIFACT  push only inside quotes / not a push\n' "$n"
		continue
	fi

	# TEST 3 -- first-push shape?
	if printf '%s' "$stripped" | grep -qE '(-u|--set-upstream)([[:space:]]|$)|HEAD:(refs/heads/)?[A-Za-z0-9._/-]+'; then
		fp="FIRST-PUSH-SHAPE (gate is meant to catch)"
	else
		fp="no first-push marker (gate not meant to gate)"
	fi
	genuine=$((genuine+1))
	printf '%2d  GENUINE           %s\n' "$n" "$fp"
	printf '=== row %d\n%s\n\n' "$n" "$line" >> "$TMP/genuine.txt"
done < "$SRC"

echo
printf 'rows read                 %s   (expect 33)\n' "$n"
printf 'HARVEST_ARTIFACT parse    %s\n' "$art_parse"
printf 'HARVEST_ARTIFACT quoted   %s\n' "$art_quote"
printf 'GENUINE                   %s\n' "$genuine"
printf 'UNDECIDABLE               %s\n' "$undec"
cp "$TMP/genuine.txt" ./genuine-rows-verbatim.txt 2>/dev/null
echo
echo "verbatim GENUINE rows written to ./genuine-rows-verbatim.txt"
