# Cutting a firmware release

This is the end-to-end process for shipping a new firmware version. The
load-bearing, error-prone part — building a **bootloader-linked** hex (so it
doesn't clobber the bootloader on customer devices) and proving its layout —
is automated by [`tools/release/cut_release.sh`](../tools/release/cut_release.sh).

> ⚠️ **The single most important rule:** the shipped `.hex` must be linked with
> `old_hv2_bootld.ld` (application at **`0x9D000480`**), NOT the default
> standalone bench layout (`0x9D000000`). A standalone hex flashed onto a
> customer device **overwrites the bootloader** and bricks the field-update
> path. The default MPLAB X `default` config is the standalone layout; the
> release build flips one linker-script flag. `cut_release.sh` does the flip and
> **hard-fails** if the resulting hex isn't bootloader-linked.

## Prerequisites

- MPLAB X v6.30 + XC32 v4.60 (see `CLAUDE.md` → Build Instructions).
- WSL with `powershell.exe` reachable (the makefile regen runs Windows-side —
  the Linux `prjMakefilesGenerator` fails with "Device pack missing").
- `gh` authenticated to `daqifi/daqifi-nyquist-firmware`.
- All feature PRs for the release already merged to `main` and **hardware-validated**.

## Steps

### 1. Land all feature work on `main`
Merge every PR that belongs in the release. Don't cut from a feature branch.

### 2. Bump the version (via PR)
`FIRMWARE_REVISION` in `firmware/src/version.h` is the single source of truth
(it's what `*IDN?` reports and what the desktop updater compares). Bump it on a
branch, open a PR, merge it. Direct pushes to `main` are blocked by branch
protection by design.

### 3. Write the release notes
`docs/release-notes/RELEASE_NOTES_v<VERSION>.md`. Follow the existing files'
shape: headline, Highlights (per issue), "All changes since v<prev>", and a
**Validation (hardware)** section with the bench serial and concrete results.
Commit it (it can ride in the same PR as the version bump, or its own).

### 4. Build + verify + package the bootloader-linked hex
From the repo root, on `main`:

```bash
bash tools/release/cut_release.sh --version <VERSION>
```

This:
1. flips the `default` config to include `old_hv2_bootld.ld` (keeps
   `p32MZ2048EFM144.ld` excluded),
2. regenerates the makefiles (Windows-side),
3. clean-builds,
4. **hard-verifies** the layout — `.map` `kseg0_program_mem` origin
   `0x9d000480`, lowest physical address `0x1D000000`, a ~408-byte `.reset`
   vector in `[0x1D000000, 0x1D000480)`, and the code bulk starting at
   `0x1D000480`. If any check fails it aborts (it will not let you ship a
   standalone hex),
5. packages `daqifi-nyquist-firmware-<VERSION>.hex` + `.zip`,
6. restores the working tree (reverts the config flip and regenerates the
   standalone makefiles so the next bench `make`/flash isn't a ship-layout
   build).

### 5. Publish (irreversible)
`cut_release.sh` prints the exact command. It attaches **both** a raw `.hex`
(REQUIRED — the desktop in-app updater selects the main-firmware asset by
`name.EndsWith(".hex")`; a release with no `.hex` is skipped) and a `.zip`
(archival; also the shape the WiFi-firmware path expects):

```bash
gh release create v<VERSION> --repo daqifi/daqifi-nyquist-firmware --target main \
  --title "v<VERSION> — <headline>" \
  --notes-file docs/release-notes/RELEASE_NOTES_v<VERSION>.md --latest \
  "daqifi-nyquist-firmware-<VERSION>.hex" "daqifi-nyquist-firmware-<VERSION>.zip"
```

### 6. Verify the updater will pick it up
The desktop app takes the **newest non-draft, non-prerelease** release that
**has a `.hex` asset**. Confirm the new tag qualifies:

```bash
gh api repos/daqifi/daqifi-nyquist-firmware/releases \
  --jq '.[] | select(.draft==false and .prerelease==false) |
        "\(.tag_name) hex=\([.assets[].name]|map(select(endswith(".hex")))|length>0)"' | head -3
```

The top line should be your new tag with `hex=true`.

### 7. Clean up
Delete the two asset files from the repo root (they're build artifacts, not
tracked). `cut_release.sh` leaves them for the `gh release create` step.

## Why the hex shape is checked so precisely

Every shipped release (v3.4.4 / v3.4.6b1 / v3.5.0 / v3.6.0 / v3.6.1 / v3.6.2)
has an identical *shape* — only the top address grows with code size:

| Property | Expected |
|---|---|
| `.map` `kseg0_program_mem` origin | `0x9d000480` |
| Lowest physical address (hex) | `0x1D000000` |
| Bytes in `[0x1D000000, 0x1D000480)` (`.reset` vector) | 408 |
| Code bulk starts at | `0x1D000480` |

A standalone build instead starts the bulk at `0x1D000000` and has no
bootloader gap — visually similar in a casual glance, catastrophic in the
field. The verification in `cut_release.sh` is the guardrail.
