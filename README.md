# Astro OS

Astro OS is an experimental embedded Linux project built with Buildroot and targeted initially at the Raspberry Pi 4.

## Current Release — v0.3.0

**Astro OS v0.3.0 — Graphical Interface**

This release adds the graphics stack required for a Wayland-based graphical environment on the Raspberry Pi 4.

### Target

- Raspberry Pi 4
- AArch64
- Buildroot 2026.05.2
- Linux 6.12.61
- glibc
- BusyBox
- ext4 root filesystem

### Graphics Stack

- Mesa3D
- Wayland
- Weston
- DRM compositor
- Raspberry Pi VC4 graphics driver
- Raspberry Pi V3D graphics driver
- Gallium V3D support

### Networking & System Services

- Ethernet support
- Wi-Fi support
- SSH support
- wpa_supplicant with WPA3 support
- iw
- iproute2
- ethtool
- dynamic device creation with eudev

### Build Configuration

The Buildroot configuration used for the Raspberry Pi 4 build is:

`config/pi4_defconfig`

### Build Environment

Buildroot source:

`buildroot-2026.05.2`

External toolchain:

- AArch64
- GCC 14.3.0
- glibc
- Bootlin 2025.08-1

## Release Image

The bootable Raspberry Pi 4 SD-card image is available from the [v0.3.0 GitHub release](https://github.com/asronal/Astro_OS/releases/tag/v0.3.0).

Release assets:

- `sdcard.img.xz`
- `SHA256SUMS`

SHA256 for `sdcard.img.xz`:

```text
3a8a1fbbc0771b040739381d67b36ccf0f391729cae3d48c1d2fca9d91a1982e
```

## Milestones

### v0.1.0 — First Bootable Image

- Buildroot setup
- External AArch64 toolchain
- Raspberry Pi 4 configuration
- Linux kernel
- Root filesystem
- Bootable SD-card image

### v0.2.0 — Networking

- Ethernet support
- Wi-Fi support
- SSH support

### v0.3.0 — Graphical Interface

- Mesa3D
- Wayland
- Weston
- DRM compositor
- Raspberry Pi VC4/V3D graphics support

## Status

- [x] Buildroot setup
- [x] External AArch64 toolchain
- [x] Raspberry Pi 4 configuration
- [x] Linux kernel
- [x] Root filesystem
- [x] Bootable SD-card image
- [x] Ethernet support
- [x] Wi-Fi support
- [x] SSH support
- [x] Mesa3D
- [x] Wayland
- [x] Weston
- [x] DRM / VC4 / V3D configuration
- [ ] Hardware validation of the graphical interface
- [ ] Additional boards

## Repository Structure

```text
Astro_OS/
├── board/                  # Raspberry Pi board-specific files
├── config/                 # Buildroot defconfig files
├── package/                # Custom Buildroot packages
├── output/                 # Local build output (not tracked)
├── VERSION                 # Current Astro OS version
└── README.md
```

## License

To be determined.
