#!/bin/sh
#
# Boot the whole Astro OS image on an emulated Raspberry Pi 4, in this terminal.
# It uses a copy of the image, so changes are lost each time unless --keep is used.
# Needs a finished build, and:
#   sudo apt install --no-install-recommends qemu-system-arm device-tree-compiler
#
# Usage: tools/try-os.sh [--keep]
#
# Log in as root, password root (development images only).
# To quit, press Ctrl+A and then X.
#
# QEMU's raspi4b machine has no working network or graphics for this image, so
# this checks booting and services, not Wi-Fi, SSH, the GPU or the touch screen.
#

set -eu

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${REPO_DIR}/../../build/pi4}"
BUILD_DIR="$(cd "${BUILD_DIR}" && pwd)"
IMAGES="${BUILD_DIR}/images"
DISK="${BUILD_DIR}/try-os/disk.img"
DISK_SIZE="4G"

if [ ! -f "${IMAGES}/sdcard.img" ]; then
	echo "ERROR: no ${IMAGES}/sdcard.img. Finish the build first." >&2
	exit 1
fi
if ! command -v qemu-system-aarch64 > /dev/null; then
	echo "ERROR: run: sudo apt install --no-install-recommends qemu-system-arm" >&2
	exit 1
fi
if ! command -v fdtoverlay > /dev/null; then
	echo "ERROR: run: sudo apt install device-tree-compiler" >&2
	exit 1
fi

mkdir -p "$(dirname "${DISK}")"
if [ "${1:-}" != "--keep" ] || [ ! -f "${DISK}" ]; then
	cp "${IMAGES}/sdcard.img" "${DISK}"
	# QEMU's emulated SD card must be a power-of-two size
	truncate -s "${DISK_SIZE}" "${DISK}"
fi

# A real Pi applies this overlay from config.txt, freeing the serial port from
# Bluetooth for the console. QEMU doesn't read config.txt, so apply it here.
DTB="${BUILD_DIR}/try-os/bcm2711-rpi-4-b.dtb"
fdtoverlay -i "${IMAGES}/bcm2711-rpi-4-b.dtb" -o "${DTB}" "${IMAGES}/rpi-firmware/overlays/miniuart-bt.dtbo"

echo "Booting. This can take a few minutes. To quit, press Ctrl+A and then X."
exec qemu-system-aarch64 \
	-M raspi4b \
	-kernel "${IMAGES}/Image" \
	-dtb "${DTB}" \
	-append "root=PARTUUID=41535452-02 rootwait console=ttyAMA0,115200 earlycon=pl011,mmio32,0xfe201000 panic=5" \
	-drive "file=${DISK},if=sd,format=raw" \
	-nographic
