#!/bin/bash
# ===========================================================================
# build_linux_compat.sh -- construye los paquetes de Linux contra una glibc
#                          ANTIGUA, para que instalen en el mayor numero de
#                          distribuciones posible.
#
# El problema: un .deb hereda como dependencia la version de glibc de la
# maquina donde se compilo.  Construido en una distribucion reciente declara
# algo como "libc6 (>= 2.38)" y dpkg lo RECHAZA en Debian 12 o Ubuntu 22.04,
# aunque el codigo funcionase ahi perfectamente.  Y no hay flag que lo evite:
# glibc 2.38+ redirige strtoll/strtoul/strtoull a variantes __isoc23_* de forma
# incondicional, asi que la unica salida es compilar contra cabeceras antiguas.
#
# Lo que hace: monta un chroot minimo de Debian 11 (glibc 2.31), compila ahi y
# saca los paquetes.  Medido, el resultado baja de
#     libc6 (>= 2.38), libgcc-s1 (>= 3.0), libstdc++6 (>= 13.1)
# a una sola dependencia:
#     libc6 (>= 2.29)
# (la de libstdc++ desaparece porque el runtime de C++ se enlaza dentro; ver
# el comentario del CMakeLists).  Eso cubre Debian 11+, Ubuntu 20.04+ y RHEL 9+.
#
# Uso (necesita root):
#     sudo cmake/build_linux_compat.sh [opciones]
#
# Opciones:
#     --chroot <dir>   Donde vive el chroot (def: /opt/vppbuild-<suite>)
#     --suite <nombre> Suite de Debian (def: bullseye = Debian 11, glibc 2.31)
#     --mirror <url>   Mirror (def: archive.debian.org para suites archivadas)
#     --out <dir>      Donde dejar los paquetes (def: ./dist-linux)
#     --rebuild        Borra el chroot y lo vuelve a crear
# ===========================================================================
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUITE="bullseye"
MIRROR="http://archive.debian.org/debian"
CHROOT=""
OUT="$PWD/dist-linux"
REBUILD=0

while [ $# -gt 0 ]; do
    case "$1" in
        --chroot)  CHROOT="$2"; shift 2 ;;
        --suite)   SUITE="$2";  shift 2 ;;
        --mirror)  MIRROR="$2"; shift 2 ;;
        --out)     OUT="$2";    shift 2 ;;
        --rebuild) REBUILD=1;   shift ;;
        -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
        *) echo "[ERROR] opcion desconocida: $1" >&2; exit 2 ;;
    esac
done

[ -n "$CHROOT" ] || CHROOT="/opt/vppbuild-$SUITE"

if [ "$(id -u)" -ne 0 ]; then
    echo "[ERROR] debootstrap y chroot necesitan root.  Reejecuta con sudo." >&2
    exit 1
fi

command -v debootstrap >/dev/null 2>&1 || {
    echo "[ERROR] falta debootstrap (apt-get install debootstrap)" >&2; exit 1; }

# --- chroot ---------------------------------------------------------------
[ "$REBUILD" -eq 1 ] && rm -rf "$CHROOT"

if [ ! -x "$CHROOT/bin/bash" ]; then
    echo "=== creando chroot $SUITE en $CHROOT ==="
    # Se instala el sistema base y DESPUES las herramientas: pedirlas con
    # --include falla en las suites archivadas, donde algun paquete accesorio
    # ya no esta disponible aunque el resto si.
    debootstrap --variant=minbase "$SUITE" "$CHROOT" "$MIRROR"

    echo "deb $MIRROR $SUITE main" > "$CHROOT/etc/apt/sources.list"
    # Las suites archivadas tienen los indices caducados; sin esto apt se niega.
    echo 'Acquire::Check-Valid-Until false;' \
        > "$CHROOT/etc/apt/apt.conf.d/99no-check-valid"

    # `file` no es opcional: CPackDeb lo usa para calcular las dependencias con
    # dpkg-shlibdeps y aborta si no lo encuentra.
    chroot "$CHROOT" /bin/bash -c \
        "apt-get update >/dev/null && apt-get install -y --no-install-recommends \
         build-essential cmake pkg-config file >/dev/null"
else
    echo "=== reutilizando el chroot existente en $CHROOT ==="
fi

echo "--- toolchain del chroot ---"
chroot "$CHROOT" /bin/bash -c \
    "ldd --version | head -1; g++ --version | head -1; cmake --version | head -1"

# --- copiar fuentes y compilar --------------------------------------------
echo "=== copiando fuentes ==="
rm -rf "$CHROOT/src/vpp"
mkdir -p "$CHROOT/src/vpp"
( cd "$SRC_DIR" && tar cf - CMakeLists.txt README.md LICENSE.md \
        cmake include include_lib src tests ) \
    | ( cd "$CHROOT/src/vpp" && tar xf - )

mount -t proc proc "$CHROOT/proc" 2>/dev/null || true

echo "=== compilando ==="
chroot "$CHROOT" /bin/bash -c "cd /src/vpp && rm -rf b && \
    cmake -S . -B b -DCMAKE_BUILD_TYPE=Release >/dev/null && \
    cmake --build b -j \$(nproc) >/dev/null && \
    cd b && ctest --output-on-failure | tail -3"

echo "=== empaquetando ==="
chroot "$CHROOT" /bin/bash -c "cd /src/vpp/b && \
    cmake --build . --target installer-deb 2>&1 | grep 'package:' ; \
    cmake --build . --target installer-zip 2>&1 | grep 'package:' ; \
    if command -v rpmbuild >/dev/null 2>&1; then \
        cmake --build . --target installer-rpm 2>&1 | grep 'package:' ; \
    else echo '  (sin rpmbuild en el chroot: se omite el RPM)'; fi"

# --- recoger artefactos ----------------------------------------------------
mkdir -p "$OUT"
cp "$CHROOT"/src/vpp/b/*.deb "$CHROOT"/src/vpp/b/*.tar.gz "$OUT"/ 2>/dev/null || true
cp "$CHROOT"/src/vpp/b/*.rpm "$OUT"/ 2>/dev/null || true

echo
echo "=== paquetes en $OUT ==="
ls -la "$OUT"
echo
echo "=== dependencias declaradas ==="
for d in "$OUT"/*.deb; do
    [ -e "$d" ] || continue
    printf '%s\n  ' "$(basename "$d")"
    dpkg-deb -f "$d" Depends
done
