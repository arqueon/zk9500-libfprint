#!/usr/bin/env bash
# Generic Meson/Ninja builder and installer for Debian/Ubuntu, Fedora, openSUSE, etc.
# Supports both vanilla upstream libfprint (default) and --with-cs9711 (ZK9500 + CS9711).
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="${ZK9500_WORKDIR:-/tmp/zk9500-libfprint-generic}"
WITH_CS9711=0
PREFIX="/usr"
LIBDIR=""

usage() {
  cat <<'EOF'
Usage: ./scripts/install-generic-linux.sh [--with-cs9711] [--prefix /usr] [--libdir lib/x86_64-linux-gnu]

Options:
  --with-cs9711         Build on top of archeYR/libfprint-CS9711 (enables both ZKTeco ZK9500
                        1b55:0124 and Chipsailing CS9711 2541:0236; requires OpenCV 4 or 5).
                        Without this flag, builds against vanilla upstream freedesktop libfprint.
  --prefix PATH         Installation prefix (default: /usr so system fprintd picks up libfprint-2.so.2).
  --libdir DIR          Override library subdir if needed (auto-detected via pkg-config otherwise).
  -h, --help            Show this help message.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --with-cs9711) WITH_CS9711=1; shift ;;
    --prefix) PREFIX="$2"; shift 2 ;;
    --libdir) LIBDIR="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z "${LIBDIR}" ]]; then
  sys_libdir="$(pkg-config --variable=libdir glib-2.0 2>/dev/null || true)"
  if [[ -n "${sys_libdir}" && "${sys_libdir}" == "${PREFIX}/"* ]]; then
    LIBDIR="${sys_libdir#${PREFIX}/}"
  elif [[ -d "/usr/lib/x86_64-linux-gnu" ]]; then
    LIBDIR="lib/x86_64-linux-gnu"
  elif [[ -d "/usr/lib64" && ! -L "/usr/lib64" ]]; then
    LIBDIR="lib64"
  else
    LIBDIR="lib"
  fi
fi

echo "==> Building in ${WORK_DIR} (prefix=${PREFIX}, libdir=${LIBDIR}, with_cs9711=${WITH_CS9711})..."
rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}"

if [[ "${WITH_CS9711}" -eq 1 ]]; then
  git clone --depth 1 --branch cs9711-rebase https://github.com/archeYR/libfprint-CS9711.git "${WORK_DIR}/libfprint"
  patch -d "${WORK_DIR}/libfprint" -Np1 -i "${REPO_DIR}/patches/0001-build-zk9500-opencv5-udev.patch"
else
  git clone --depth 1 https://gitlab.freedesktop.org/libfprint/libfprint.git "${WORK_DIR}/libfprint"
  patch -d "${WORK_DIR}/libfprint" -Np1 -i "${REPO_DIR}/patches/0001-upstream-libfprint-zk9500.patch"
fi

install -Dm644 "${REPO_DIR}/drivers/zk9500.c" "${WORK_DIR}/libfprint/libfprint/drivers/zk9500.c"

GIR_OPT="-Dintrospection=false"
if command -v g-ir-scanner >/dev/null 2>&1; then
  GIR_OPT="-Dintrospection=true"
fi

meson setup "${WORK_DIR}/build" "${WORK_DIR}/libfprint" \
  --prefix="${PREFIX}" \
  --libdir="${LIBDIR}" \
  --buildtype=release \
  "${GIR_OPT}" \
  -Dudev_hwdb=enabled \
  -Dinstalled-tests=false \
  -Ddoc=false \
  -Dgtk-examples=false

meson compile -C "${WORK_DIR}/build"
meson test -C "${WORK_DIR}/build" --suite unit-tests --print-errorlogs

"${WORK_DIR}/build/libfprint/fprint-list-supported-devices" | grep -q "1b55:0124"

# shellcheck disable=SC2046
cc -O2 -Wall -Wextra "${REPO_DIR}/tools/zk9500-probe.c" \
  -o "${WORK_DIR}/zk9500-probe" \
  $(pkg-config --cflags --libs libusb-1.0)

echo "==> Installing library, udev rules, and helper tools (requires sudo)..."
sudo meson install -C "${WORK_DIR}/build"
sudo install -Dm755 "${WORK_DIR}/zk9500-probe" "${PREFIX}/bin/zk9500-probe"
sudo install -Dm755 "${REPO_DIR}/tools/zk9500-fprintd" "${PREFIX}/bin/zk9500-fprintd"
sudo install -Dm644 "${REPO_DIR}/udev/70-zk9500-libfprint.rules" "/lib/udev/rules.d/70-zk9500-libfprint.rules" 2>/dev/null \
  || sudo install -Dm644 "${REPO_DIR}/udev/70-zk9500-libfprint.rules" "/usr/lib/udev/rules.d/70-zk9500-libfprint.rules"

sudo ldconfig
sudo systemd-hwdb update 2>/dev/null || sudo udevadm hwdb --update 2>/dev/null || true
sudo udevadm control --reload
sudo udevadm trigger --action=change --subsystem-match=usb --attr-match=idVendor=1b55
if [[ "${WITH_CS9711}" -eq 1 ]]; then
  sudo udevadm trigger --action=change --subsystem-match=usb --attr-match=idVendor=2541
fi
sudo systemctl restart fprintd.service 2>/dev/null || true

echo ""
echo "==> Done! Detected fprintd devices:"
"${PREFIX}/bin/zk9500-fprintd" status || true
