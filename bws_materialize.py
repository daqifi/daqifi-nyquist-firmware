#!/usr/bin/env python3
"""Materialize secrets from a Bitwarden Secrets Manager project to disk.

Reads `bws secret list <project> -o json` on stdin and writes each secret to its
destination under the repo root (argv[1]):

  * key `<NAME>_DOTENV`   -> <repo>/.env
  * key `FILE:<relpath>`  -> <repo>/<relpath>   (e.g. FILE:config/ssh.conf)

Files are written 0600. Any other key is ignored. Secret values never pass through
argv/ps — they arrive on stdin only.

Used by bws-pull.sh (and mirrored in the TSOfficeAgent container entrypoint).
"""
import json
import os
import sys

repo = sys.argv[1]
for s in json.load(sys.stdin):
    key, val = s["key"], s["value"]
    if key.endswith("_DOTENV"):
        dest = os.path.join(repo, ".env")
    elif key.startswith("FILE:"):
        dest = os.path.join(repo, key[5:])
    else:
        continue
    os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
    fd = os.open(dest, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    os.write(fd, val.encode())
    os.close(fd)
