#!/bin/sh
#
# Run the real Astro OS app on this PC, using the ARM programs from the build
# with QEMU, and INDI's telescope simulator as the mount. Needs a finished build.
#
# Usage: tools/try-app.sh [--vnc]
#   Default: opens a window, using WSLg or any Wayland desktop
#   --vnc:   serves it on localhost:5900 for a VNC viewer
#

set -eu

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${REPO_DIR}/../../build/pi4}"
BUILD_DIR="$(cd "${BUILD_DIR}" && pwd)"
TARGET="${BUILD_DIR}/target"
QEMU="${BUILD_DIR}/host/bin/qemu-aarch64"
STATE_DIR="${BUILD_DIR}/try-app"
INDI_PORT="7624"

if [ ! -x "${QEMU}" ] || [ ! -x "${TARGET}/usr/bin/astro-ui" ]; then
	echo "ERROR: ${BUILD_DIR} has no finished build, with QEMU and astro-ui" >&2
	exit 1
fi

mkdir -p "${STATE_DIR}/config" "${STATE_DIR}/drivers" "${STATE_DIR}/home"

# INDI starts its drivers by name, so give it one that runs under QEMU
cat > "${STATE_DIR}/drivers/indi_simulator_telescope" <<WRAPPER
#!/bin/sh
exec "${QEMU}" -L "${TARGET}" "${TARGET}/usr/bin/indi_simulator_telescope" "\$@"
WRAPPER
chmod +x "${STATE_DIR}/drivers/indi_simulator_telescope"

export HOME="${STATE_DIR}/home"
export QT_QUICK_BACKEND=software
export QT_PLUGIN_PATH="${TARGET}/usr/lib/qt6/plugins"
export QML_IMPORT_PATH="${TARGET}/usr/lib/qt6/qml"
export QML2_IMPORT_PATH="${TARGET}/usr/lib/qt6/qml"
export FONTCONFIG_PATH="${TARGET}/etc/fonts"
export ASTRO_CONFIG_DIR="${STATE_DIR}/config"
export ASTRO_WINDOWED=1

if [ "${1:-}" = "--vnc" ]; then
	export QT_QPA_PLATFORM="vnc:size=800x480"
	echo "Connect a VNC viewer to localhost:5900"
else
	if [ -S /mnt/wslg/runtime-dir/wayland-0 ]; then
		export XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir
		export WAYLAND_DISPLAY=wayland-0
	fi
	export QT_QPA_PLATFORM=wayland
fi

"${QEMU}" -L "${TARGET}" -E PATH="${STATE_DIR}/drivers:/usr/bin:/bin" \
	"${TARGET}/usr/bin/indiserver" -p "${INDI_PORT}" -r 0 indi_simulator_telescope \
	> "${STATE_DIR}/indiserver.log" 2>&1 &
INDI_PID=$!
trap 'kill "${INDI_PID}" 2> /dev/null || true; pkill -f "${TARGET}/usr/bin/indi_simulator_telescope" || true' EXIT

echo "Starting the app. It is slower than on a Pi because every instruction is emulated."
echo "Press Ctrl+C here to quit."
"${QEMU}" -L "${TARGET}" "${TARGET}/usr/bin/astro-ui"
