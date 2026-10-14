# My WiFi restore recipe was broken in two ways, and `batch.sh` does **not** auto-load the env file

2026-10-14. Every flash wipes NVM, so the fw#1027 flash required restoring WiFi. My own station-local
recipe `.claude/wifi_restore.batch` **failed twice before it worked.** Fixed, re-run, verified.

## Defect 1 — the recipe used the OLD verify syntax, which now means something else

```
[  7] SET SYSTem:POWer:STATe? 1  -> '1**ERROR: -108, "Parameter not allowed"'
      ABORTED at line 7 after 1 failure(s)
```

The recipe's own header said *"Verify syntax is `CMD? expected` (the expected value follows the
query)."* **`batch.sh:33` now says the opposite:**

> *"VERIFY is keyed on a **LEADING** `'?'`. A trailing `'?'` is part of the SCPI mnemonic and never
> means verify — so a parameterised query is sent **whole** instead of being truncated at its first
> argument."*

So `SYSTem:POWer:STATe? 1` is now transmitted as a **parameterised query** — `POWer:STATe?` with
argument `1` — which the device correctly refuses. ⭐ **A tool fix in both directions turned a working
recipe into a broken one, silently**, and `batch.sh:294` says so: the old behaviour *"made EVERY
parameterised query look like a verify."* The recipe is station-local and nothing re-ran it, so it sat
broken from the change until the next flash needed it.

⚠ **My memory had this and I did not apply it.** `CORRECTION-finding-8-was-mine-and-was-wrong.md`
records: *"`batch.sh` fixed at claude-skills `2c11ba7` … Verify is now keyed on a **leading `?`**.
**I have not re-tested it.**"* **I wrote "I have not re-tested it" and then hit the untested thing.**
Fifth instance today of the written-not-run shape.

**Fix:** `?SYSTem:POWer:STATe? 1` — leading `?` marks the verify, trailing `?` stays part of the
mnemonic. Verified working: lines 15/18/20/24 all report `== '<expected>'  OK`.

## Defect 2 — ⛔ the recipe's header described a sequence its body did not implement

Header: *"power on → ENAbled 0 → set fields → ENAbled 1 → APPLY → settle → verify ADDR? → SAVE."*
Body: **ended at `APPLY`.** No settle, no `ADDRess?`, **and no `SAVE`.**

> **Without `SAVE` nothing persists to NVM, so the restore would have been reverted by the next
> reboot** — and the recipe would have looked successful every time. **A header documenting a step the
> body omits is worse than no header**: it is checkable-looking and unchecked, the
> stated-but-unimplemented shape at the recipe layer.

**Fix:** added `@wait 25`, `CONnected?`, `ADDRess?`, `SYSTem:COMMunicate:LAN:SAVE` — each command
**verified against the registered pattern table first** (`SAVE` and `ADDRess?` both exist; `SAVE` is
all-caps so `SAVE` is its only legal spelling, like `APPLY`).

## Defect 3 — `batch.sh` requires `--env`; it does NOT read `~/.daqifi.env` by itself

```
WARN: ${WIFI_SSID …
[ 21] SET SYSTem:COMMunicate:LAN:SSID …  -> ERR -200: Execution error
```

The literal `${WIFI_SSID}` reached the device. The variable names are correct — `~/.daqifi.env` defines
exactly `WIFI_SSID` and `WIFI_PASS`, and the recipe references exactly those two — but
`batch.sh:58` sets `ENV_FILE=""` and sources it only `if [[ -n "$ENV_FILE" ]]`, i.e. **only when
`--env` is passed.**

⚠ **CLAUDE.md implies otherwise:** *"`batch.sh` for stateful sequences (… `${VAR}` secrets from
`~/.daqifi.env`)"* — which reads as automatic. **It is not.** The invocation must be:

```
bash ~/.claude/skills/scpi/batch.sh <recipe> COM8 --env ~/.daqifi.env
```

⭐ **And an unsubstituted `${VAR}` fails LOUDLY here** (`WARN` + `-200` + abort), which is the good
direction — a silently-empty substitution would have set the SSID to an empty string and "succeeded".

## Result after all three fixes

```
[15] POWer:STATe? == '1' OK   [18] NETType? == '1' OK   [20] SECurity? == '3' OK
[24] ENAbled? == '1' OK       [25] APPLY   [26] @wait 25.0s
[27] CONnected?  -> 'CONNECTED'      [28] ADDRess?  -> <a real address>
[29] SET SYSTem:COMMunicate:LAN:SAVE                      exit 0
```

**Board `7E2873046200E891` is back on the bench AP with the configuration persisted.** No credential
was printed at any point: values live in `~/.daqifi.env` (mode 600), the recipe carries only `${VAR}`
references, the two credential lines were copied byte-identically during the rewrite without being
read, and every transcript line above was filtered.

## Why this is worth a file rather than a log line

**A restore recipe is only exercised after a flash, and a flash is exactly when you are busy.** All
three defects were latent, all three would have been blamed on the device, and one of them — the
missing `SAVE` — **would have reported success while persisting nothing.** That is the
`writeBytesTotal`-style failure at the tooling layer: an operation that counts the attempt, not the
effect.
