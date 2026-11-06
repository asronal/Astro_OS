#!/bin/sh
#
# Keep the touch app running: on a display if there is one, otherwise over VNC
# on development images.
#

ALLOW_VNC="no"
[ -f /etc/astro-ui.conf ] && . /etc/astro-ui.conf

export XDG_RUNTIME_DIR="/run/astro-ui"
export LIBSEAT_BACKEND="seatd"
export HOME="/home/astro"

display_connected() {
	grep -qs '^connected' /sys/class/drm/card*-*/status
}

# Display drivers load after this starts, so give them time to appear
retries=15
while ! display_connected && [ "${retries}" -gt 0 ]; do
	sleep 1
	retries=$((retries - 1))
done

while true; do
	if display_connected; then
		weston --continue-without-input
	elif [ "${ALLOW_VNC}" = "yes" ]; then
		QT_QPA_PLATFORM="vnc:size=800x480" QT_QUICK_BACKEND=software /usr/bin/astro-ui
	fi
	sleep 3
done
