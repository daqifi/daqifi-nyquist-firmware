# Release packaging

> Split out of `CLAUDE.md` on 2026-09-10. That file is loaded into **every turn of
> every agent**, and at ~38k tokens it was ~13% of all token spend; this material is
> reference — needed when you work in this area, not on every task. **It is
> unchanged, not summarised.** Read it in full before changing anything it
> describes, and update it here rather than re-adding it to `CLAUDE.md`.

Building the bootloader-linked hex, the `.hex` asset the in-app updater REQUIRES, and publishing.

---

### Packaging Release Artifacts

The version string comes from `FIRMWARE_REVISION` in `firmware/src/version.h`.

### 1. Build the production hex **bootloader-linked** (required for customer devices)

The release hex MUST be built with the **bootloader linker script**
`old_hv2_bootld.ld` (MPLAB X Project Properties → include `old_hv2_bootld.ld`,
exclude `p32MZ2048EFM144.ld`), **not** the default standalone build. The
bootloader-linked layout places the application at `0x9D000480` (leaving
`0x9D000000`–`0x9D000480` for the bootloader); the standalone build links the
app at `0x9D000000` and will **clobber the bootloader** if flashed on a customer
device. The standalone hex is for direct PICkit bench flashing only — never ship it.

Verify the candidate hex before publishing: lowest physical address `0x1D000000`,
exactly **408 bytes** in `[0x1D000000, 0x1D000480)` (the `.reset` vector), and the
bulk of code from `0x1D000480`. This matches every shipped release (v3.4.4 / v3.4.6b1 /
v3.5.0 are all identical in shape — only the top address grows with code size). The
`kseg0_program_mem` origin in the `.map` must read `0x9d000480`.

### 2. Attach a raw **`.hex`** asset — REQUIRED for the Windows in-app updater

The desktop app's in-app firmware update (`daqifi-core`
`Firmware/GitHubFirmwareDownloadService.cs`) queries
`api.github.com/repos/daqifi/daqifi-nyquist-firmware/releases`, takes the newest
**non-draft, non-prerelease** release, and selects the main-firmware asset by
**`name.EndsWith(".hex")`**. A release with **no `.hex` asset is skipped entirely** —
the app keeps offering the previous release. The `.zip` is **ignored** by the main-
firmware updater (zip is only consumed for the *WiFi* firmware path,
`wifi-firmware-<tag>.zip`). The manual flash dialog likewise filters `*.hex`.

So every full release that should be flashable in-app MUST attach a raw `.hex`
(any name ending `.hex`; the proven shape is v3.4.4's `DAQiFi_Nyquist.hex`):

```bash
cp firmware/daqifi.X/dist/default/production/daqifi.X.production.hex \
   "daqifi-nyquist-firmware-<VERSION>.hex"
```

### 3. Also zip it (archival / manual distribution)

```bash
zip -j "daqifi-nyquist-firmware-<VERSION>.zip" firmware/daqifi.X/dist/default/production/daqifi.X.production.hex
```
Example: `daqifi-nyquist-firmware-3.5.0.zip`.

### 4. Publish with both assets

```bash
gh release create v<VERSION> --repo daqifi/daqifi-nyquist-firmware --target main \
  --title "..." --notes-file docs/release-notes/RELEASE_NOTES_v<VERSION>.md --latest \
  "daqifi-nyquist-firmware-<VERSION>.hex" "daqifi-nyquist-firmware-<VERSION>.zip"
```
The updater deterministically picks the `.hex`; the `.zip` stays for archival/manual use.

**Verify before declaring the release done:** `gh api repos/daqifi/daqifi-nyquist-firmware/releases`
→ confirm the new tag is the newest **non-draft, non-prerelease** release that **has a `.hex` asset**.
Otherwise the in-app updater will silently keep offering the prior version.
