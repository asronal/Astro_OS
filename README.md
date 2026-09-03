# Astro OS

Astro OS is an experimental embedded Linux distribution built with Buildroot, currently targeting the Raspberry Pi 4.

## Current Development Release

**Astro OS v0.5.0**

v0.5.0 adds a dedicated non-root `astro` user while retaining the networking, Wi-Fi, SSH, and graphical stack developed in previous milestones.

> Hardware validation is still pending.

## Features

### v0.1.0 — First Raspberry Pi 4 Build

- Initial Buildroot setup
- Raspberry Pi 4 AArch64 target
- External AArch64 toolchain
- Linux kernel
- BusyBox
- ext4 root filesystem
- Bootable SD-card image

### v0.2.0 — Networking

- Ethernet networking
- DHCP using BusyBox `udhcpc`
- `iproute2`
- `ethtool`
- OpenSSH server
- OpenSSH client
- SSH key utilities
- Wi-Fi support
- `wpa_supplicant`
- `wpa_cli`
- WPA3 support
- `iw`

### v0.3.0 — Graphics

- Mesa3D
- Wayland
- Weston
- Weston DRM compositor
- Raspberry Pi VC4 support
- Raspberry Pi V3D support
- DRM/KMS graphics stack

### v0.4.0 — System Userspace

- Astro OS `/etc/os-release`
- Astro OS version identification
- Root filesystem overlay support
- Buildroot external-tree structure
- Portable root filesystem customization

### v0.5.0 — Non-root User

- Dedicated `astro` user
- UID/GID `1000`
- `/home/astro`
- `/bin/sh` login shell
- Buildroot users-table support
- Existing networking, Wi-Fi, SSH and graphics functionality retained

## Target

- Raspberry Pi 4
- AArch64
- Linux 6.12.61
- Buildroot 2026.05.2
- glibc
- BusyBox
- ext4 root filesystem

## Build Environment

### Buildroot

```text
buildroot-2026.05.2
