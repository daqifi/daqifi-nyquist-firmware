#!/usr/bin/env bash
# bws-pull.sh — materialize this repo's secrets (.env) from its Bitwarden Secrets
# Manager project. Run before anything that reads .env. See SECRETS.md.
set -euo pipefail
REPO="$(cd "$(dirname "$0")" && pwd)"
PROJ="61680959-2c3f-4a5e-82da-b4aa00621e74"
export PATH="$HOME/.local/bin:$PATH"
TOKEN="${BWS_TOKEN_FILE:-$HOME/.config/bws/token}"
[ -r "$TOKEN" ] || { echo "bws-pull: no token at $TOKEN" >&2; exit 1; }
command -v bws >/dev/null 2>&1 || { echo "bws-pull: bws not installed (bash ~/.claude/skills/bws/install-bws.sh)" >&2; exit 1; }
export BWS_ACCESS_TOKEN="$(cat "$TOKEN")"
bws secret list "$PROJ" -o json | python3 "$REPO/bws_materialize.py" "$REPO"
echo "bws-pull: materialized .env from BWS -> $REPO"
