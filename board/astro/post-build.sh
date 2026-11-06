#!/bin/sh

set -eu

TARGET_DIR="$1"
BOARD_DIR="$(dirname "$0")"
VERSION="$(cat "${BOARD_DIR}/../../VERSION")"
BUILDROOT_VERSION="$(sed -n 's/^# Buildroot \(.*\) Configuration$/\1/p' "${BR2_CONFIG}")"
SSH_KEY="${BOARD_DIR}/local/authorized_keys"

# VERSION is the only place the release number is stored
rm -f "${TARGET_DIR}/etc/os-release"
cat > "${TARGET_DIR}/usr/lib/os-release" <<OS_RELEASE
NAME="Astro OS"
PRETTY_NAME="Astro OS ${VERSION}"
ID=astro
VERSION="${VERSION}"
VERSION_ID="${VERSION}"
HOME_URL="https://github.com/asronal/Astro_OS"
OS_RELEASE
ln -sf ../usr/lib/os-release "${TARGET_DIR}/etc/os-release"

# The screen shown before login, on the serial console and any display.
# tools/make-login-art.py draws it.
sed "s/@VERSION@/${VERSION}/" "${BOARD_DIR}/issue.in" > "${TARGET_DIR}/etc/issue"

# The welcome after login is live, so see usr/lib/astro/welcome.sh
rm -f "${TARGET_DIR}/etc/motd"
echo "${VERSION}" > "${TARGET_DIR}/usr/lib/astro/version"

mkdir -p "${TARGET_DIR}/boot" "${TARGET_DIR}/data"

# Remote screen over VNC has no real protection, so development images only
if grep -q '^# BR2_TARGET_ENABLE_ROOT_LOGIN is not set' "${BR2_CONFIG}"; then
	echo 'ALLOW_VNC="no"' > "${TARGET_DIR}/etc/astro-ui.conf"
else
	echo 'ALLOW_VNC="yes"' > "${TARGET_DIR}/etc/astro-ui.conf"
fi

# Allow console logins on the USB-C serial port
if [ -f "${TARGET_DIR}/etc/securetty" ] && ! grep -qx ttyGS0 "${TARGET_DIR}/etc/securetty"; then
	echo ttyGS0 >> "${TARGET_DIR}/etc/securetty"
fi

# A release image has no root password, so it needs a key to be reachable
if grep -q '^# BR2_TARGET_ENABLE_ROOT_LOGIN is not set' "${BR2_CONFIG}" && [ ! -s "${SSH_KEY}" ]; then
	echo "ERROR: release images need an SSH key in ${SSH_KEY}" >&2
	exit 1
fi

install -d -m 0700 "${TARGET_DIR}/home/astro/.ssh"
if [ -s "${SSH_KEY}" ]; then
	install -m 0600 "${SSH_KEY}" "${TARGET_DIR}/home/astro/.ssh/authorized_keys"
else
	rm -f "${TARGET_DIR}/home/astro/.ssh/authorized_keys"
	echo "WARNING: no ${SSH_KEY}, so SSH login as astro will not work" >&2
fi

mkdir -p "${TARGET_DIR}/etc/sudoers.d"
# Read-only once created, so remove it first or rebuilds fail
rm -f "${TARGET_DIR}/etc/sudoers.d/astro"
echo 'astro ALL=(ALL) NOPASSWD: ALL' > "${TARGET_DIR}/etc/sudoers.d/astro"
chmod 0440 "${TARGET_DIR}/etc/sudoers.d/astro"
