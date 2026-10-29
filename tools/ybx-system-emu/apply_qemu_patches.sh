#!/bin/sh
# Apply tested ESP32-C3 GDMA fix to the SEPARATE espressif/qemu checkout.
# Never changes FreeInkSDK or production YBX Reader firmware.
set -eu
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
QEMU_ROOT="$HOME/.local/state/ybx-emulator-upstream/qemu"
if [ "$#" -gt 0 ]; then QEMU_ROOT="$1"; fi
PATCH="$HERE/patches/0001-esp32c3-gdma-interrupt-layout.patch"
test -d "$QEMU_ROOT/.git" || { echo "Not a QEMU Git checkout: $QEMU_ROOT" >&2; exit 2; }
test -s "$PATCH" || { echo "Missing patch" >&2; exit 2; }

if git -C "$QEMU_ROOT" apply --reverse --check "$PATCH" 2>/dev/null; then
  echo "GDMA patch already applied"
elif git -C "$QEMU_ROOT" apply --check "$PATCH"; then
  git -C "$QEMU_ROOT" apply "$PATCH"
  echo "Applied ESP32-C3 GDMA patch"
else
  echo "Cannot apply GDMA patch: upstream checkout differs; no changes made" >&2
  exit 3
fi
echo "Rebuild qemu-system-riscv32 with Docker from patched checkout."
