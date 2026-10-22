#!/bin/sh
# build-hpkg.sh — builds meshy-<version>-<revision>-<arch>.hpkg on Haiku/Renku.
#
#   make hpkg            (or: sh packaging/haiku/build-hpkg.sh)
#
# Run it on Haiku R1/beta6 or later: the package requires the Haiku it was
# built on (>= r1~beta6), because the Haiku API is C++ and a binary built
# against newer libraries may not load on older ones.
set -eu

cd "$(dirname "$0")/../.."
ROOT=$(pwd)

VERSION=0.1.0
REVISION=1
ARCH=$(getarch)
PACKAGER="${PACKAGER:-idumych <19998719+idumych@users.noreply.github.com>}"
OUT="$ROOT/meshy-${VERSION}-${REVISION}-${ARCH}.hpkg"

for tool in rc xres mimeset package getarch; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "build-hpkg: '$tool' not found (needs Haiku with haiku_devel)" >&2
		exit 1; }
done

# 1. Build. Strip at link time: xres appends the resources after the ELF
#    data, so stripping afterwards would cut them off.
make clean >/dev/null
make LDFLAGS="${LDFLAGS:-} -s"

# 2. Package tree. Inside the package, paths are relative to the
#    installation location (/boot/system).
PKG="$ROOT/obj/hpkg"
rm -rf "$PKG"
mkdir -p "$PKG/apps" \
	"$PKG/bin" \
	"$PKG/data/deskbar/menu/Applications" \
	"$PKG/documentation/packages/meshy"

cp Meshy "$PKG/apps/Meshy"
mimeset -f "$PKG/apps/Meshy"
ln -s ../apps/Meshy "$PKG/bin/meshy"
ln -s ../../../../apps/Meshy "$PKG/data/deskbar/menu/Applications/Meshy"
cp README.md LICENSE "$PKG/documentation/packages/meshy/"
cp LICENSE "$PKG/LICENSE"

sed -e "s/@VERSION@/${VERSION}-${REVISION}/g" -e "s/@PVERSION@/${VERSION}/g" \
	-e "s/@ARCH@/${ARCH}/g" \
	-e "s/@PACKAGER@/${PACKAGER}/g" \
	packaging/haiku/PackageInfo.in > "$PKG/.PackageInfo"

# 3. Package
rm -f "$OUT"
package create -C "$PKG" "$OUT"
package list -i "$OUT" >/dev/null
echo "Built $OUT"
