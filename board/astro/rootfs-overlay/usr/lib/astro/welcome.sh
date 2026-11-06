# The short welcome shown after login. /etc/profile runs this inside the login
# shell, so it must not exit or leave variables behind.

astro_welcome() {
	local up data network

	# Serial logins have already seen the login screen, but SSH logins haven't
	if [ -n "${SSH_CONNECTION:-}" ]; then
		cat /etc/issue
	fi

	up="$(uptime | sed -e 's/^.*up *//' -e 's/, *load.*$//' -e 's/, *[0-9]* users*.*$//')"
	data="$(df -h /data 2> /dev/null | awk 'NR == 2 { print $4 }')"
	network="$(ip -o -4 addr show 2> /dev/null | awk '$2 != "lo" { sub(/\/.*/, "", $4); print $4; exit }')"

	printf '\n    \033[38;5;246mUp %s, %s free, %s\033[0m\n' "${up}" "${data:-no storage}" "${network:-not connected}"
	printf '    \033[38;5;246mType\033[0m \033[38;5;179mastro_sky\033[0m \033[38;5;246mto explore the sky\033[0m\n\n'
}

case "$-" in
*i*)
	# A serial line can't report its size by itself, so ask the terminal
	if [ "$(stty size 2> /dev/null)" = "0 0" ]; then
		resize > /dev/null 2>&1
	fi
	astro_welcome
	;;
esac
unset -f astro_welcome
