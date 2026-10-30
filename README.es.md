# zk9500-libfprint — Driver nativo `libfprint-2` para ZKTeco ZK9500 (`1b55:0124`) en Linux

[![CI](https://github.com/arqueon/zk9500-libfprint/actions/workflows/ci.yml/badge.svg)](https://github.com/arqueon/zk9500-libfprint/actions/workflows/ci.yml)
[![License: LGPL-2.1-or-later](https://img.shields.io/badge/License-LGPL_2.1+-blue.svg)](LICENSE)

> **English:** [Full English documentation (`README.md`)](README.md)

Driver libre en C (`LGPL-2.1-or-later`) para el lector óptico USB de huella dactilar **ZKTeco ZK9500 (`1b55:0124`)**, integrado nativamente en `libfprint-2` para funcionar con **`fprintd`**, **`pam_fprintd.so`**, **`sudo`**, **Polkit**, **GDM / SDDM / greetd** y pantallas de bloqueo Wayland / X11.

El archivo [`drivers/zk9500.c`](drivers/zk9500.c) compila sin advertencias en **dos variantes**:
1. **`libfprint` oficial de `freedesktop.org`** (cero dependencias adicionales: usa el motor NBIS `mindtct` + `bozorth3` integrado en `libfprint-2`), y
2. **Compilación combinada con [`libfprint-CS9711`](https://github.com/archeYR/libfprint-CS9711)**, habilitando simultáneamente en una sola biblioteca `libfprint-2.so.2` el **ZKTeco ZK9500 (`1b55:0124`)**, el **Chipsailing CS9711 (`2541:0236`)** y los ~40 lectores estándar de `libfprint-2` (con soporte tanto para **OpenCV 4** como **OpenCV 5**).

De este modo, una misma instalación sirve **indistintamente para uno u otro lector por separado, o para ambos conectados al mismo tiempo**.

---

## 1. Por qué fallaba el binario experimental antiguo de ZKTeco en Linux

1. El antiguo paquete experimental de Linux (`libzkfp.so` + `libsilkidcap.so`) solo reconocía el PID `1b55:0120` (primera revisión SilkID / ZK4500), que entregaba tramas Bayer crudas `1600×1200` y dependía de corrección trapezoidal por software cerrado (`libcorrect`).
2. La verificación del protocolo sobre `ZKFPSensors/libsilkidcap.dll` (ZKFinger Standard SDK 5.3.0.33, que opera en espacio de usuario sobre `libusb0.sys`) confirmó cómo trabaja realmente el `1b55:0124`:
   - Al consultar `IN 0xE2` (`wIndex=0x0055`, 2 bytes), el lector reporta revisión de MCU `> 1.4`.
   - En los dispositivos `1b55:0124` (MCU `> 1.4`), **el propio MCU interno realiza la corrección geométrica trapezoidal, el recorte y la normalización en hardware**, entregando fotogramas listos de **`300×375` píxeles en escala de grises de 8 bits a 500 DPI (`112,500` bytes sin cabecera)** por `Bulk IN 0x82`.
   - `libfprint-2` procesa esa imagen de 500 DPI (`ppmm = 500.0 / 25.4 ≈ 19.685`) directamente con su motor **NBIS (`mindtct` + `bozorth3`)** sin requerir ningún binario propietario.

---

## 2. Especificación del protocolo USB (`1b55:0124`)

Todas las transferencias de control usan peticiones `Vendor · Device` en `EP0` (`0x40` OUT / `0xC0` IN) sobre la interfaz USB `0`:

| Fase | Dirección / `bRequest` | `wValue` | `wIndex` | Tamaño | Propósito |
|---|---|---|---|---|---|
| Despertar | `OUT 0xE0` | `0x0000` | `0x0000` | `0 B` | Inicia la máquina de captura del sensor |
| Sincronía | `OUT 0x80` | `0x0000` | `0x0000` | `16 B` (`0x00`) | Sincronización + drenaje opcional de 4 B en `EP 0x82` |
| Versión MCU | `IN 0xE2` | `0x0000` | `0x0055` | `2 B` | Revisión de firmware (`> 1.4` indica modo `300×375` corregido en MCU) |
| EEPROM CMOS | `IN 0xE7` | `0x0000` | `0x30..0x3B` | `1 B × 12` | `6 × uint16_le`: `exposure`, `R`, `G1`, `G2`, `B`, `checksum` (`sum` o `sum + 1`) |
| EEPROM LEDs | `IN 0xE7` | `0x0000` | `0x70..0x75` | `1 B × 6` | Niveles PWM de fábrica: `Main1`, `Main2`, `Side1`, `Side2`, `Anti1`, `Anti2` |
| LEDs prisma | `OUT 0xE1` | `(pwm << 8) \| 1` | `0x00..0x05` | `0 B` | Enciende los 6 canales LED ópticos del prisma |
| Ganancias CMOS | `OUT 0xE3` | valor | `0xA3..0xAA`, `0x03..0x04` | `0 B` | Ganancias RGB (`G1=A3,A4`, `R=A5,A6`, `B=A7,A8`, `G2=A9,AA`) y exposición (`03=hi`, `04=lo`) |
| Iniciar flujo | `OUT 0xE1` | `1, 0, 1, 3` | `0x53, 0x32, 0x31, 0x30` | `0 B` | Arma la detección de dedo y captura de tramas |
| Estado dedo | `IN 0xEA` | `0x0000` | `0x0000` | `1 B` | `0x01` = dedo presente y trama lista en `EP 0x82`; `0x00` = sin dedo |
| Fotograma | `Bulk IN 0x82` | — | — | `112,500 B` | Imagen `300×375` 8-bit grayscale @ 500 DPI |
| LEDs indicador | `OUT 0xE1` | `0x1401` / `0x1402` / `0x1403` | `0x0014` | `0 B` | Pulso de LED: rojo (`0x1401`), verde (`0x1402`), blanco (`0x1403`) |
| Reposo / cierre | `OUT 0xE1` | `0x0000` | `0x00..0x05`, `0x15, 0x30, 0x31` | `0 B` | Apaga los 6 LEDs del prisma y detiene la captura |

---

## 3. Instalación por distribución

### Arch Linux / CachyOS / Manjaro / EndeavourOS

#### Opción A — Combinado: ZK9500 + Chipsailing CS9711 + lectores estándar (compatible con OpenCV 4 y 5)
```bash
git clone https://github.com/arqueon/zk9500-libfprint.git
cd zk9500-libfprint
./instalar.sh
```

#### Opción B — Solo `libfprint` oficial + ZK9500 (sin dependencia de OpenCV)
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
./scripts/install-generic-linux.sh
# O añade --with-cs9711 (instalando antes libopencv-dev) si también usas un Chipsailing CS9711.
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

## 4. Registro de huellas y convivencia con múltiples lectores

- Si **solo tienes un lector conectado**, funcionan tanto `fprintd-enroll` / `fprintd-verify` como `zk9500-fprintd`.
- Si **tienes dos lectores conectados a la vez** (por ejemplo, `ZK9500` y `CS9711`), `pam_fprintd.so` selecciona automáticamente el primer lector con **mayor cantidad de huellas registradas** (`num_prints > max_prints`), y `fprintd-enroll` estándar carece de opción `--device`. Usa `zk9500-fprintd`:

```bash
# Ver lectores detectados, huellas por lector y cuál tiene [★ ACTIVO EN PAM]:
zk9500-fprintd status

# Registrar un dedo específicamente en el ZK9500:
zk9500-fprintd enroll right-index-finger

# Registrar en el ZK9500 los mismos dedos que ya tengas en otro lector conectado:
zk9500-fprintd enroll --sync

# Probar verificación en vivo en el ZK9500:
zk9500-fprintd verify
```

### Diagnóstico directo USB a `.pgm` (sin `fprintd`)
```bash
sudo systemctl stop fprintd.service
zk9500-probe 1 20 /tmp/huella-zk9500
```

## Licencia

Distribuido bajo **LGPL-2.1-or-later** (la misma licencia que `libfprint-2`); consulta [`LICENSE`](LICENSE).
