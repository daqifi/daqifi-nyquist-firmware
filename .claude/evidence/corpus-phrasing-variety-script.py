#!/usr/bin/env python3
"""Measure PHRASING VARIETY of the arbiter's merge relation over nq-b's
pre-registered 10-artifact corpus.

NOT a classifier. Two outputs only:
  (A) conv-fw's EXACT regexes (copied verbatim from their :204 and :209)
      applied per rationale, hit counts reported.
  (B) a DELIBERATELY OVER-BROAD token net; every matching rationale is printed
      VERBATIM with artifact + index. Classification is conv-fw's.

The point of (B) being over-broad: a net tuned to the five known phrasings
cannot discover a sixth, which is the exact failure conv-fw predicted.
"""
import json, re, sys, os

# --- conv-fw's patterns, copied verbatim. DO NOT "improve". ---
RE_INDEXED = r'(?:same defect as|twin of)\s+index\s+(\d+)'
RE_OTHER   = r'\b(same defect|duplicate of|twin)\b'

# --- my over-broad net: ANY token that could reference another finding ---
RE_NET = re.compile(
    r'\b(dup\w*|twin\w*|same|identical|overlap\w*|subsum\w*|merge\w*|'
    r'collaps\w*|redundant|already|covered by|see\s+(?:index|#)|'
    r'index\s*\d+|#\d+|finding\s*\d+|item\s*\d+)\b', re.I)

def dispositions(doc):
    d = doc.get('result', doc) if isinstance(doc, dict) else doc
    arb = (d or {}).get('arbiter') or {}
    return arb.get('dispositions') or []

def main(paths):
    tot = 0
    idx_hits = other_hits = net_hits = 0
    net_rows = []
    for p in paths:
        try:
            doc = json.load(open(p, encoding='utf-8'))
        except Exception as e:
            print(f"!! {os.path.basename(p)}: {e}"); continue
        for i, dp in enumerate(dispositions(doc)):
            r = dp.get('rationale') or ''
            if not r:
                continue
            tot += 1
            m = re.findall(RE_INDEXED, r, re.I)
            if m:
                idx_hits += 1
            elif re.search(RE_OTHER, r, re.I):
                other_hits += 1
            if RE_NET.search(r):
                net_hits += 1
                net_rows.append((os.path.basename(p), i, r))

    print(f"=== (A) conv-fw's EXACT regexes over {tot} rationales")
    print(f"  matched RE_INDEXED (back/fwd eligible) : {idx_hits}")
    print(f"  fell through to RE_OTHER (nOtherMerge) : {other_hits}")
    print(f"  matched NEITHER                        : {tot - idx_hits - other_hits}")
    print(f"\n=== (B) over-broad net: {net_hits}/{tot} rationales, VERBATIM, unclassified")
    for fn, i, r in net_rows:
        print(f"\n--- {fn}  disposition[{i}]")
        print(r)

if __name__ == '__main__':
    main(sys.argv[1:])
