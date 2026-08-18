#!/usr/bin/env python3
"""Materialize secrets from a Bitwarden Secrets Manager project to disk.

Reads `bws secret list <project> -o json` on stdin and writes under the repo root
(argv[1]):

  * key `FILE:<relpath>`  -> <repo>/<relpath> verbatim  (e.g. FILE:secrets/x.json)
  * any bare key          -> a `KEY=value` line in <repo>/.env
  * key `<NAME>_DOTENV`    -> merged into <repo>/.env verbatim  (legacy whole-blob)

The preferred convention is **one secret per env var** (bare keys) — each is
individually editable in the Bitwarden UI. They're reassembled into `.env` here
(keys sorted, each LF-terminated). `_DOTENV` is still honoured for back-compat.

Files are written 0600. Secret values arrive on stdin only (never argv/ps).
"""
import json
import os
import sys

repo = sys.argv[1]
env_pairs = []
dotenv_blob = ""
for s in json.load(sys.stdin):
    key, val = s["key"], s["value"]
    if key.startswith("FILE:"):
        dest = os.path.join(repo, key[5:])
        os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
        fd = os.open(dest, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        os.write(fd, val.encode())
        os.close(fd)
    elif key.endswith("_DOTENV"):
        dotenv_blob = val
    else:
        env_pairs.append((key, val))

if env_pairs or dotenv_blob:
    body = (dotenv_blob.rstrip("\n") + "\n") if dotenv_blob else ""
    body += "".join("%s=%s\n" % (k, v) for k, v in sorted(env_pairs))
    dest = os.path.join(repo, ".env")
    fd = os.open(dest, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    os.write(fd, body.encode())
    os.close(fd)
