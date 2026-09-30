# ts#405 audit round 3 — the driver agent destroyed its own output

Primary evidence for the serialization bug, and the SPECIFICATION for the fix:
read the script's stdout, do not have an agent restate it.

Source transcript: `ts405-driver-agent-transcript-6c1b844.jsonl` (same dir).


## TOOL 1: Bash

```json
{"command": "bash \"$HOME\"/'.claude/skills/qodo-cycle/codex-audit.sh' --repo 'daqifi/daqifi-python-test-suite' --pr 405 --repo-path '/mnt/c/daqifi/wt/audit-ts405' --effort 'high' --timeout 460 --fixed-b64 Uk9VTkQgMywgVEhJUkQgQVRURU1QVCBhdCA2YzFiODQ0ODdmYjA0YjY5MzY1YjBlMGM3MWJmZjQyZTA0MGQwNTBhLiBUd28gcHJldmlvdXMgYXR0ZW1wdHMgREVHUkFERUQgaWRlbnRpY2FsbHk6IHRoZSBjb2RleCBwcm9kdWNlciBuZXZlciByYW4sIHRoZSBhdWRpdG9yIHJldHVybmVkIGFuIGVtcHR5IGZpbmRpbmcgbGlzdCBhZnRlciAzIHRvb2wgY2FsbHMsIHRoZSBibGluZCBsZWcgZGlkIG5vdCBydW4sIGFuZCBwcm92ZW5hbmNlIHdhcyBlbXB0eS4gVGhlIGF1ZGl0IHdvcmt0cmVlIGlzIHZlcmlmaWVkIGNsZWFuLCBjb3JyZWN0bHkgcGlubmVkIGF0IHRoaXMgaGVhZCwgd2l0aCBiYXNlLCBoZWFkIGFuZCBwcmlvciBoZWFkIG9iamVjdHMgYWxsIHByZXNlbnQsIGEgZnVsbCBmZXRjaCByZWZzcGVjLCAxNjYgcmVtb3RlIHJlZnMgYW5kIGEgY3VycmVudCBvcmlnaW4vbWFpbiwgc28gdGhlIHRyZWUgaXMgTk9UIHRoZSBjYXVzZS4gUFIgNDA1IGdhdGVzIHRlc3RfODYxX3N0b3BfcmFjZXNfc3RhcnRfcHJlYXJtLnB5J3MgcmFjZS1maWxlIERFTGV0ZSBvbiBwcm9vZiB0aGF0IHRoaXMgcnVuIHdyb3RlIHRoZSBmaWxlLiBUaGUgbGFzdCBmdWxseSBhdWRpdGVkIGhlYWQgd2FzIGQyNjY0YTkyZjRlOTM2NmY0MDc5ZGEzZTkyNjdhZjllZGE0ZmEyNWY7IHNpbmNlIHRoZW4gdGhlIGRpZmYgYWRkcyA0ODQgbGluZXMgdG8gdGVzdF84NjFfc3RvcF9yYWNlc19zdGFydF9wcmVhcm0ucHkgYW5kIDM4IHRvIHRlc3RfaG
```

**RESULT:**

```
{"repo":"daqifi/daqifi-python-test-suite","base_sha":"09133f383c99c2af1fe2cdfd318f6b91e0dd0516","head_sha":"6c1b84487fb04b69365b0e0c71bff42e040d050a","producer_sha":"7c541130ed829d03d3b1fc784586160378f02b21","findings":[],"truncated":false,"covered_bytes":69850,"total_bytes":69850,"tools_degraded":false,"files":["test_861_stop_races_start_prearm.py","test_harness.py"],"repo_path_head":"6c1b84487fb04b69365b0e0c71bff42e040d050a","repo_path_matches_head":true,"blind_findings":[]}
```

## TOOL 2: StructuredOutput

```json
{"input": "{\"findings\": [], \"repo\": \"daqifi/daqifi-python-test-suite\", \"base_sha\": \"09133f383c99c2af1fe2cdfd318f6b91e0dd0516\", \"head_sha\": \"6c1b84487fb04b69365b0e0c71bff42e040d050a\", \"producer_sha\": \"7c541130ed829d03d3b1fc784586160378f02b21\", \"files\": [\"test_861_stop_races_start_prearm.py\", \"test_harness.py\"], \"covered_bytes\": 69850, \"total_bytes\": 69850, \"truncated\": false, \"blind_findings\": []}"}
```

**RESULT:**

```
Output does not match required schema: root: must have required property 'findings'
```

## TOOL 3: StructuredOutput

```json
{"findings": []}
```

**RESULT:**

```
Structured output provided successfully
```

## Reading

TOOL 1 SUCCEEDED with full provenance and both legs. TOOL 2 double-encoded the payload
as a STRING under an `input` key, so the validator saw `{input: "..."}` and reported a
missing `findings` that was present one level down. TOOL 3 'recovered' by dropping every
field except `findings` — destroying the provenance. `codexProducerRan` is DERIVED from
`provenance`, so it then read false, describing a producer that had run perfectly.
