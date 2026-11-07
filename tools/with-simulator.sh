#!/bin/bash
# Runs an ARM test program on this PC with a fresh INDI telescope simulator behind it,
# as the mount tests need one that starts out disconnected. Emulation, not hardware.
#
#   tools/with-simulator.sh /tmp/uitest/mount_e2e
#   tools/with-simulator.sh /tmp/uitest/ui_e2e
set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-/home/asron/el/build/pi4}"
TARGET="${BUILD_DIR}/target"
QEMU="${BUILD_DIR}/host/bin/qemu-aarch64"
INDI_PORT=7624

if [ $# -lt 1 ]; then
	echo "usage: $0 PROGRAM [ARGUMENTS]" >&2
	exit 2
fi
if ss -ltn | grep -q ":${INDI_PORT} "; then
	echo "Something is already using port ${INDI_PORT}, such as try-app.sh" >&2
	exit 2
fi

STATE_DIR="$(mktemp -d)"
mkdir -p "${STATE_DIR}/drivers"
cat > "${STATE_DIR}/drivers/indi_simulator_telescope" <<WRAPPER
#!/bin/sh
echo \$\$ > "${STATE_DIR}/driver.pid"
exec "${QEMU}" -L "${TARGET}" "${TARGET}/usr/bin/indi_simulator_telescope" "\$@"
WRAPPER
chmod +x "${STATE_DIR}/drivers/indi_simulator_telescope"

export HOME="${STATE_DIR}"
export PATH="${STATE_DIR}/drivers:${PATH}"
"${QEMU}" -L "${TARGET}" "${TARGET}/usr/bin/indiserver" -p "${INDI_PORT}" -r 0 indi_simulator_telescope \
	> "${STATE_DIR}/indiserver.log" 2>&1 &
SERVER=$!

stop_simulator() {
	kill "${SERVER}" 2> /dev/null
	[ -f "${STATE_DIR}/driver.pid" ] && kill "$(cat "${STATE_DIR}/driver.pid")" 2> /dev/null
	rm -rf "${STATE_DIR}"
}
trap stop_simulator EXIT

export QT_QPA_PLATFORM=offscreen
export QT_QUICK_BACKEND=software
export QT_PLUGIN_PATH="${TARGET}/usr/lib/qt6/plugins"
export QML_IMPORT_PATH="${TARGET}/usr/lib/qt6/qml"
export QML2_IMPORT_PATH="${TARGET}/usr/lib/qt6/qml"
export FONTCONFIG_PATH="${TARGET}/etc/fonts"
export ASTRO_CATALOG_DIR="${ASTRO_CATALOG_DIR:-${REPO}/package/astro-ui/data}"

"${QEMU}" -L "${TARGET}" "$@"
