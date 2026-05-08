#!/bin/bash
# Non-invasive: map Windows usbipd busid → /dev/ttyACMn via kernel state.
# Bench primary is Windows busid 2-4 (COM3). Optional 2nd arg overrides.
set -e
TARGET_BUSID="${1:-2-4}"

# Encode "2-4" -> "00020004" (vhci_hcd dev column format)
bus="${TARGET_BUSID%-*}"
port="${TARGET_BUSID#*-}"
dev_hex=$(printf "%04x%04x" "$bus" "$port")

# Find WSL local_busid (e.g., 1-2) for that Windows busid
local_busid=$(awk -v want="$dev_hex" 'NR>1 && $5==want {print $7}' /sys/devices/platform/vhci_hcd.0/status)

if [ -z "$local_busid" ]; then
  echo "ERROR: Windows busid $TARGET_BUSID not attached to WSL" >&2
  echo "Hint: powershell.exe -Command \"usbipd attach --wsl --busid $TARGET_BUSID\"" >&2
  exit 1
fi

# Find /dev/ttyACMn whose DEVPATH contains that local_busid
for dev in /dev/ttyACM*; do
  [ -e "$dev" ] || continue
  if udevadm info -q property -n "$dev" 2>/dev/null | grep -q "/usb1/${local_busid}/"; then
    echo "$dev"
    exit 0
  fi
done

echo "ERROR: no /dev/ttyACM* matched local_busid $local_busid" >&2
exit 2
