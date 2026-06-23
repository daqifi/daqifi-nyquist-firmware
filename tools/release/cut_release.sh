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
    --version) WANT_VER="$2"; shift 2 ;;
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
[ -z "$(git status --porcelain --untracked-files=no)" ] || echo "WARNING: working tree has tracked changes; configurations.xml will be restored from git at the end."

# --- 1. flip the default conf to bootloader-linked (include old_hv2_bootld.ld, keep p32MZ excluded) ---
say "Flipping default config to bootloader-linked (old_hv2_bootld.ld included)"
cp "$CFG" "/tmp/configurations.xml.release.bak"
# Find old_hv2_bootld.ld inside the <conf name="default"> block and flip the ex=
# flag on the line that follows it. Robust to line moves: locate by block, not
# hardcoded line numbers.
LN="$(awk '
  /<conf name="default"/ {indef=1}
  indef && /<conf name="Nq/ {indef=0}
  indef && /old_hv2_bootld\.ld"/ {print NR; exit}
' "$CFG")"
[ -n "$LN" ] || die "could not locate default-conf old_hv2_bootld.ld item in $CFG"
EXLN=$((LN + 1))
grep -q 'ex="true"' <(sed -n "${EXLN}p" "$CFG") || die "expected ex=\"true\" at line $EXLN (got: $(sed -n "${EXLN}p" "$CFG"))"
sed -i "${EXLN}s/ex=\"true\"/ex=\"false\"/" "$CFG"
echo "  old_hv2_bootld.ld -> included (line $EXLN)"

# Always restore the working tree on exit, even on failure.
cleanup() {
  say "Restoring configurations.xml (standalone default)"
  git checkout -- "$CFG" 2>/dev/null || cp "/tmp/configurations.xml.release.bak" "$CFG"
  if [ "$KEEP_BL_MK" -eq 0 ]; then
    echo "  regenerating makefiles back to standalone (bench default)…"
    ( cd "$(wslpath -w "$PRJ")" >/dev/null 2>&1 || true
      powershell.exe -Command "cd \"$(wslpath -w "$REPO/$PRJ")\"; & '$PRJGEN_WIN' -v ." ) >/dev/null 2>&1 \
      && echo "  standalone makefiles restored" \
      || echo "  WARNING: regen-back failed — run the regen manually before the next bench build"
  else
    echo "  --keep-bootloader-makefiles: on-disk makefiles remain bootloader-linked (regen before bench flashing)"
  fi
}
trap cleanup EXIT

# --- 2. regenerate makefiles (Windows-side) ---
say "Regenerating makefiles from the flipped config (Windows prjMakefilesGenerator)"
powershell.exe -Command "cd \"$(wslpath -w "$REPO/$PRJ")\"; & '$PRJGEN_WIN' -v ." 2>&1 | tail -2
grep -q "old_hv2_bootld" "$PRJ/nbproject/Makefile-default.mk" \
  || die "Makefile-default.mk does not reference old_hv2_bootld.ld after regen — the flip didn't take"

# --- 3. clean build ---
say "Clean-building the bootloader-linked hex"
( cd "$PRJ" && rm -rf build dist && "$MAKE" -f nbproject/Makefile-default.mk CONF=default build -j"$(nproc)" ) \
  >/tmp/cut_release_build.log 2>&1 \
  || { tail -20 /tmp/cut_release_build.log; die "build failed (see /tmp/cut_release_build.log)"; }
[ -f "$HEX" ] || die "hex not produced: $HEX"
echo "  built: $HEX"

# --- 4. HARD-VERIFY the bootloader-linked layout (the whole point of this script) ---
say "Verifying bootloader-linked layout"
grep -qiE 'kseg0_program_mem[[:space:]]+0x000000009d000480' "$MAP" \
  || die "kseg0_program_mem origin is NOT 0x9d000480 — this is a STANDALONE build, DO NOT SHIP. (.map: $(grep -i 'kseg0_program_mem 0x' "$MAP" | head -1))"
echo "  .map kseg0_program_mem origin = 0x9d000480 OK"
python3 - "$HEX" <<'PY' || die "hex layout verification failed — DO NOT SHIP"
import sys
addrs=[]; ext=0
with open(sys.argv[1]) as f:
    for line in f:
        line=line.strip()
        if not line.startswith(':'): continue
        ln=int(line[1:3],16); off=int(line[3:7],16); rt=int(line[7:9],16)
        if rt==0x04: ext=int(line[9:13],16)<<16
        elif rt==0x00: addrs.append((ext+off, ln))
addrs.sort()
lo=addrs[0][0]
reset_bytes=sum(ln for a,ln in addrs if 0x1D000000<=a<0x1D000480)
bulk=[a for a,ln in addrs if a>=0x1D000480]
ok=True
def chk(cond,msg):
    global ok
    print(("  OK  " if cond else "  FAIL")+" "+msg); ok=ok and cond
chk(lo==0x1D000000, f"lowest phys addr 0x{lo:08X} == 0x1D000000")
chk(380<=reset_bytes<=440, f"reset vector [0x1D000000,0x1D000480) = {reset_bytes} bytes (~408)")
chk(bool(bulk) and min(bulk)==0x1D000480, f"bulk starts at 0x{(min(bulk) if bulk else 0):08X} == 0x1D000480")
sys.exit(0 if ok else 1)
PY

# --- 5. package assets ---
say "Packaging assets"
ASSET_HEX="daqifi-nyquist-firmware-${VER}.hex"
ASSET_ZIP="daqifi-nyquist-firmware-${VER}.zip"
cp "$HEX" "$ASSET_HEX"
rm -f "$ASSET_ZIP"; zip -j "$ASSET_ZIP" "$HEX" >/dev/null
echo "  $ASSET_HEX  ($(stat -c%s "$ASSET_HEX") bytes)"
echo "  $ASSET_ZIP  ($(stat -c%s "$ASSET_ZIP") bytes)"

# --- 6. hand off the irreversible step ---
cat <<EOF

================================================================================
Bootloader-linked v${VER} hex built, VERIFIED, and packaged. Next (manual):

  1. Ensure the release notes exist:
       docs/release-notes/RELEASE_NOTES_v${VER}.md

  2. Publish (irreversible — review first):
       gh release create v${VER} --repo daqifi/daqifi-nyquist-firmware --target main \\
         --title "v${VER} — <headline>" \\
         --notes-file docs/release-notes/RELEASE_NOTES_v${VER}.md --latest \\
         "${ASSET_HEX}" "${ASSET_ZIP}"

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
