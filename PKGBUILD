# Maintainer: arqueon <66568719+arqueon@users.noreply.github.com>
pkgname=libfprint-cs9711-zk9500-git
pkgver=1.94.10+1.r1866.20260217.02b285c
pkgrel=2
pkgdesc="libfprint-2 with ZKTeco ZK9500 (1b55:0124) and Chipsailing CS9711 (2541:0236) support (OpenCV 4/5 compatible)"
arch=('x86_64' 'aarch64')
url="https://github.com/arqueon/zk9500-libfprint"
license=('LGPL-2.1-or-later')
groups=('fprint')
depends=(
  'gcc-libs'
  'glib2'
  'glibc'
  'libgudev'
  'libgusb'
  'libusb'
  'opencv'
  'openssl'
  'pixman'
)
optdepends=(
  'python-gobject: for zk9500-fprintd multi-reader enrollment and PAM status CLI'
)
makedepends=(
  'cmake'
  'git'
  'meson'
  'ninja'
  'pkgconf'
)
provides=(
  "libfprint=${pkgver}"
  'libfprint-2.so'
  'libfprint-cs9711'
  'libfprint-zk9500'
)
conflicts=(
  'libfprint'
  'libfprint-cs9711-rebase-git'
)
replaces=(
  'libfprint-cs9711-rebase-git'
)
source=(
  'libfprint-CS9711::git+https://github.com/archeYR/libfprint-CS9711.git#branch=cs9711-rebase'
  'zk9500.c'
  'zk9500-probe.c'
  'zk9500-fprintd'
  '0001-build-zk9500-opencv5-udev.patch'
  '70-zk9500-libfprint.rules'
)
sha256sums=(
  'SKIP'
  'SKIP'
  'SKIP'
  'SKIP'
  'SKIP'
  'SKIP'
)

pkgver() {
  cd "${srcdir}/libfprint-CS9711"
  local _base _rev _date _hash
  _base="$(meson introspect --projectinfo meson.build 2>/dev/null | sed -n 's/.*"version": *"\([^"]*\)".*/\1/p')"
  [[ -z "${_base}" ]] && _base="1.94.10"
  _rev="$(git rev-list --count HEAD)"
  _date="$(git log -1 --format=%cd --date=format:%Y%m%d)"
  _hash="$(git rev-parse --short=7 HEAD)"
  printf '%s+1.r%s.%s.%s' "${_base}" "${_rev}" "${_date}" "${_hash}"
}

prepare() {
  cd "${srcdir}/libfprint-CS9711"
  patch -Np1 -i "${srcdir}/0001-build-zk9500-opencv5-udev.patch"
  install -Dm644 "${srcdir}/zk9500.c" "libfprint/drivers/zk9500.c"
}

build() {
  local _gir_opt="-Dintrospection=false"
  if [[ -x /usr/bin/g-ir-scanner ]]; then
    _gir_opt="-Dintrospection=true"
  fi

  arch-meson "${srcdir}/libfprint-CS9711" build \
    "${_gir_opt}" \
    -Dudev_hwdb=enabled \
    -Dinstalled-tests=false \
    -Ddoc=false \
    -Dgtk-examples=false

  meson compile -C build

  # Compile standalone USB diagnostic & PGM capture tool
  # shellcheck disable=SC2046,SC2086
  cc ${CFLAGS} ${CPPFLAGS} -O2 -Wall -Wextra \
    "${srcdir}/zk9500-probe.c" -o "${srcdir}/zk9500-probe" \
    $(pkg-config --cflags --libs libusb-1.0) ${LDFLAGS}
}

check() {
  meson test -C build --suite unit-tests --print-errorlogs
  ./build/libfprint/fprint-list-supported-devices | grep -q "1b55:0124"
  ./build/libfprint/fprint-list-supported-devices | grep -q "2541:0236"
}

package() {
  meson install -C build --destdir "${pkgdir}"
  install -Dm755 "${srcdir}/zk9500-probe" \
    "${pkgdir}/usr/bin/zk9500-probe"
  install -Dm755 "${srcdir}/zk9500-fprintd" \
    "${pkgdir}/usr/bin/zk9500-fprintd"
  install -Dm644 "${srcdir}/70-zk9500-libfprint.rules" \
    "${pkgdir}/usr/lib/udev/rules.d/70-zk9500-libfprint.rules"
}
