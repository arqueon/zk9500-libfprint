# zk9500-libfprint — Native `libfprint-2` Linux Driver for ZKTeco ZK9500 (`1b55:0124`)

[![CI](https://github.com/arqueon/zk9500-libfprint/actions/workflows/ci.yml/badge.svg)](https://github.com/arqueon/zk9500-libfprint/actions/workflows/ci.yml)
[![License: LGPL-2.1-or-later](https://img.shields.io/badge/License-LGPL_2.1+-blue.svg)](LICENSE)

> **Español:** [Documentación completa en español (`README.es.md`)](README.es.md)

Open-source (`LGPL-2.1-or-later`) native `libfprint-2` driver in C for the **ZKTeco ZK9500 (`1b55:0124`)** optical USB fingerprint scanner. Integrates seamlessly with **`fprintd`**, **`pam_fprintd.so`**, **`sudo`**, **Polkit**, **GDM / SDDM / greetd**, and **Wayland / X11 lock screens** on Linux.

The driver (`drivers/zk9500.c`) compiles cleanly with zero warnings against **both**:
1. **Vanilla upstream [`freedesktop.org/libfprint`](https://gitlab.freedesktop.org/libfprint/libfprint)** (zero external dependencies beyond standard `libfprint-2`: uses built-in NBIS `mindtct` + `bozorth3`), and
2. **Combined [`libfprint-CS9711`](https://github.com/archeYR/libfprint-CS9711) (`cs9711-rebase`)**, enabling **ZKTeco ZK9500 (`1b55:0124`)**, **Chipsailing CS9711 (`2541:0236`)**, and all ~40 standard upstream `libfprint-2` readers simultaneously in a single `libfprint-2.so.2` library (including an **OpenCV 4 & OpenCV 5** compatibility fix).

---

## Table of Contents

1. [Supported Hardware](#1-supported-hardware)
2. [Why ZKTeco's Old Linux SDK Binary Failed](#2-why-zktecos-old-linux-sdk-binary-failed)
3. [USB Wire Protocol Specification (`1b55:0124`)](#3-usb-wire-protocol-specification-1b550124)
4. [Installation](#4-installation)
   - [Arch Linux / CachyOS / Manjaro / EndeavourOS](#arch-linux--cachyos--manjaro--endeavouros)
   - [Ubuntu / Debian / Linux Mint / Pop!_OS](#ubuntu--debian--linux-mint--pop_os)
   - [Fedora / RHEL / openSUSE](#fedora--rhel--opensuse)
5. [Enrolling Fingerprints & Multi-Reader PAM Setup](#5-enrolling-fingerprints--multi-reader-pam-setup)
6. [Standalone USB Diagnostic & `.pgm` Capture Tool](#6-standalone-usb-diagnostic--pgm-capture-tool)
7. [Repository Layout](#7-repository-layout)

---

## 1. Supported Hardware

| USB `VID:PID` | Reader | Engine | Build Variant |
|---|---|---|---|
| **`1b55:0124`** | **ZKTeco Inc. ZK9500** (SilkID / MCU > 1.4 optical scanner) | NBIS (`300×375` @ 500 DPI) | Both (`upstream` & `cs9711-combined`) |
| **`2541:0236`** | **Chipsailing CS9711** | SIGFM (OpenCV 4 / 5) | `cs9711-combined` (root `PKGBUILD` or `--with-cs9711`) |
| All standard IDs | ~40 upstream `libfprint-2` drivers (Goodix, Synaptics, Elan, U.are.U, etc.) | Upstream `libfprint-2` | Both |

Because `libfprint-2` compiles all enabled drivers into one `libfprint-2.so.2`, a single installed build works indistinctly with **either reader alone** or **multiple readers plugged in at the same time**.

---

## 2. Why ZKTeco's Old Linux SDK Binary Failed

1. ZKTeco's legacy experimental Linux binary bundle (`libzkfp.so` + `libsilkidcap.so`) hardcoded the older PID `1b55:0120` (early SilkID / ZK4500 revision), which streamed raw `1600×1200` Bayer frames and required closed-source software trapezoid/perspective unwarping (`libcorrect`).
2. Protocol analysis verified against `ZKFPSensors/libsilkidcap.dll` (ZKFinger Standard SDK 5.3.0.33, which binds `VID_1B55&PID_0124` to user-space `libusb0.sys`) shows how `1b55:0124` actually works:
   - During open, querying `IN 0xE2` (`wIndex=0x0055`, 2 bytes) reports MCU firmware `> 1.4`.
   - On `1b55:0124` (MCU `> 1.4`), **the onboard MCU performs Bayer demosaicing, geometric trapezoid correction, cropping, and normalization in hardware**, delivering ready-to-match **`300×375` 8-bit grayscale 500 DPI frames (`112,500` bytes, headerless)** directly over `Bulk IN 0x82`.
   - Standard `libfprint-2` NBIS (`mindtct` minutiae extraction + `bozorth3` matcher at `ppmm = 500.0 / 25.4 ≈ 19.685`) processes these 500 DPI optical frames natively with **zero proprietary blobs**.

---

## 3. USB Wire Protocol Specification (`1b55:0124`)

All control transfers use `Vendor · Device` requests on `EP0` (`bmRequestType = 0x40` OUT / `0xC0` IN) on USB Interface `0` (`bInterfaceClass = 0xFF`):

| Stage | Direction & `bRequest` | `wValue` | `wIndex` | Length | Description |
|---|---|---|---|---|---|
| Engine wake | `OUT 0xE0` | `0x0000` | `0x0000` | `0 B` | Wakes sensor capture state machine |
| Sync packet | `OUT 0x80` | `0x0000` | `0x0000` | `16 B` (`0x00`) | Host sync + optional 4-byte status drain on `EP 0x82` |
| MCU version | `IN 0xE2` | `0x0000` | `0x0055` | `2 B` | Firmware revision (`> 1.4` indicates hardware-rectified `300×375` mode) |
| CMOS EEPROM | `IN 0xE7` | `0x0000` | `0x30..0x3B` | `1 B × 12` | `6 × uint16_le`: `exposure`, `R`, `G1`, `G2`, `B`, `checksum` (`sum` or `sum + 1`) |
| LED EEPROM | `IN 0xE7` | `0x0000` | `0x70..0x75` | `1 B × 6` | Factory PWM levels: `Main1`, `Main2`, `Side1`, `Side2`, `Anti1`, `Anti2` |
| Prism LEDs ON | `OUT 0xE1` | `(pwm << 8) \| 1` | `0x00..0x05` | `0 B` | Powers on the 6 optical prism illumination channels |
| CMOS gains | `OUT 0xE3` | register byte | `0xA3..0xAA`, `0x03..0x04` | `0 B` | RGB gains (`G1=A3,A4`, `R=A5,A6`, `B=A7,A8`, `G2=A9,AA`) & exposure (`03=hi`, `04=lo`) |
| Start stream | `OUT 0xE1` | `1, 0, 1, 3` | `0x53, 0x32, 0x31, 0x30` | `0 B` | Arms finger detection and `300×375` frame capture |
| Finger poll | `IN 0xEA` | `0x0000` | `0x0000` | `1 B` | `0x01` = finger present & frame ready on `EP 0x82`; `0x00` = no finger |
| Read frame | `Bulk IN 0x82` | — | — | `112,500 B` | Raw `300×375` 8-bit grayscale image @ 500 DPI (row-major, top-to-bottom) |
| Status LEDs | `OUT 0xE1` | `0x1401` / `0x1402` / `0x1403` | `0x0014` | `0 B` | Indicator pulse: Red (`0x1401`), Green (`0x1402`), White (`0x1403`) |
| Stop / sleep | `OUT 0xE1` | `0x0000` | `0x00..0x05`, `0x15, 0x30, 0x31` | `0 B` | Turns off all 6 prism LEDs and halts streaming on session close |

> **Hardware Quirk — Finger-Off Detection (`AWAIT_FINGER_OFF`) & USB Autosuspend:**
> - While a finger remains pressed on the prism, the MCU queues a fresh frame in its FIFO and keeps `IN 0xEA == 0x01`. During `AWAIT_FINGER_OFF`, the host must drain `112,500` bytes from `EP 0x82` whenever `0xEA == 1` so the status register transitions cleanly to `0x00` once the finger is lifted.
> - Kernel USB autosuspend (`ID_AUTOSUSPEND=1`) causes mid-session resets on both `1b55:0124` and `2541:0236`. Both the `hwdb` denylist patch and `udev/70-zk9500-libfprint.rules` keep `power/control="on"`.

---

## 4. Installation

Make sure `fprintd` is installed via your distribution's package manager (`sudo pacman -S fprintd`, `sudo apt install fprintd libpam-fprintd`, or `sudo dnf install fprintd fprintd-pam`).

### Arch Linux / CachyOS / Manjaro / EndeavourOS

#### Option A — ZK9500 + Chipsailing CS9711 + all upstream drivers (OpenCV 4 & 5 compatible)
```bash
git clone https://github.com/arqueon/zk9500-libfprint.git
cd zk9500-libfprint
./instalar.sh
```

#### Option B — Vanilla upstream `libfprint` + ZK9500 only (no OpenCV dependency)
```bash
git clone https://github.com/arqueon/zk9500-libfprint.git
cd zk9500-libfprint/packaging/arch-upstream
makepkg -si
sudo systemd-hwdb update && sudo udevadm control --reload
sudo udevadm trigger --action=change --subsystem-match=usb --attr-match=idVendor=1b55
sudo systemctl restart fprintd.service
```

### Ubuntu / Debian / Linux Mint / Pop!_OS

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake git meson ninja-build pkg-config python3 python3-gi \
  fprintd libpam-fprintd \
  libglib2.0-dev libgusb-dev libusb-1.0-0-dev libpixman-1-dev \
  libnss3-dev libssl-dev libgudev-1.0-dev libudev-dev udev libcairo2-dev

git clone https://github.com/arqueon/zk9500-libfprint.git
cd zk9500-libfprint

# Vanilla upstream libfprint + ZK9500:
./scripts/install-generic-linux.sh

# Or with Chipsailing CS9711 support too (install libopencv-dev first):
# sudo apt install -y libopencv-dev && ./scripts/install-generic-linux.sh --with-cs9711
```

### Fedora / RHEL / openSUSE

```bash
sudo dnf install -y \
  gcc gcc-c++ cmake git meson ninja-build pkgconf-pkg-config python3 python3-gobject \
  fprintd fprintd-pam \
  glib2-devel libgusb-devel libusb1-devel pixman-devel \
  nss-devel openssl-devel libgudev-devel systemd-devel cairo-devel

git clone https://github.com/arqueon/zk9500-libfprint.git
cd zk9500-libfprint
./scripts/install-generic-linux.sh
```

---

## 5. Enrolling Fingerprints & Multi-Reader PAM Setup

### Single Reader Connected
Standard `fprintd` commands work out of the box:
```bash
fprintd-enroll -f right-index-finger
fprintd-verify
fprintd-list "$USER"
```

### Multiple Readers Connected Simultaneously (e.g. `ZK9500` + `CS9711`)
Upstream `fprintd` has two multi-reader quirks:
1. `fprintd-enroll` has no `--device` flag (it always opens `Manager.GetDefaultDevice()`).
2. `pam_fprintd.so` claims **only one** reader per PAM prompt: specifically the first connected reader in `Manager.GetDevices()` with the **strictly highest count of enrolled fingerprints** (`num_prints > max_prints`).

This repository installs **`zk9500-fprintd`**, a D-Bus helper that targets specific readers by name and shows which device PAM will select (`[★ ACTIVO EN PAM]`):

```bash
# Show all connected readers, enrolled fingers, and active PAM device:
zk9500-fprintd status

# Enroll a specific finger directly on the ZK9500 (even if another reader is plugged in):
zk9500-fprintd enroll right-index-finger

# Enroll on the ZK9500 the exact same set of fingers enrolled on your other reader
# so PAM prioritizes the ZK9500 while preserving your other reader as automatic fallback:
zk9500-fprintd enroll --sync

# Live verification test on the ZK9500:
zk9500-fprintd verify
```

### Enabling Fingerprint Authentication in PAM
- **Ubuntu / Debian:** `sudo pam-auth-update` → enable **Fingerprint authentication**.
- **Fedora:** `sudo authselect enable-feature with-fingerprint && sudo authselect apply-changes`.
- **Arch Linux (`/etc/pam.d/sudo`, `/etc/pam.d/polkit-1`, etc.):**
  Add before `auth include system-auth`:
  ```pam
  auth sufficient pam_fprintd.so max-tries=3 timeout=10
  ```

---

## 6. Standalone USB Diagnostic & `.pgm` Capture Tool

`zk9500-probe` talks directly to `1b55:0124` via `libusb-1.0` (stop `fprintd` first if it is currently claiming USB interface 0), dumps factory EEPROM calibration values, captures `300×375` `.pgm` images, and powers off the prism LEDs cleanly on exit:

```bash
sudo systemctl stop fprintd.service
zk9500-probe 1 20 /tmp/zk9500-sample
```

---

## 7. Repository Layout

- [`drivers/zk9500.c`](drivers/zk9500.c) — Asynchronous `FpImageDevice` + `FpiSsm` driver for `libfprint-2` (compatible with both upstream `libfprint` and `libfprint-CS9711`).
- [`patches/0001-upstream-libfprint-zk9500.patch`](patches/0001-upstream-libfprint-zk9500.patch) — Clean patch against vanilla upstream `freedesktop.org/libfprint`.
- [`patches/0001-build-zk9500-opencv5-udev.patch`](patches/0001-build-zk9500-opencv5-udev.patch) — Combined patch against `archeYR/libfprint-CS9711` (`ZK9500` + `CS9711` + OpenCV 4/5 + `hwdb` autosuspend denylist).
- [`udev/70-zk9500-libfprint.rules`](udev/70-zk9500-libfprint.rules) — `uaccess` seat permissions and `ATTR{power/control}="on"` anti-autosuspend rules.
- [`tools/zk9500-fprintd`](tools/zk9500-fprintd) — Multi-reader `fprintd` D-Bus CLI (`status`, `enroll`, `enroll --sync`, `verify`, `delete`).
- [`tools/zk9500-probe.c`](tools/zk9500-probe.c) — Standalone `libusb-1.0` EEPROM & raw `.pgm` capture diagnostic utility.
- [`PKGBUILD`](PKGBUILD), [`build-pkg.sh`](build-pkg.sh), [`instalar.sh`](instalar.sh) — Arch Linux combined `libfprint-cs9711-zk9500-git` packaging.
- [`packaging/arch-upstream/PKGBUILD`](packaging/arch-upstream/PKGBUILD) — Arch Linux vanilla `libfprint-zk9500-git` packaging (no OpenCV dependency).
- [`scripts/install-generic-linux.sh`](scripts/install-generic-linux.sh) — Distro-agnostic Meson/Ninja installer for Debian/Ubuntu, Fedora, openSUSE, etc.

## License

Licensed under **LGPL-2.1-or-later** (matching upstream `libfprint-2`); see [`LICENSE`](LICENSE).
