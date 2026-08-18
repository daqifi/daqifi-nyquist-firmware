# Secrets — where to get them

This repo's secrets are **not committed**. They live in the **`daqifi-nyquist-firmware`** project in
Bitwarden Secrets Manager (BWS), id `61680959-2c3f-4a5e-82da-b4aa00621e74` (org `8ee1c697-...`):

- `FILE:.env` -> `.env` — WIFI_PASS

## Get them onto a box
1. One-time: install `bws` + place a read-scoped token (see the `bws` skill):
   `bash ~/.claude/skills/bws/install-bws.sh`, then a token at `~/.config/bws/token` (chmod 600).
2. `bash bws-pull.sh` — writes `.env` (0600) from BWS.

Plaintext `.env` is intentionally **not** kept on disk — pull it when needed.
Full workflow: `~/.claude/skills/bws/SKILL.md`.
