#!/usr/bin/env bash
#
# cut_release.sh — build + verify + package a BOOTLOADER-LINKED release hex.
#
# The release hex MUST be linked with old_hv2_bootld.ld (app @ 0x9D000480) so it
# does not clobber the bootloader on customer devices. The default MPLAB X
# "default" config is the STANDALONE bench layout (app @ 0x9D000000) — flashing
# that to a customer device bricks the bootloader. This script does the linker
# flip, regenerates the makefiles, clean-builds, HARD-VERIFIES the resulting hex
# shape, packages the .hex + .zip assets, and restores the working tree.
#
# It does NOT run `gh release create` (that step is irreversible and needs the
# release notes) — it prints the exact command to run.
#
# Usage:
#   bash tools/release/cut_release.sh [--version X.Y.Z] [--keep-bootloader-makefiles]
#
#   --version X.Y.Z   Expected FIRMWARE_REVISION (asserts version.h matches;
#                     does NOT edit version.h — bump + merge that via PR first).
#                     Defaults to whatever version.h currently holds.
#   --keep-bootloader-makefiles
#                     Skip the final regen-back-to-standalone (leaves the on-disk
#                     makefiles bootloader-linked). Default restores standalone so
#                     a subsequent bench `make` / flash isn't accidentally a
#                     ship-layout build.
#
# Run from the repo root. Requires: MPLAB X v6.30, XC32 v4.60, WSL (wslpath),
# powershell.exe (for the Windows-side makefile regen — the Linux-side
# prjMakefilesGenerator fails with "Device pack missing" on this toolchain).
#
set -euo pipefail

MPLABX="/mnt/c/Program Files/Microchip/MPLABX/v6.30"
MAKE="$MPLABX/gnuBins/GnuWin32/bin/make.exe"
PRJGEN_WIN='C:\Program Files\Microchip\MPLABX\v6.30\mplab_platform\bin\prjMakefilesGenerator.bat'

KEEP_BL_MK=0
WANT_VER=""
while [ $# -gt 0 ]; do
  case "$1" in
    --version)
      [ $# -ge 2 ] || { echo "FATAL: --version requires an argument (X.Y.Z)" >&2; exit 2; }
      WANT_VER="$2"; shift 2 ;;
    --keep-bootloader-makefiles) KEEP_BL_MK=1; shift ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

# --- locate repo root (script lives in tools/release/) ---
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO"
PRJ="firmware/daqifi.X"
CFG="$PRJ/nbproject/configurations.xml"
HEX="$PRJ/dist/default/production/daqifi.X.production.hex"
MAP="$PRJ/dist/default/production/daqifi.X.production.map"
VERSION_H="firmware/src/version.h"

say() { printf '\n=== %s ===\n' "$*"; }
die() { printf '\nFATAL: %s\n' "$*" >&2; exit 1; }

# --- version check ---
VER="$(grep -oE 'FIRMWARE_REVISION[[:space:]]+"[^"]+"' "$VERSION_H" | grep -oE '[0-9][^"]*')"
[ -n "$VER" ] || die "could not read FIRMWARE_REVISION from $VERSION_H"
if [ -n "$WANT_VER" ] && [ "$WANT_VER" != "$VER" ]; then
  die "version.h says '$VER' but --version was '$WANT_VER'. Bump version.h (via PR) first."
fi
say "Building release for FIRMWARE_REVISION $VER"

BRANCH="$(git rev-parse --abbrev-ref HEAD)"
[ "$BRANCH" = "main" ] || echo "WARNING: not on main (on '$BRANCH') — releases are normally cut from main."
[ -z "$(git status --porcelain --untracked-files=no)" ] || echo "WARNING: working tree has tracked changes; configurations.xml will be restored to its pre-run state at the end."

# Unique per-run temp files (safe under concurrent runs / multiple clones).
BACKUP_CFG="$(mktemp -t cut_release_cfg.XXXXXX)"
BUILD_LOG="$(mktemp -t cut_release_build.XXXXXX.log)"

# --- 1. flip the default conf to bootloader-linked (include old_hv2_bootld.ld, keep p32MZ excluded) ---
say "Flipping default config to bootloader-linked (old_hv2_bootld.ld included)"
# Back up the pre-run file BEFORE arming the trap (reading $CFG is not a mutation),
# so cleanup() can always restore the exact pre-run state.
cp "$CFG" "$BACKUP_CFG"

# Restore the working tree on exit, even on failure. Armed BEFORE the first
# mutation (the sed below) so an early failure can never strand the
# bootloader-linked config on disk.
cleanup() {
  rc=$?   # capture the script's exit status FIRST, before anything resets $?
  say "Restoring configurations.xml (standalone default)"
  # Restore the EXACT pre-run file from our backup (preserves any uncommitted
  # local edits the user had); fall back to git only if the backup is missing.
  if [ -s "$BACKUP_CFG" ]; then
    cp "$BACKUP_CFG" "$CFG"
  else
    git checkout -- "$CFG" 2>/dev/null || true
  fi
  if [ "$KEEP_BL_MK" -eq 0 ]; then
    echo "  regenerating makefiles back to standalone (bench default)…"
    ( powershell.exe -Command "cd \"$(wslpath -w "$REPO/$PRJ")\"; & '$PRJGEN_WIN' -v ." ) >/dev/null 2>&1 \
      && echo "  standalone makefiles restored" \
      || echo "  WARNING: regen-back failed — run the regen manually before the next bench build"
  else
    echo "  --keep-bootloader-makefiles: on-disk makefiles remain bootloader-linked (regen before bench flashing)"
  fi
  rm -f "$BACKUP_CFG"
  [ "$rc" -eq 0 ] && rm -f "$BUILD_LOG"   # keep the build log only when something failed
}
trap cleanup EXIT

# Assert the starting state with the SAME checker CI uses (#767, #769).
#
# This began as a hand-rolled grep that only ever inspected old_hv2_bootld.ld
# and assumed ex= sat on the line AFTER the path. Both assumptions were wrong.
# Nothing verified p32MZ2048EFM144.ld was excluded, so the flip below could turn
# ONE selected script into TWO. And configurations.xml contains both single-line
# and wrapped <item> forms (129 vs 35 today), so an MPLAB X regen that emitted
# these entries single-line would have aborted a perfectly valid release cut.
#
# check_build_config.py parses the file as XML, so attribute layout and ordering
# cannot fool it, and it exits 0 only for the state this script requires: no
# custom linker script selected in the default conf. It carries an 11-case
# selftest. Reusing it keeps ONE definition of "releasable state" rather than
# two that drift apart.
python3 "$REPO/tools/release/check_build_config.py" "$CFG" \
  || die "configurations.xml is not in a releasable state (detail above).
  Nothing has been modified — this check runs before the linker flip, and
  cleanup() restores the file from a per-run backup regardless. Fix the
  selection in MPLAB X (or by hand) and re-run; do NOT 'git checkout' the
  file unless you also mean to discard your own edits to it."

# Locate the default-conf old_hv2_bootld.ld ex= attribute and flip it.
#
# Format-agnostic for the same reason as above: take ex= from the item's own
# line when it is there, else the first ex= that follows. Scoped to the
# <conf name="default"> block and bounded a few lines past the item so a
# malformed file cannot run away.
EXLN="$(awk '
  /<conf name="default"/ {indef=1}
  indef && /<\/conf>/     {indef=0}
  indef && found && NR > found + 4 {exit}
  indef && /old_hv2_bootld\.ld"/ {found=NR; if ($0 ~ /ex="/) {print NR; exit} next}
  indef && found && /ex="/ {print NR; exit}
' "$CFG")"
[ -n "$EXLN" ] || die "could not locate the default-conf old_hv2_bootld.ld ex= attribute in $CFG"
EXLINE="$(sed -n "${EXLN}p" "$CFG")"
# Plain expansion rather than `grep -q ... <(sed ...)`: process substitution
# needs /dev/fd, which is not guaranteed in every environment this might run in,
# and reading the line once means the check and the error message cannot
# disagree about what was actually there.
case "$EXLINE" in
  *'ex="true"'*) : ;;
  *) die "expected ex=\"true\" at line $EXLN (got: $EXLINE)" ;;
esac
sed -i "${EXLN}s/ex=\"true\"/ex=\"false\"/" "$CFG"
echo "  old_hv2_bootld.ld -> included (line $EXLN)"

# --- 2. regenerate makefiles (Windows-side) ---
say "Regenerating makefiles from the flipped config (Windows prjMakefilesGenerator)"
powershell.exe -Command "cd \"$(wslpath -w "$REPO/$PRJ")\"; & '$PRJGEN_WIN' -v ." 2>&1 | tail -2
grep -q "old_hv2_bootld" "$PRJ/nbproject/Makefile-default.mk" \
  || die "Makefile-default.mk does not reference old_hv2_bootld.ld after regen — the flip didn't take"

# --- 3. clean build ---
say "Clean-building the bootloader-linked hex"
( cd "$PRJ" && rm -rf build dist && "$MAKE" -f nbproject/Makefile-default.mk CONF=default build -j"$(nproc)" ) \
  >"$BUILD_LOG" 2>&1 \
  || { tail -20 "$BUILD_LOG"; die "build failed (see $BUILD_LOG)"; }
[ -f "$HEX" ] || die "hex not produced: $HEX"
echo "  built: $HEX"

# --- 4. HARD-VERIFY the bootloader-linked layout (the whole point of this script) ---
say "Verifying bootloader-linked layout"
grep -qiE 'kseg0_program_mem[[:space:]]+0x0*9d000480' "$MAP" \
  || die "kseg0_program_mem origin is NOT 0x9d000480 — this is a STANDALONE build, DO NOT SHIP. (.map: $(grep -i 'kseg0_program_mem 0x' "$MAP" | head -1))"
echo "  .map kseg0_program_mem origin = 0x9d000480 OK"

# --- #909: the application must fit inside the LOWER program-flash panel ---
#
# The bootloader now erases only the lower panel (0x1D000000-0x1D0FFFFF) instead
# of the whole PFM, so that an in-app customer update stops destroying the four
# NVM settings pages at 0x9D1E0000 (TopLevel, WiFi credentials, factory and user
# ADC calibration) -- see nvm.c:APP_FlashErase and FRM DS60001193B Register 52-1.
#
# That fix is only correct while the application FITS in the lower panel. If the
# app ever grows past 1 MB, the bootloader would erase only the part of it that
# lives below 0x1D100000 and then program the rest into flash it never erased:
# the update silently produces a corrupt image on a customer device. Nothing in
# the build warns about it, which is exactly why it is gated here.
#
# Checked two independent ways, both fail-closed:
#   (a) the .map's total kseg0_program_mem usage, and
#   (b) the placement of the actual hex records (in the python block below),
# because (a) alone would pass a build whose bytes merely ADD UP to under 1 MB
# while a section sits high in the address space.
#
# The block between the two BEGIN/END markers is extracted verbatim and executed
# by tools/release/selftest_release_map_size.py, so it can be proven to fire on
# an oversized .map without cutting a release. Keep the markers.
# >>>BEGIN #909 lower-panel size guard
USED_LINE="$(grep -iE 'Total[[:space:]]+kseg0_program_mem[[:space:]]+used' "$MAP" | head -1)"
[ -n "$USED_LINE" ] || die "could not find 'Total kseg0_program_mem used' in $MAP — the #909 lower-panel size guard cannot run, so this build is UNVERIFIED. DO NOT SHIP. (A toolchain change may have renamed the line; fix this check, do not delete it.)"
USED_HEX="$(printf '%s\n' "$USED_LINE" | grep -oiE '0x[0-9a-f]+' | head -1)"
[ -n "$USED_HEX" ] || die "could not parse a hex size out of the .map usage line — DO NOT SHIP. (line: $USED_LINE)"
USED=$(( USED_HEX ))
LOWER_PANEL_BYTES=$(( 0x100000 ))
if [ "$USED" -ge "$LOWER_PANEL_BYTES" ]; then
  die "application uses $USED bytes ($USED_HEX) of kseg0_program_mem, which reaches or exceeds the 0x100000 (1 MB) LOWER FLASH PANEL. The bootloader erases only the lower panel (#909), so an in-app update of this image would program unerased flash and brick the device. DO NOT SHIP. Either shrink the application or revisit the #909 erase strategy (page erase is the documented fallback). (.map line: $USED_LINE)"
fi
printf '  .map kseg0_program_mem used = %s (%d B) < 0x100000 lower panel OK (%d%% used)\n' \
  "$USED_HEX" "$USED" "$(( USED * 100 / LOWER_PANEL_BYTES ))"
# <<<END #909 lower-panel size guard

python3 - "$HEX" <<'PY' || die "hex layout verification failed — DO NOT SHIP"
import sys
spans=[]; base=0; saw_eof=False   # spans: (start, end_exclusive)
with open(sys.argv[1]) as f:
    for raw in f:
        line=raw.strip()
        if not line.startswith(':'): continue
        ln=int(line[1:3],16); off=int(line[3:7],16); rt=int(line[7:9],16)
        if rt==0x01:      # End Of File
            saw_eof=True; break
        elif rt==0x04:    # Extended Linear Address
            base=int(line[9:13],16)<<16
        elif rt==0x02:    # Extended Segment Address
            base=int(line[9:13],16)<<4
        elif rt==0x00:    # Data
            start=base+off; spans.append((start, start+ln))
ok=True
def chk(cond,msg):
    global ok
    print(("  OK  " if cond else "  FAIL")+" "+msg); ok=ok and cond
chk(saw_eof, "EOF record present")
if not spans:
    chk(False, "no data records found in hex"); sys.exit(1)
spans.sort()
lo=spans[0][0]
RESET_LO=0x1D000000; RESET_HI=0x1D000480
reset_bytes=0
for s,e in spans:
    # Count only the bytes inside [RESET_LO, RESET_HI) — intersection length,
    # so a record straddling the 0x1D000480 boundary is counted correctly.
    isect=min(e,RESET_HI)-max(s,RESET_LO)
    if isect>0: reset_bytes+=isect
# Rule 984830: a data record must START exactly at 0x1D000480 (the bulk origin).
# The lowest record start at/after RESET_HI must BE RESET_HI — a record that
# merely straddles the boundary (starts before, ends after) does NOT satisfy this.
bulk_starts=sorted(s for s,e in spans if s>=RESET_HI)
chk(lo==RESET_LO, f"lowest phys addr 0x{lo:08X} == 0x1D000000")
chk(reset_bytes==408, f"reset vector [0x1D000000,0x1D000480) = {reset_bytes} bytes == 408")
chk(bool(bulk_starts) and bulk_starts[0]==RESET_HI, f"bulk starts at 0x{(bulk_starts[0] if bulk_starts else 0):08X} == 0x1D000480")
# #764: NOTHING may live in boot flash. The bootloader can only write program
# flash 0x9D000000-0x9D1FFFFF, so any record at or above 0x1FC00000 is SILENTLY
# DROPPED by the in-field updater -- the hex verifies fine, the device accepts
# the update, and the dropped content simply never arrives. That is how a bench
# image and a fielded image diverge with nobody seeing it: the config words stay
# whatever the bootloader burned, permanently (erratum #45, RTSP of
# Configuration Words, no workaround).
#
# A bootloader-linked build has no boot-flash records at all, so this must be
# zero. If it is not, the hex is a standalone build wearing the right reset
# vector, and shipping it means shipping content the customer cannot receive.
BOOT_FLASH_LO=0x1FC00000
boot_records=[(a,b) for a,b in spans if b>BOOT_FLASH_LO]
boot_bytes=sum(b-max(a,BOOT_FLASH_LO) for a,b in boot_records)
boot_msg='no records at/above 0x1FC00000 (boot flash)'
if boot_records:
    # Report the first OFFENDING address, not the span start: a span may
    # straddle the boundary (start below 0x1FC00000, end above), and printing
    # its start contradicts the message and points at an address that is not
    # the problem.
    boot_msg += ' -- found %d span(s), %d byte(s), first offending addr 0x%08X' % (
        len(boot_records), boot_bytes, max(boot_records[0][0], BOOT_FLASH_LO))
chk(not boot_records, boot_msg)

# #909: EVERY record must land in the LOWER program-flash panel.
#
# The bootloader erases only 0x1D000000-0x1D0FFFFF now, so that a customer
# update stops wiping the NVM settings pages at 0x1D1E0000. Anything this hex
# places at or above 0x1D100000 would therefore be programmed into flash the
# bootloader never erased -- a silent corrupt-image update on a customer
# device. It is also the address range the settings pages themselves live in.
#
# This is the placement half of the guard; the .map usage half is in
# cut_release.sh above. A size check alone cannot catch a section that is small
# but sits high, and an address check alone cannot explain WHY a build is too
# big, so both run.
UPPER_PANEL_LO=0x1D100000
upper_records=[(a,b) for a,b in spans if b>UPPER_PANEL_LO]
upper_msg='no records at/above 0x1D100000 (upper flash panel — #909 erases only the lower panel)'
if upper_records:
    upper_bytes=sum(b-max(a,UPPER_PANEL_LO) for a,b in upper_records)
    upper_msg += ' -- found %d span(s), %d byte(s), first offending addr 0x%08X' % (
        len(upper_records), upper_bytes, max(upper_records[0][0], UPPER_PANEL_LO))
chk(not upper_records, upper_msg)
sys.exit(0 if ok else 1)
PY

# --- 5. package assets ---
say "Packaging assets"
ASSET_HEX="daqifi-nyquist-firmware-${VER}.hex"
ASSET_ZIP="daqifi-nyquist-firmware-${VER}.zip"
cp "$HEX" "$ASSET_HEX"
rm -f "$ASSET_ZIP"; zip -j "$ASSET_ZIP" "$ASSET_HEX" >/dev/null
echo "  $ASSET_HEX  ($(stat -c%s "$ASSET_HEX") bytes)"
echo "  $ASSET_ZIP  ($(stat -c%s "$ASSET_ZIP") bytes)"

# --- 6. hand off the irreversible step ---
cat <<EOF

================================================================================
Bootloader-linked v${VER} hex built, VERIFIED, and packaged. Next (manual):

  1. Ensure the release notes exist:
       docs/release-notes/RELEASE_NOTES_v${VER}.md

  2. Publish (irreversible — review first). PICK ONE; they are not
     interchangeable, and the flags are what decide who gets this build:

     (a) FULL release — the in-app updater WILL offer it to customers:
       gh release create v${VER} --repo daqifi/daqifi-nyquist-firmware --target main \\
         --title "v${VER} — <headline>" \\
         --notes-file docs/release-notes/RELEASE_NOTES_v${VER}.md --latest \\
         "${ASSET_HEX}" "${ASSET_ZIP}"

     (b) PRE-RELEASE / soak tier — kept OUT of the customer update path:
       gh release create v${VER} --repo daqifi/daqifi-nyquist-firmware --target release/v${VER} \\
         --prerelease \\
         --title "v${VER} (pre-release) — <headline>" \\
         --notes-file docs/release-notes/RELEASE_NOTES_v${VER}.md \\
         "${ASSET_HEX}" "${ASSET_ZIP}"

     Note --prerelease and --latest are mutually exclusive in intent: the
     updater picks the newest non-draft, NON-prerelease release carrying a
     .hex, so omitting --prerelease on a soak build silently ships it. Form
     (a) also targets main, which requires the version bump to have merged
     there; a soak tag usually targets its release branch (see #906).

  3. Verify the in-app updater will pick it up (newest non-draft/non-prerelease
     release that HAS a .hex asset):
       gh api repos/daqifi/daqifi-nyquist-firmware/releases \\
         --jq '.[] | select(.draft==false and .prerelease==false) |
               "\\(.tag_name) hex=\\([.assets[].name]|map(select(endswith(".hex")))|length>0)"' | head -3

The working tree (configurations.xml + makefiles) is restored on exit.
The two asset files are left in the repo root — delete them after publishing
(they are build artifacts, not tracked).
================================================================================
EOF
