# Release Notes Guide

This guide defines the format and process for drafting firmware release notes
for the DAQiFi Nyquist project. It exists so every release reads the same way
and so that future maintainers (human or AI) can produce notes from a PR list
without reinventing structure each time.

Past releases (v3.4.3, v3.4.4, v3.4.6b1) used a simple prose-only template.
The v3.4.7 template extends that with explicit `Added / Changed / Fixed /
Internal` buckets. New releases should follow this extended format.

## When to draft

Open a release-notes draft when:

1. The release tracking issue (e.g. `Release vX.Y.Z tracking — validate N
   commits since vP.Q.R`) is close to "ready to cut" status.
2. The release tracker has a categorized PR list — that list is the
   primary input. Don't try to categorize 100 PRs from scratch when the
   tracker already did the work.
3. `FIRMWARE_REVISION` in `firmware/src/version.h` has been bumped (or is
   about to be).

## File layout

Draft both files in this order:

1. **`RELEASE_NOTES_<version>.md` at repo root** — the actual notes for this
   release. One file per release. The root location keeps it visible and
   easy for the release tagger to find. Committed alongside the release
   PR and kept in the repo as a historical record (this convention starts
   with v3.4.7b1; pre-v3.4.7b1 releases lived only in the GitHub release
   body and are not in the tree). Example: `RELEASE_NOTES_v3.4.7b1.md`.
2. **`docs/RELEASE_NOTES_GUIDE.md`** — this guide. Update only when the
   format itself changes, not for each release.

After the release ships, the `RELEASE_NOTES_<version>.md` body becomes the
GitHub Release description (`gh release create vX.Y.Z --notes-file
RELEASE_NOTES_vX.Y.Z.md`). The file stays in the repo as a historical
record.

## The template

```markdown
# vX.Y.Z - <SHORT THEME>

## Highlights

1-3 paragraphs of marketing-grade prose. Write it so a customer reading
the release page understands what changed about the product. Not a commit
log — a story. Pick the 3-5 biggest customer-visible arcs and tell them.

## Added
### <Feature title> (PR #N)
- User-visible SCPI commands or behavior
- Measured impact, citing CLAUDE.md tables or PR bodies — never invented

## Changed
### <Behavioral change> (PR #N)
- What changed
- Client-library migration notes if applicable

## Fixed
### <Bug fix title> (PR #N)
- Symptom, scope, one-line root cause

## Internal
PRs #N #N #N — build/CI/refactor/docs/observability/test. One line each,
grouped by theme. Do NOT give each chore PR its own sub-section.

## Test coverage
- Bench characterization sessions (see CLAUDE.md)
- Overnight ceiling + endurance sweeps
- Manual functional tests (per release-tracker MED-tier checklist)
- cppcheck baseline status

## Known limitations
- Open issues that aren't blocking the release
- Features deferred to next release
- WIP investigations

---

## Upgrade notes
- Client-library compatibility (SCPI alias notes)
- New default behaviors that change UX
- NVM-persisted settings that may need re-saving
```

## Categorization rules

Every merged PR lands in exactly one bucket. Use these decision rules in
order — apply the first one that matches.

### Highlights

A PR earns Highlights billing when **all three** of these are true:

1. **Customer-visible.** A user running the firmware notices the change
   in normal operation. Examples: new SCPI command, throughput jump,
   fewer crashes, new auto-recovery behavior.
2. **Substantial.** It changes a top-line capability or eliminates a
   long-running pain point. "Fixed a typo in a log message" does not
   count, even if the log is customer-visible.
3. **Stand-alone interesting.** A bench operator or library author wants
   to know about it specifically. Not "we cleaned up volatile usage" —
   that's a means, not an end.

If a change is internal correctness (#410 volatile audit, #347 stack
overflow fix, #354 ADC stack overflow, #421 set-once pointer audit), it
goes in `Internal`, NOT `Highlights`, even if it required heroic work.
Highlights describe what the product does better, not how cleanly the
code got there.

### Added — new features

A `feat()` commit that gives users a new capability or SCPI command they
didn't have before. Example shape: "Added the `SYST:STR:THRoughput`
command for self-contained throughput benchmarks."

Sub-section per feature. Inline the PR number in parentheses in the
heading. List the SCPI surface area and the measured impact if there is
one (cite CLAUDE.md or the PR body — don't invent numbers).

### Changed — behavior changes that aren't bug fixes

- SCPI command rename (with legacy alias retained → `Changed`, no alias →
  this is a breaking change, mention in Upgrade notes)
- Default value changes (e.g. WiFi buffer 32 KB → 64 KB auto-balance)
- New default behaviors that existing customers might notice (e.g. #335
  WINC idle-gate — silent under the hood but changes observed timing)
- Performance characteristic changes (e.g. cap formula updates)

Each gets a sub-section with PR number, what changed, and migration notes
for clients if any.

### Fixed — bug fixes

A `fix()` commit that resolves observable wrong behavior. Each gets a
sub-section. Format: symptom (what users saw), scope (which configs were
affected), one-line root cause. Don't write a PR-body-length narrative —
link to the PR for that.

Group very-similar fixes if they're the same root cause across PRs (e.g.
"WiFi reconnect resilience" can cover #416 + #423 + #425 in one sub-
section).

### Internal — everything else

PRs that don't change observable behavior. Lump these into a single
bulleted list, one line per PR, grouped by theme. Categories:

- **Build / CI / kernel tuning** (#414, #426, #432, #459)
- **Refactors and renames without behavior change** (#316, #319, #320,
  #339)
- **Observability and instrumentation** (#244, #265, #266, #295-#297)
- **Documentation and audits** (#321, #338, #357, #431, #433, #442,
  #444)
- **Test infrastructure** (#308's DIO probes, etc)
- **Defensive correctness** (#347 stack, #354 stack, #410 volatile —
  these are the hard ones that *look* like fixes but didn't fix an
  observed-in-the-wild bug; they hardened an internal hazard)

### Tie-break: when in doubt, demote

If you can't decide between `Highlights` and `Added`, choose `Added`.
If you can't decide between `Added` and `Internal`, choose `Internal`.
The customer reading the page benefits from short, focused Highlights
and Added sections; nobody is harmed by an extra line in Internal.

## Examples from past releases

### v3.4.6b1 (March 2026)

Simple two-bucket format (`New Features` + `Bug Fixes`), no
Added/Changed/Fixed split. Worked at small scale (4 PRs total). Useful
as a Highlights template:

> This prerelease adds SCPI status register support, streaming
> diagnostics improvements, and automatic device hostname/SSID
> differentiation for multi-device setups.

The PRs were then listed under "New Features" and "Bug Fixes" as
sub-sections. v3.4.7's scale (~100 PRs) breaks this format — too much
prose, hard to scan. Hence the Added/Changed/Fixed/Internal split.

### v3.4.4 (March 2026)

Single-theme release (USB power fix). Highlights wrote the story:

> This release fixes a critical USB power issue where the device would
> not draw current from USB when a battery was present. The root cause
> was the BQ24297 charger IC classifying the USB host as a 100 mA
> source...

Used "Bug Fixes" sub-sections per PR, plus an explicit "Known Issues"
section pointing at the deferred ticket. This is the model for `Fixed`
sub-sections in the extended template.

### v3.4.3 (Pre-v3.4 release)

Multi-theme release, used "New Features / Stability Fixes / Breaking
Changes / Commits Since v3.2.0" sections. The "Breaking Changes: None"
explicit-zero pattern is useful — call out backward compatibility
explicitly when it's a feature.

## Process

### 1. Set up the branch

```bash
git checkout main
git pull origin main
git checkout -b docs/release-notes-vX.Y.Z
```

### 2. Source the PR list

The release tracker (e.g. issue #464 for v3.4.7) is the canonical
categorization. Read it first:

```bash
gh issue view <TRACKER_NUM> --repo daqifi/daqifi-nyquist-firmware \
  --json body --jq .body
```

The tracker already groups PRs by risk/coverage area:

- Hardware-touching streaming / ADC (HIGH)
- WiFi state machine / WINC interface (HIGH)
- Power / boot / init (MED)
- SCPI surface (MED)
- Memory / pool / buffer management (MED)
- SD card path (MED)
- Logging / observability / diagnostics (LOW)
- Build / CI / kernel / docs / chores (LOW)

Map that risk grouping to the release-notes bucket:

- HIGH groups → mostly `Highlights` + `Added`/`Fixed`
- MED groups → split between `Added`, `Changed`, `Fixed`, `Internal`
- LOW groups → almost all `Internal`

Then cross-check with the raw PR list to catch anything the tracker
missed (often late-arriving PRs after the tracker was authored):

```bash
gh pr list --repo daqifi/daqifi-nyquist-firmware \
  --base main --state merged --limit 200 \
  --json number,title,labels,mergedAt \
  --jq '[.[] | select(.mergedAt > "<PREV_RELEASE_DATE>T00:00:00Z")] |
        sort_by(.mergedAt) |
        .[] | "\(.number)\t\(.mergedAt[:10])\t\(.title)"'
```

Filter `mergedAt` by the previous release date.

### 3. Source measured-impact numbers

Bench characterization numbers come from **CLAUDE.md** — search for
`Session 22`, `Session 23`, `Session 24` tables and quote conservatively.
Never invent numbers. If the PR body has a measurement, cite it (e.g.
"PR #335: USB CSV 16ch CV at 10 kHz: 9.44% → 0.5%, p-p 326 µs → 8.8
µs"). If no measurement is documented, write "see PR #N for details" and
don't make one up.

### 4. Read past releases for tone

```bash
gh release view v3.4.6b1 --repo daqifi/daqifi-nyquist-firmware \
  --json body --jq .body
```

Match the conversational-marketing tone in `Highlights`. Match the
terse-bullet style in `Fixed` / `Internal`.

### 5. Draft both files

- `RELEASE_NOTES_<version>.md` at repo root
- Update `docs/RELEASE_NOTES_GUIDE.md` only if the template itself
  changes

Target sizes:

- Highlights: 200-400 words
- Added: ~1 sub-section per substantive feat PR (typically 3-8 features)
- Changed: only if there is real behavior change (often empty or short)
- Fixed: ~1 sub-section per fix PR or per fix-group (typically 5-12)
- Internal: 30-40 PRs in a single bulleted list — DO NOT split per PR
- Known limitations + Upgrade notes: 5-10 bullets total

Aim for the full file to be 400-600 lines for a 100-PR release. Shorter
for smaller releases.

### 6. Commit and PR

```bash
git add docs/RELEASE_NOTES_GUIDE.md RELEASE_NOTES_<version>.md
git commit -m "$(cat <<'EOF'
docs(release): vX.Y.Z release notes + format guide (#<TRACKER>)

<one-paragraph context — what release this is for, what changed in the
format guide if anything>
EOF
)"
git push -u origin docs/release-notes-vX.Y.Z
gh pr create --title "docs(release): vX.Y.Z release notes + format guide (#<TRACKER>)" \
  --body "..."
```

The PR should request a human review of the Highlights prose and the
Known Limitations wording specifically — those are the parts most likely
to need user input.

### 7. After merge: tag and zip

Per CLAUDE.md's "Packaging Release Artifacts" recipe:

```bash
zip -j "daqifi-nyquist-firmware-<version>.zip" \
  firmware/daqifi.X/dist/default/production/daqifi.X.production.hex
```

The version string is the value of `FIRMWARE_REVISION` in
`firmware/src/version.h` (e.g. `3.4.7b1` → `daqifi-nyquist-firmware-3.4.7b1.zip`).

Tag the release and upload the zip + hex:

```bash
git tag -a v<version> -m "v<version>"
git push origin v<version>
gh release create v<version> \
  --repo daqifi/daqifi-nyquist-firmware \
  --title "v<version> - <SHORT THEME>" \
  --notes-file RELEASE_NOTES_v<version>.md \
  daqifi-nyquist-firmware-<version>.zip \
  firmware/daqifi.X/dist/default/production/daqifi.X.production.hex
```

For `b1`/`b2` prereleases, add `--prerelease` to the `gh release
create` invocation.

### 8. Update wiki SCPI page

If the release adds, renames, or removes SCPI commands, update the wiki
SCPI reference per CLAUDE.md's "SCPI Wiki Maintenance" section:

```bash
git clone https://github.com/daqifi/daqifi-nyquist-firmware.wiki.git \
  /tmp/daqifi-wiki
# edit 01-SCPI-Interface.md
# commit + push
```

## Common pitfalls

### Inventing numbers

If a PR body says "+15% throughput" but doesn't specify config, do not
extrapolate. Quote what was actually measured ("PR #278: +15-18% on
16ch in benchmark") or just link to the PR.

### Hyping internal correctness as headline news

The temptation is real for things like #354 (ADC stack overflow), #410
(volatile audit), #421 (set-once pointer audit). They feel important —
they took weeks of investigation. But the customer-visible win is "no
more SCPI-over-WiFi crashes when running diagnostic commands during
streaming", not "fixed a 5 KB stack-local in ADC_WriteChannelStateAll".
Lead with the customer outcome; the deep root-cause discussion belongs
in the PR body and the engineering memory files.

### Letting Internal sprawl

If `Internal` exceeds ~40 items, sub-categorize but DO NOT add per-PR
headings. Use bullet groups:

```markdown
## Internal

**Build / CI / kernel:** #414 cppcheck gate · #426 configLIST_VOLATILE
(drops FreeRTOS_tasks.c -O1 clamp) · #432 kernel build hygiene ·
#459 parallelize cppcheck (10m → 23s).

**Refactors and renames:** #316, #319, #320, #322, #323, #339 — SCPI
command surface renames (legacy aliases retained — see Upgrade notes).

**Observability:** #244 add LOG_E to silent failure paths · #265/#266
streaming ISR overrun counters · #295-#297 silent-loss instrumentation ·
#319 SCPI execution-error logging.

**Documentation and audits:** #321, #338, #357 (Session 20/21/22
characterization refreshes) · #431, #433, #444 (atomicity rules, set-
once pointer audits) · #298 BUSL-1.1 license.
```

### Treating prereleases as throwaway

Even `b1`/`b2` prereleases get full release notes. They're the only
record of what was in that build, and customers may pin to a prerelease
for months waiting for a `.0` cut.

## Updating this guide

If a release introduces a format change (new section, removed section,
renamed bucket, new categorization rule):

1. Make the change in `docs/RELEASE_NOTES_GUIDE.md` in the same PR as
   the first `RELEASE_NOTES_<version>.md` that uses it.
2. Update the "examples from past releases" section if the change
   meaningfully alters how the previous releases would have looked.
3. Mention the format change in the release notes themselves (so
   readers know this release's notes look different from prior ones).
