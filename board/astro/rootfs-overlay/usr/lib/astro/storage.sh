# Work out which disk Astro OS booted from, so that nothing else is ever
# touched. Sourced by the init scripts. ASTRO_DISK is left empty on failure.
#
# Astro OS image layout: 1 = boot, 2 = root, 3 = data.

ASTRO_DISK=""
ASTRO_BOOT_PART=""
ASTRO_DATA_PART=""
ASTRO_DATA_PART_NUM="3"

astro_find_storage() {
	# e.g. /sys/devices/.../block/sda/sda2 or .../block/mmcblk0/mmcblk0p2
	disk_name="$(basename "$(dirname "$1")")"

	case "${disk_name}" in
		block|.|"")
			return 1;;
		*[0-9])
			part_prefix="${disk_name}p";;
		*)
			part_prefix="${disk_name}";;
	esac

	ASTRO_DISK="/dev/${disk_name}"
	ASTRO_BOOT_PART="/dev/${part_prefix}1"
	ASTRO_DATA_PART="/dev/${part_prefix}${ASTRO_DATA_PART_NUM}"
}

astro_find_storage "$(readlink -f "/sys/dev/block/$(mountpoint -d /)" 2> /dev/null)"
