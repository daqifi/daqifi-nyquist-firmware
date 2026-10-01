#!/usr/bin/env bash
# Field set for every `parked` row in daqifi-python-test-suite. READ-ONLY.
# Payload-verified: the row count is re-read and compared, never assumed.
# Selection and shaping in jq, never grep over raw text (ugrep classifies GitHub
# payloads with sliced multibyte chars as binary and exits rc=1 = "no match").
# DIRTY is carried as its OWN field -- letting `parked` absorb it cost the firmware
# dirty count twice.
set -u
R=daqifi/daqifi-python-test-suite
gh pr list --repo "$R" --state open --label parked --limit 100 \
  --json number,state,isDraft,labels,mergeable,mergeStateStatus,baseRefName,headRefOid,title \
  --jq '.[] | [ (.number|tostring), (if .isDraft then "draft" else "ready" end),
                .mergeable, .mergeStateStatus, .baseRefName,
                (.headRefOid[0:10]),
                ([.labels[].name] | join(",")),
                (.title[0:58]) ] | @tsv'
