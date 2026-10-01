#!/usr/bin/env bash
# Review-to-next-push interval, per the pre-registration at
# PREREG-review-to-push-interval.md. Read-only. No disposition on any row.
#
# ATTESTATION TIMESTAMP comes from the POINTER COMMENT that names the head, never
# from a Qodo body's created_at — there is one standing body edited in place, so its
# created_at is frozen at the first review ever and would inflate every interval to
# the age of the PR.
#
# SHA candidates are validated against git (cat-file -t == commit): a bare hex class
# picks up all-digit GitHub comment IDs, measured on fw#1110.
#
# Every fetch is verified and retried; an empty read is UNDETERMINED, never zero.
set -u
REPO="${REPO:-daqifi/daqifi-nyquist-firmware}"
TREE="${TREE:-/mnt/c/daqifi/wt/nq-b}"
RETRIES=4

fetch() {  # $1=path $2=jq -> payload or empty after retries
	local o i
	for ((i=0;i<RETRIES;i++)); do
		o=$(gh api "$1" --paginate --jq "$2" 2>/dev/null)
		[ -n "$o" ] && { printf '%s' "$o"; return 0; }
		sleep 4
	done
	return 1
}

printf 'PR\tLIVE\tPTRS\tINTERVALS_created\tINTERVALS_updated\tSTALE|diverge\n'
for pr in $(gh pr list --repo "$REPO" --state open --limit 200 --json number --jq '.[].number' 2>/dev/null); do
	live=$(gh pr view "$pr" --repo "$REPO" --json headRefOid --jq .headRefOid 2>/dev/null)
	# ⛔ BOTH TIMESTAMPS, because neither is unambiguously right and they bias in
	# OPPOSITE directions:
	#   created_at  frozen at first post if the body is edited in place -> INFLATES
	#               the interval (against my bias)
	#   updated_at  advances on any edit, cosmetic ones included       -> SHRINKS
	#               the interval (TOWARD my bias)
	# The coordinator's bound said to avoid the body timestamp because one standing
	# body is edited in place. MEASURED on fw#996: these are DISTINCT comments with
	# created_at == updated_at (05:20:43 both), so the bound did not reproduce on that
	# row — and Qodo bodies DO cite 40-hex SHAs there (19 of them), so the pointer
	# method catches Qodo attestations too, not only audit ones.
	# Rather than choose, report both and count the rows where they diverge.
	cj=$(fetch "repos/$REPO/issues/$pr/comments" '.[] | (.created_at) + " " + (.updated_at) + " " + (.body|gsub("\n";" "))') \
		|| { printf '%s\tUNDETERMINED\t-\t-\t-\n' "$pr"; continue; }
	kj=$(fetch "repos/$REPO/pulls/$pr/commits" '.[] | (.commit.committer.date) + " " + (.sha)') \
		|| { printf '%s\tUNDETERMINED\t-\t-\t-\n' "$pr"; continue; }

	# commit timeline as epoch<TAB>sha
	tl=$(printf '%s\n' "$kj" | while read -r d s; do printf '%s\t%s\n' "$(date -u -d "$d" +%s 2>/dev/null)" "$s"; done | sort -n)

	ptrs=0; ivals=""; ivals_u=""; diverge=0; newest_ts=0; newest_sha=""
	while IFS= read -r line; do
		[ -z "$line" ] && continue
		cts=${line%% *}; rest=${line#* }
		uts=${rest%% *}; body=${rest#* }
		cep=$(date -u -d "$cts" +%s 2>/dev/null) || continue
		uep=$(date -u -d "$uts" +%s 2>/dev/null) || uep=$cep
		[ $((uep-cep)) -gt 60 ] && diverge=$((diverge+1))
		# candidate SHAs: validated as real commits, and must be THIS PR's commits
		hit=""
		for c in $(printf '%s' "$body" | grep -oE '\b[0-9a-f]{40}\b' | sort -u); do
			[ "$(git -C "$TREE" cat-file -t "$c" 2>/dev/null)" = "commit" ] || continue
			printf '%s' "$tl" | cut -f2 | grep -qx "$c" || continue
			hit="$c"; break
		done
		[ -z "$hit" ] && continue
		ptrs=$((ptrs+1))
		[ "$cep" -gt "$newest_ts" ] && { newest_ts=$cep; newest_sha=$hit; }
		# first commit strictly AFTER this pointer comment
		nxt=$(printf '%s' "$tl" | awk -F'\t' -v t="$cep" '$1>t {print $1; exit}')
		[ -n "$nxt" ] && ivals="$ivals$((nxt-cep)) "
		nxu=$(printf '%s' "$tl" | awk -F'\t' -v t="$uep" '$1>t {print $1; exit}')
		[ -n "$nxu" ] && ivals_u="$ivals_u$((nxu-uep)) "
	done <<< "$cj"

	if [ -z "$newest_sha" ]; then stale="NO_ATTESTATION"
	elif [ "$newest_sha" = "$live" ]; then stale="current"
	else stale="STALE"; fi
	printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$pr" "${live:0:10}" "$ptrs" "${ivals:--}" "${ivals_u:--}" "$stale|div=$diverge"
done
