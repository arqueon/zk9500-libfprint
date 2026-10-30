#!/usr/bin/env bash
# Instala libfprint-cs9711-zk9500-git, recarga reglas udev/hwdb, reinicia fprintd
# y ofrece registrar huellas directamente en el ZKTeco ZK9500.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="${ZK9500_BUILDDIR:-/tmp/zk9500-pkgbuild}"

if [[ "${EUID}" -eq 0 ]]; then
  echo "Ejecuta este script como tu usuario normal (usará sudo solo para pacman/udev/systemctl)." >&2
  exit 1
fi

PKG_FILE="$(ls -1t "${WORK_DIR}"/libfprint-cs9711-zk9500-git-[0-9]*.pkg.tar.* 2>/dev/null | grep -v '\-debug-' | head -n1 || true)"

need_build=0
if [[ -z "${PKG_FILE}" || ! -f "${PKG_FILE}" ]]; then
  need_build=1
else
  for src in "${PROJECT_DIR}/PKGBUILD" "${PROJECT_DIR}/drivers/zk9500.c" "${PROJECT_DIR}/tools/zk9500-probe.c" "${PROJECT_DIR}/tools/zk9500-fprintd" "${PROJECT_DIR}/patches/0001-build-zk9500-opencv5-udev.patch" "${PROJECT_DIR}/udev/70-zk9500-libfprint.rules"; do
    if [[ "${src}" -nt "${PKG_FILE}" ]]; then
      need_build=1
      break
    fi
  done
fi

if [[ "${need_build}" -eq 1 ]]; then
  "${PROJECT_DIR}/build-pkg.sh"
  PKG_FILE="$(ls -1t "${WORK_DIR}"/libfprint-cs9711-zk9500-git-[0-9]*.pkg.tar.* | grep -v '\-debug-' | head -n1)"
fi

echo "==> Instalando ${PKG_FILE}..."
sudo sh -c 'yes | pacman -U "$1"' _ "${PKG_FILE}"

echo "==> Verificando enlace dinámico de /usr/lib/libfprint-2.so.2 (OpenCV 5 / libusb)..."
if ldd /usr/lib/libfprint-2.so.2 | grep -q "not found"; then
  echo "ERROR: faltan bibliotecas dinámicas en /usr/lib/libfprint-2.so.2:" >&2
  ldd /usr/lib/libfprint-2.so.2 | grep "not found" >&2
  exit 1
fi
echo "    ✓ Todas las bibliotecas dinámicas resueltas correctamente."

echo "==> Recargando hwdb, reglas udev y aplicando permisos USB a ZK9500/CS9711..."
sudo systemd-hwdb update
sudo udevadm control --reload
sudo udevadm trigger --action=change --subsystem-match=usb --attr-match=idVendor=1b55
sudo udevadm trigger --action=change --subsystem-match=usb --attr-match=idVendor=2541
sudo systemctl restart fprintd.service

echo ""
/usr/bin/zk9500-fprintd status || true

if [[ -t 0 && -t 1 ]]; then
  if gdbus call --system --dest net.reactivated.Fprint --object-path /net/reactivated/Fprint/Manager --method net.reactivated.Fprint.Manager.GetDevices 2>/dev/null | grep -q "/net/reactivated/Fprint/Device/"; then
    echo ""
    read -r -p "¿Deseas registrar tus huellas ahora en el ZKTeco ZK9500 (sincronizando tus dedos para activarlo en PAM)? [S/n] " resp
    resp="${resp:-S}"
    if [[ "${resp}" =~ ^[SsYy]$ ]]; then
      /usr/bin/zk9500-fprintd enroll --sync
      echo ""
      read -r -p "¿Probar una verificación en vivo con el ZKTeco ZK9500 ahora? [S/n] " vresp
      vresp="${vresp:-S}"
      if [[ "${vresp}" =~ ^[SsYy]$ ]]; then
        /usr/bin/zk9500-fprintd verify
      fi
    fi
  fi
fi

echo ""
echo "Listo. Comandos disponibles:"
echo "  zk9500-fprintd status                 # Ver ambos lectores, huellas y lector activo en PAM"
echo "  zk9500-fprintd enroll --sync          # Registrar en ZK9500 los mismos dedos del CS9711"
echo "  zk9500-fprintd enroll right-thumb     # Registrar un dedo específico en ZK9500"
echo "  zk9500-fprintd verify                 # Probar lectura y coincidencia NBIS en ZK9500"
