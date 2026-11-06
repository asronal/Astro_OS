#!/bin/sh
#
# Run the touch app inside Weston.
#

export QT_QPA_PLATFORM=wayland
export HOME=/home/astro
exec /usr/bin/astro-ui
