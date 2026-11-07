# Astro OS

Astro OS is an experimental embedded Linux distribution built with Buildroot, currently targeting the Raspberry Pi 4.

## Current Release

**Astro OS v0.8.0** (pre-release)

Astro OS is becoming a self-contained field computer for astronomy and astrophotography, with a touchscreen, telescope and camera control, and offline sky data. v0.8.0 builds the base for that: an image designed to boot and be reachable on a real Raspberry Pi 4.

> **Nothing has been tested on a physical Raspberry Pi 4 yet.** Features below describe what is built into the image, not what is proven to work. See [Hardware Status](#hardware-status).

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
- `/bin/sh`
- Buildroot users-table support

### v0.6.0 — Diagnostics

- Custom `astro-info` Buildroot package
- Astro OS system information utility
- AArch64 target executable
- Kernel version reporting
- Architecture reporting
- Hostname reporting

### v0.7.0 — System Usability

- Astro OS `/etc/profile`
- Custom shell environment
- `nano` as the default editor
- Shell history configuration
- Custom Astro OS MOTD
- Version and platform information displayed in the login environment
- Existing networking, Wi-Fi, SSH and graphics functionality retained

### v0.8.0 — Base System

- Portable build: Bootlin toolchain downloaded by Buildroot, with no machine-specific paths
- Wi-Fi firmware and regulatory database
- SSH login as `astro` using your own key, with `sudo`
- Separate release configuration with root login locked
- Boot configuration that enables the GPU driver and detects DSI displays and CSI cameras
- `/data` partition that grows to fill the boot drive on first boot
- Booting from a USB drive, leaving any OS on the SD card untouched
- USB-C connection to a PC: network with SSH, plus a serial console
- Network time with `chrony`, CA certificates, filesystem tools
- Version number generated from a single `VERSION` file

## Testing

Without a Pi, using QEMU:

- Boots twice in a row on an emulated Pi 4, with `/data` growing and services starting.
- Sky calculations match NASA JPL Horizons, running as ARM programs (179 checks).
- The mount code passes 28 checks against INDI's telescope simulator, including GoTo, STOP, Sync and the Sun check.
- The interface was driven over VNC through connecting, selecting a target, GoTo and tracking.

## Hardware Status

| Item | Status |
|---|---|
| Boot on Raspberry Pi 4 | Untested |
| Ethernet | Untested |
| Wi-Fi | Untested |
| SSH login | Untested |
| USB-C network and serial | Untested |
| GPU driver (`/dev/dri`) | Untested |
| `/data` growing to fill the boot drive | Untested |
| Booting from a USB drive | Untested |
| Touch displays (HDMI and DSI) | Untested |
| Real mounts, cameras and RC servos | Untested |

### In Development — Astronomy System

**Touch app**

- Tonight: Moon phase, dark sky times, best targets, and the Sun, Moon and planets
- Sky Chart: about 83,000 stars and 895 deep-sky objects at their real sizes, with star colours, constellations, the Milky Way, the Moon's real phase, daylight and dusk, plus pan, pinch and search
- Tap an object for its details: when it rises, sets and is best seen, its size, phase and distance, and whether GoTo would allow it. Tap again for the next object there
- Time preview: see the sky up to 30 days ahead or behind. This only changes what the chart shows, and Sync and GoTo stay off until you return to now
- Eyepiece rings: enter your telescope and eyepieces to see how much sky each one takes in. The size is the usual estimate, apparent field divided by magnification
- Mount: choice of 10 mount types through INDI, GoTo, Sync, tracking, parking and manual moves
- Tools: field of view, image scale, exposure limits and a polar alignment dial
- Red night mode, and a STOP button on every screen
- GoTo refuses to point near the Sun or below the horizon, or at all while the clock isn't set
- Finds the location and time zone from the internet address, using ipapi.co, unless you enter your own. The clock sets itself over the internet, or by hand when offline

**`astro_sky` shell**

Type `astro_sky` at a terminal for an interactive shell in the style of tools such as `dc_shell`. It runs Tcl, so scripts can use variables, loops and procedures, and it shares its settings and GoTo safety checks with the touch app. Type `help` for the commands.

```tcl
astro_sky> report_tonight
astro_sky> foreach body [get_planets] { puts "$body [dict get [get_object $body] alt]" }
astro_sky> if {[dict get [get_object M31] alt] > 30} { goto_object M31 }
```

Scripts can also run without the prompt: `astro_sky -f my_script.tcl`, or `astro_sky -x "report_tonight; exit"`.

**Hardware drivers, packaged but not yet tested on real hardware**

- INDI 2.2.5, with its LX200 (including OnStep), Celestron, SynScan and iOptron drivers
- Sky-Watcher direct control (EQMod), and DSLR and mirrorless cameras (gphoto)
- A custom driver for RC servos on a PCA9685 board, for pointing and visual use only

**Not yet in the app:** camera control. Also still to do: the Pi camera driver, read-only system, Wi-Fi hotspot and watchdog.


**Sky calculations**

- `astro-core` library and `astro-sky` command-line tool
- Sun, Moon and planet position, altitude, distance, magnitude, phase, and rise, transit and set times
- Twilight times and best targets tonight
- Checked against NASA JPL Horizons on the build PC: positions within 1 arcminute, rise and set within 3 minutes for the Sun and Jupiter, and within 5 minutes for the Moon

## Trying It Without a Pi

After a build, you can try Astro OS on your PC:

```sh
tools/try-app.sh          # The touch app in a window, with a simulated telescope
tools/try-app.sh --vnc    # The same, for a VNC viewer on localhost:5900
tools/try-os.sh           # Boot the whole OS on an emulated Pi 4, in the terminal
```

`try-os.sh` checks booting and services. It has no network, GPU or touch screen, so those still need a real Pi.

## Target

- Raspberry Pi 4
- AArch64
- Linux 6.12.61
- Buildroot 2026.05.2
- glibc
- BusyBox
- ext4 root filesystem

## Building

Lay out a workspace with Buildroot 2026.05.2 in `buildroot-2026.05.2/` and this repository in `projects/pi4/`, then from `projects/pi4/`:

```sh
# Optional: lets you log in over SSH as astro. This file is never committed.
mkdir -p board/astro/local
cp ~/.ssh/id_ed25519.pub board/astro/local/authorized_keys

cd ../../buildroot-2026.05.2
make BR2_EXTERNAL=../projects/pi4 O=../build/pi4 pi4_defconfig
make O=../build/pi4
```

The image is written to `build/pi4/images/sdcard.img.xz` in the workspace.

- **Windows/WSL:** Buildroot rejects a `PATH` containing spaces, which WSL adds by default. Prefix both `make` commands with `env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin`.
- **Shared downloads:** set `BR2_DL_DIR` to reuse a download cache between builds.
- **Release images:** use `pi4_release_defconfig`. Root login is locked, and the build fails without an SSH key because the device would otherwise be unreachable.

The development image `pi4_defconfig` has the root password `root`, so use it only on a trusted network.

## First Boot

1. Flash `sdcard.img.xz` with Raspberry Pi Imager ("Use custom"). On WSL, reach the file through `\\wsl$`.
2. To set up Wi-Fi, add this as `wpa_supplicant.conf` on the drive's boot partition, using your own values. On the next boot it moves to `/data` and is removed from the boot partition.

   ```text
   ctrl_interface=/run/wpa_supplicant
   update_config=1
   country=US

   network={
       ssid="your-network"
       psk="your-password"
   }
   ```

3. Boot the Pi. The first boot grows `/data` to fill the drive.
4. Log in on the HDMI or serial console (115200 baud on GPIO14/15) as `root`, or with `ssh astro@<ip-address>` if you added a key.

Per-device settings such as third-party DSI displays go in `astro-hardware.txt` on the boot partition.

## Booting from USB

The same `sdcard.img.xz` image works on a USB drive, which leaves any OS on the SD card untouched. Astro OS only ever writes to the drive it booted from.

1. Flash the image to the USB drive with Raspberry Pi Imager, and check that the USB drive is the selected target. Flashing erases that whole drive.
2. Check the Pi's bootloader from your current OS. Its version should be from September 2020 or later:

   ```sh
   vcgencmd bootloader_version
   vcgencmd bootloader_config | grep BOOT_ORDER
   ```

3. Choose how to switch between the two systems:
   - **No settings changed:** remove the SD card to boot from USB, assuming the Pi tries USB after the SD card. `BOOT_ORDER=0xf41` does this.
   - **No card swapping:** run `sudo -E rpi-eeprom-config --edit`, set `BOOT_ORDER=0xf14`, and reboot. The Pi then boots from USB when the drive is plugged in and from the SD card when it is unplugged.

Don't attach two drives flashed with this image at the same time, because both copies share the same partition IDs and the Pi could mount the wrong one.

## Connecting over USB-C

The USB-C port can connect the Pi directly to a PC, with no router needed:

1. Use a USB-C **data** cable, since many charging cables have no data wires.
2. The PC sees a new network adapter and a serial port.
3. To use SSH, connect with `ssh astro@10.55.0.1`. This needs an image built with your own SSH key, so it does not work with the published release image.
4. To use the serial port, connect at 115200 baud and log in as `root`. It starts late in boot, so it does not show early boot messages.

This port also powers the Pi, and a PC port may not supply enough, especially with a camera or other USB devices attached. If you see undervoltage warnings in `dmesg`, use a powered USB hub between the PC and the Pi, or power the Pi through the GPIO header with a regulated 5V supply.
