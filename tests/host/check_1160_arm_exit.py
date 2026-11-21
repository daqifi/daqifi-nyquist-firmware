#!/usr/bin/env python3
"""#1160: does a REFUSED SD arm EXIT the handler? Compile the real text and run it.

`tools/lint/scpi_sd_arm_path.py` proves an arm wrapper's verdict is CONSUMED. It
cannot prove a refusal LEAVES the handler -- four shapes pass it (bare call,
`(void)` cast, captured and never tested, guarded with a branch that does not
exit), and the remedy is branch-gated reasoning, the class that file removed on
purpose after fifteen catalogued bypasses (#976). So the property lives here,
as dataflow, by EXECUTION.

WHAT THIS DOES, in order
  1. Extracts the REAL text of six handlers + `SD_ArmOrRefuse*` from
     SCPIStorageSD.c, and two SLICES (the SD:BENCHmark arm and the SYST:STR:START
     SD-logging arm) from their very large functions, with `hash_function.py`'s
     extractor -- the same definition rule the #971 pin and the lint use.
  2. Splices them into `test_1160_sd_arm_exit.scaffold.c` (host stand-ins +
     cases), compiles it, RUNS it. Control: arm accepted, the code after the
     arm is reached. Refusal: error result, nothing after the arm ran.
  3. For every site, builds the four shapes from the list above as MUTANTS of
     that site's real text and requires each to turn the SITE'S OWN refusal case
     red. A mutant that cannot be built or applied is REFUSED (exit 2), never a
     pass: a mutation proof that silently mutates nothing proves nothing.
  4. A sha256 DRIFT PIN per arm region (splice-joined, comment-stripped,
     whitespace-normalised, #971's precedent). The compiled text is always the
     live text, so nothing here can rot -- but the EXTRACTION is itself textual,
     and a reshape that makes it pull a different or partial region can still
     compile. The pin forces a human to look at an arm-region edit.

WHY THE SITES ARE NOT ONE PREDICATE. Six sites consume the verdict INLINE
(`if (!SD_ArmOrRefuse(...)) {...}`), two CAPTURED (`bool x = arm(...); if (!x)`).
Re-measured at the commit this was written against with the lint's own
`function_body()` / `_arm_verdict()` on comment-stripped text: CrcStart, GetData,
ListDir, Delete, Format (`SD_ArmOrRefuseWithCleanup`), SpaceGet -> `inline`;
SCPI_StorageSDBenchmark (`benchArmed`), SCPI_StartStreamingClaimed (`sdArmed`)
-> `captured`. This script re-derives the form per site from the text and
REFUSES if it is not the one declared below.

WHAT IT DOES NOT COVER (disclosed, not argued away)
  * Dataflow of WHICH OBJECT reaches the manager (lint finding `:1058`,
    "equivalent mode-clear syntax"): the stand-ins hand every site the one
    settings object, so an arm on the wrong object is invisible here.
  * Upstream of the arm: every precondition is stubbed to pass.
  * The mutants are the FOUR shapes in the ticket, applied to each site's
    arm statement. A fifth way to fall through is not covered by construction.
  * Concurrency: ordering under the claim is test_971's job.
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, os.pardir, os.pardir, "tools", "lint"))
import hash_function as hf          # noqa: E402
import cdef                          # noqa: E402

SD_SRC = os.path.join(HERE, "..", "..", "firmware", "src", "services", "SCPI",
                      "SCPIStorageSD.c")
IF_SRC = os.path.join(HERE, "..", "..", "firmware", "src", "services", "SCPI",
                      "SCPIInterface.c")
SCAFFOLD = os.path.join(HERE, "test_1160_sd_arm_exit.scaffold.c")

# Helpers compiled verbatim (the arm wrappers, which every inline site calls).
HELPERS = ("SD_ArmOrRefuseWithCleanup", "SD_ArmOrRefuse")

# (site name, source, enclosing function, arm call, consumption form)
SITES = (
    ("CrcStart", "sd", "SCPI_StorageSDCrcStart", "SD_ArmOrRefuse", "inline"),
    ("GetData", "sd", "SCPI_StorageSDGetData", "SD_ArmOrRefuse", "inline"),
    ("ListDir", "sd", "SCPI_StorageSDListDir", "SD_ArmOrRefuse", "inline"),
    ("Delete", "sd", "SCPI_StorageSDDelete", "SD_ArmOrRefuse", "inline"),
    ("Format", "sd", "SCPI_StorageSDFormat", "SD_ArmOrRefuseWithCleanup",
     "inline"),
    ("SpaceGet", "sd", "SCPI_StorageSDSpaceGet", "SD_ArmOrRefuse", "inline"),
    ("Benchmark", "sd", "SCPI_StorageSDBenchmark",
     "sd_card_manager_UpdateSettingsForPlainWrite", "captured"),
    ("Streaming", "if", "SCPI_StartStreamingClaimed",
     "sd_card_manager_UpdateSettingsForStreamingLog", "captured"),
)

# sha256 of hashed_code(arm region) per site. Re-derive with `--rederive` AFTER
# re-reading the region: the pin asserts "this text is unchanged", nothing more.
PINS = {
    "CrcStart": "2f600a6f9cdcedf3fcdcd309b61cb811102f5f23f33ebd88a5655d6e5641816d",
    "GetData": "f3bc09f7c86a5b761477eccee0babee73cfa227f628abbca97e272e35187061d",
    "ListDir": "455a47768d077fb0d79e3af75fc448c8930067058d364ff7e1a2811fdd5d5902",
    "Delete": "0446c28e18e615b4b1cec74c42edf1107227a9afad3ac47523174c809cc8d734",
    "Format": "c9435f4dce20d9e7bcd342ebce81f2cbe4af60f0eea31e9c4ddc07aed60ea2b9",
    "SpaceGet": "06781bc887fc32ff454d90c3fe72a0bc0587ef494c26e7b5b9b57fb036c049ab",
    "Benchmark": "0d65464aa8073c5d57f5340fdfb8739ca13818ec178777eb4b7f8cf262a9e17a",
    "Streaming": "167fb0affab77228ce4ee2aa78fe31356b43699da90cd7731a3625b49079c47f",
}

MUTANTS = ("bare", "void", "captured_untested", "guarded_no_exit")


class Refused(Exception):
    """Could not build/apply something. Exit 2 -- never read as a pass."""


def _match(text, i, open_ch, close_ch):
    depth = 0
    for j in range(i, len(text)):
        if text[j] == open_ch:
            depth += 1
        elif text[j] == close_ch:
            depth -= 1
            if depth == 0:
                return j
    raise Refused("unbalanced %s at offset %d" % (open_ch, i))


def _skip_ws(masked, i):
    while i < len(masked) and masked[i].isspace():
        i += 1
    return i


def locate(body, call, form, site):
    """-> dict describing the arm region of `site` inside `body`.

    Works on the offset-preserving MASK so a call named in a comment or string
    is not found, and requires EXACTLY ONE occurrence: two means the shape
    this mutates is ambiguous and refusing is the only honest answer.
    """
    masked = cdef.mask(body)
    if len(masked) != len(body):
        raise Refused("%s: cdef.mask is no longer length-preserving" % site)
    if form == "inline":
        pat = re.compile(r"\bif\s*\(\s*!\s*(%s)\s*\(" % re.escape(call))
    else:
        pat = re.compile(r"\bbool\s+(\w+)\s*=\s*(%s)\s*\(" % re.escape(call))
    hits = list(pat.finditer(masked))
    if len(hits) != 1:
        raise Refused("%s: expected exactly one %s-form arm of %s, found %d"
                      % (site, form, call, len(hits)))
    m = hits[0]
    if form == "inline":
        call_start = m.start(1)
        call_open = m.end() - 1
        call_close = _match(masked, call_open, "(", ")")
        cond_open = masked.index("(", m.start())
        cond_close = _match(masked, cond_open, "(", ")")
        b = _skip_ws(masked, cond_close + 1)
        if masked[b] != "{":
            raise Refused("%s: the arm's `if` has no braced block" % site)
        block_end = _match(masked, b, "{", "}")
        return {"start": m.start(), "end": block_end + 1,
                "call": body[call_start:call_close + 1]}
    var = m.group(1)
    call_start = m.start(2)
    call_open = m.end() - 1
    call_close = _match(masked, call_open, "(", ")")
    semi = _skip_ws(masked, call_close + 1)
    if masked[semi] != ";":
        raise Refused("%s: captured arm is not `bool %s = call(...);`" %
                      (site, var))
    ifm = re.compile(r"\s*if\s*\(\s*!\s*%s\s*\)\s*\{" % re.escape(var)).match(
        masked, semi + 1)
    if not ifm:
        raise Refused("%s: the captured verdict `%s` is not tested by the "
                      "very next statement" % (site, var))
    block_end = _match(masked, ifm.end() - 1, "{", "}")
    rel = re.compile(r"\s*sd_card_manager_ReleaseClaim\s*\(\s*\)\s*;").match(
        masked, block_end + 1)
    if not rel:
        raise Refused("%s: the success-path release does not directly follow "
                      "the refusal block" % site)
    return {"start": m.start(), "end": rel.end(),
            "call": body[call_start:call_close + 1], "var": var,
            "decl_end": semi + 1, "block_start": ifm.end() - 1 - (
                ifm.end() - 1 - semi - 1) + 0,
            "release": body[block_end + 1:rel.end()],
            "ifstart": semi + 1}


def mutant_text(loc, kind, form):
    """The text one shape puts in place of the arm region."""
    call = loc["call"]
    if form == "inline":
        return {
            "bare": call + ";",
            "void": "(void)" + call + ";",
            "captured_untested": "bool armed_1160 = " + call +
                                 "; (void)armed_1160;",
            "guarded_no_exit": "if (!" + call + ") { (void)0; }",
        }[kind]
    decl = "bool %s = %s;" % (loc["var"], call)
    rel = loc["release"]
    return {
        "bare": call + ";" + rel,
        "void": "(void)" + call + ";" + rel,
        "captured_untested": decl + " (void)%s;" % loc["var"] + rel,
        "guarded_no_exit": decl + " if (!%s) { (void)0; }" % loc["var"] + rel,
    }[kind]


def region_digest(text):
    return hashlib.sha256(hf.hashed_code(text).encode()).hexdigest()


def build_units():
    """-> (helper_text, {site: (body, loc)}, slices) from the live sources."""
    srcs = {}
    for key, path in (("sd", SD_SRC), ("if", IF_SRC)):
        with open(path, "r", encoding="utf-8", errors="strict") as fh:
            srcs[key] = fh.read()
    helpers = []
    for name in HELPERS:
        body = hf.extract(srcs["sd"], "static bool %s(" % name)
        if body is None:
            raise Refused("no definition of %s" % name)
        helpers.append(body)
    sites = {}
    for site, key, fn, call, form in SITES:
        body = hf.extract(srcs[key], "x %s(" % fn)
        if body is None:
            raise Refused("no definition of %s" % fn)
        sites[site] = (body, locate(body, call, form, site), fn, form)
    return helpers, sites


def render(helpers, sites, mutate=None):
    """Fill the scaffold. `mutate` = (site, kind) applies one mutant."""
    with open(SCAFFOLD, "r", encoding="utf-8") as fh:
        text = fh.read()
    funcs = list(helpers)
    slices = {}
    for site, (body, loc, fn, form) in sites.items():
        orig = body[loc["start"]:loc["end"]]
        repl = orig
        if mutate and mutate[0] == site:
            repl = mutant_text(loc, mutate[1], form)
            if repl == orig:
                raise Refused("mutant %s of %s changed nothing" %
                              (mutate[1], site))
        if site in ("Benchmark", "Streaming"):
            slices[site] = repl
        else:
            funcs.append(body[:loc["start"]] + repl + body[loc["end"]:])
    for ph, val in (("/*@@FUNCS@@*/", "\n\n".join(funcs)),
                    ("/*@@SLICE_BENCH@@*/", slices["Benchmark"]),
                    ("/*@@SLICE_STREAM@@*/", slices["Streaming"])):
        if text.count(ph) != 1:
            raise Refused("scaffold placeholder %s not found exactly once" % ph)
        text = text.replace(ph, val)
    return text


def compile_run(text, workdir, tag, out=None, extra=()):
    src = os.path.join(workdir, "t1160_%s.c" % tag)
    exe = out or os.path.join(workdir, "t1160_%s" % tag)
    with open(src, "w", encoding="utf-8") as fh:
        fh.write(text)
    cc = os.environ.get("CC", "gcc")
    cmd = [cc, "-std=c11", "-O0", "-g", "-I", HERE] + list(extra) + \
          ["-o", exe, src]
    cp = subprocess.run(cmd, capture_output=True, text=True)
    if cp.returncode != 0:
        raise Refused("compile failed (%s):\n%s" % (tag, cp.stderr[-3000:]))
    rp = subprocess.run([exe], capture_output=True, text=True)
    return rp.returncode, rp.stdout, exe


def main(argv):
    rederive = "--rederive" in argv
    out_exe = None
    if "--out" in argv:
        out_exe = os.path.abspath(argv[argv.index("--out") + 1])
    helpers, sites = build_units()

    digests = {s: region_digest(v[0][v[1]["start"]:v[1]["end"]])
               for s, v in sites.items()}
    if rederive:
        for s, d in digests.items():
            print('    "%s": "%s",' % (s, d))
        return 0
    bad = [] if "--skip-pin" in argv else [
        s for s in digests if PINS.get(s) != digests[s]]
    if bad:
        for s in bad:
            print("PIN DRIFT: arm region of %s is now %s (pinned %s)" %
                  (s, digests[s], PINS.get(s)))
        print("The text of an SD arm site changed. Re-read it -- does a "
              "refusal still EXIT? -- then re-derive with --rederive and "
              "retype the pin. The compile-and-run cases below are the "
              "behavioural check; the pin only forces the look.")
        return 1

    with tempfile.TemporaryDirectory(prefix="t1160_") as wd:
        rc, out, _ = compile_run(render(helpers, sites), wd, "base",
                                 out=out_exe)
        if rc != 0:
            sys.stdout.write(out)
            print("BASELINE FAILED: the real arm sites do not all exit on "
                  "refusal (or a control did not reach its follow-up).")
            return 1
        print("baseline: %s" % out.strip().splitlines()[-1])
        survived = []
        for site in sites:
            for kind in MUTANTS:
                rc, out, _ = compile_run(render(helpers, sites, (site, kind)),
                                         wd, "m", extra=("-w",))
                red = "[FAIL] %s_refusal_exits" % site in out
                print("  mutant %-9s %-17s -> %s" %
                      (site, kind, "killed" if (rc != 0 and red) else
                       "SURVIVED"))
                if rc == 0 or not red:
                    survived.append((site, kind))
        if survived:
            print("SURVIVING MUTANTS (the test cannot see these): %s" %
                  survived)
            return 1
    print("OK: %d sites x %d shapes, all killed" % (len(sites), len(MUTANTS)))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except Refused as exc:
        sys.stderr.write("REFUSED: %s\n" % exc)
        sys.exit(2)
