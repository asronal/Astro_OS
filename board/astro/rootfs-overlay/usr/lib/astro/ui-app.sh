#!/bin/sh
#
# Run the touch app inside Weston.
#

export QT_QPA_PLATFORM=wayland
export HOME=/home/astro

# Buttons and text are sized for a small touch screen, and would look tiny on a large
# monitor. The first mode listed for a connected screen is its preferred one.
for connector in /sys/class/drm/card*-*; do
	grep -qs '^connected' "${connector}/status" || continue
	width="$(sed -n '1s/x.*//p' "${connector}/modes")"
	if [ "${width:-0}" -ge 1900 ]; then
		export QT_SCALE_FACTOR=1.5
	fi
	break
done

exec /usr/bin/astro-ui
