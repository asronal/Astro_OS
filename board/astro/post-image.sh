#!/bin/bash

set -e

BOARD_DIR="$(dirname "$0")"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"

install -m 0644 "${BOARD_DIR}/astro-hardware.txt" "${BINARIES_DIR}/rpi-firmware/"

# genimage needs a root path, but every partition here is a prebuilt image
# or empty, so give it an empty directory instead of a copy of TARGET_DIR.
trap 'rm -rf "${ROOTPATH_TMP}"' EXIT
ROOTPATH_TMP="$(mktemp -d)"

rm -rf "${GENIMAGE_TMP}"

genimage \
	--rootpath "${ROOTPATH_TMP}"   \
	--tmppath "${GENIMAGE_TMP}"    \
	--inputpath "${BINARIES_DIR}"  \
	--outputpath "${BINARIES_DIR}" \
	--config "${BOARD_DIR}/genimage-astro.cfg"

cd "${BINARIES_DIR}"

# genimage leaves the MBR boot code empty. When parted sees that, it wipes the
# sector and invents a new disk ID, so resizing /data on first boot would stop
# the root partition being found on the next boot. Any non-zero first byte
# prevents this. The Pi's firmware doesn't run this code.
printf '\372' | dd of=sdcard.img bs=1 count=1 conv=notrunc status=none

xz -T0 -f -k sdcard.img
sha256sum sdcard.img.xz > SHA256SUMS
