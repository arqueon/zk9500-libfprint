#!/usr/bin/env bash
# Compila libfprint-cs9711-zk9500-git en /tmp para mantener limpio el árbol del repositorio.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="${ZK9500_BUILDDIR:-/tmp/zk9500-pkgbuild}"

echo "==> Preparando entorno limpio en ${WORK_DIR}..."
mkdir -p "${WORK_DIR}"
rm -f "${WORK_DIR}"/libfprint-cs9711-zk9500-git-*.pkg.tar.*

install -m644 "${PROJECT_DIR}/PKGBUILD" "${WORK_DIR}/PKGBUILD"
install -m644 "${PROJECT_DIR}/drivers/zk9500.c" "${WORK_DIR}/zk9500.c"
install -m644 "${PROJECT_DIR}/tools/zk9500-probe.c" "${WORK_DIR}/zk9500-probe.c"
install -m755 "${PROJECT_DIR}/tools/zk9500-fprintd" "${WORK_DIR}/zk9500-fprintd"
install -m644 "${PROJECT_DIR}/patches/0001-build-zk9500-opencv5-udev.patch" "${WORK_DIR}/0001-build-zk9500-opencv5-udev.patch"
install -m644 "${PROJECT_DIR}/udev/70-zk9500-libfprint.rules" "${WORK_DIR}/70-zk9500-libfprint.rules"

cd "${WORK_DIR}"
echo "==> Ejecutando makepkg -sfc..."
makepkg -sfc

PKG_FILE="$(ls -1t "${WORK_DIR}"/libfprint-cs9711-zk9500-git-[0-9]*.pkg.tar.* | grep -v '\-debug-' | head -n1)"
echo ""
echo "==> Paquete generado correctamente:"
ls -lh "${PKG_FILE}"
