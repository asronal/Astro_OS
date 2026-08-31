# Astro OS

Astro OS is an experimental embedded Linux project built with Buildroot.

## Milestone 0.1.0

First successful Raspberry Pi 4 build.

### Target

- Raspberry Pi 4
- AArch64
- Buildroot 2026.05.2
- glibc
- BusyBox
- ext4 root filesystem

### Build Configuration

The Buildroot configuration used for this milestone is:

`config/pi4_defconfig`

### Build Environment

Buildroot source:

`buildroot-2026.05.2`

External toolchain:

- AArch64
- GCC 14.3.0
- glibc
- Bootlin 2025.08-1

### Release Image

The bootable Raspberry Pi 4 SD-card image is provided through the GitHub Releases section.

The release contains:

- `astro-os-pi4-v0.1.0.img.xz`
- `SHA256SUMS`

### Status

- [x] Buildroot setup
- [x] External AArch64 toolchain
- [x] Raspberry Pi 4 configuration
- [x] Linux kernel
- [x] Root filesystem
- [x] Bootable SD-card image
- [ ] Networking
- [ ] SSH
- [ ] Graphical interface
- [ ] Additional boards

## License

To be determined.# Astro_OS
