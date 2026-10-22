#!/bin/sh
# Builds meshy_<version>_<arch>.deb for VitruvianOS.
#
#   sh packaging/build-deb.sh            (from the project root)
#
# Needs VitruvianOS headers and libraries (vos-dev / pkg-config vos, or
# /system/develop/headers), the rc, xres and resattr tools, tar with xattr
# support and dpkg-deb. Override the maintainer with DEBFULLNAME/DEBEMAIL.
set -eu

cd "$(dirname "$0")/.."
ROOT=$(pwd)

VERSION=0.1.0
REVISION=1
ARCH=$(dpkg --print-architecture)
MAINTAINER="${DEBFULLNAME:-Meshy maintainer} <${DEBEMAIL:-19998719+idumych@users.noreply.github.com}>"
# Built against VitruvianOS 0.6.2. The Haiku API is C++ and V\OS is pre-1.0,
# so class layouts may change in 0.7: require a rebuild then.
VOS_DEPENDS="vos (>= 0.6.2), vos (<< 0.7~)"

export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(date +%s)}"

BUILD="$ROOT/obj/deb"
PKG="$BUILD/meshy_${VERSION}-${REVISION}_${ARCH}"
OUT="$ROOT/meshy_${VERSION}-${REVISION}_${ARCH}.deb"

for tool in rc xres resattr dpkg-deb tar; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "build-deb: '$tool' not found in PATH" >&2; exit 1; }
done

# 1. Build. Strip at link time: xres appends the resources after the ELF
#    data, so stripping afterwards would cut them off.
make clean >/dev/null
make CXXFLAGS="${CXXFLAGS:--O2 -g -fstack-protector-strong -D_FORTIFY_SOURCE=2}" \
	LDFLAGS="${LDFLAGS:-} -s -Wl,--as-needed -Wl,-z,relro -Wl,-z,now"

# 2. Package tree
rm -rf "$BUILD"
mkdir -p "$PKG/DEBIAN" \
	"$PKG/system/apps" \
	"$PKG/system/data/deskbar/menu/Applications" \
	"$PKG/usr/bin" \
	"$PKG/usr/share/meshy" \
	"$PKG/usr/share/doc/meshy"

install -m 0755 Meshy "$PKG/system/apps/Meshy"
ln -s /system/apps/Meshy "$PKG/system/data/deskbar/menu/Applications/Meshy"
ln -s /system/apps/Meshy "$PKG/usr/bin/meshy"

# 3. Attributes. dpkg drops xattrs, so ship them in a tarball on an empty
#    placeholder (as vos does) and let postinst copy them onto the binary.
STAGE="$BUILD/attrs"
mkdir -p "$STAGE/system/apps"
: > "$STAGE/system/apps/Meshy"
resattr -O -o "$STAGE/system/apps/Meshy" "$ROOT/obj/Meshy.rsrc"
# BEOS:TYPE as mimeset would set it: B_MIME_STRING_TYPE ('MIMS', stored
# little-endian) followed by the NUL-terminated value.
TYPE_HEX=534d494d$(printf '%s' application/x-vnd.Be-elfexecutable | od -A n -t x1 | tr -d ' \n')00
if command -v setfattr >/dev/null 2>&1; then
	setfattr -n user.beos.BEOS:TYPE -v "0x$TYPE_HEX" "$STAGE/system/apps/Meshy"
else
	python3 -c 'import os,sys; os.setxattr(sys.argv[1], "user.beos.BEOS:TYPE", bytes.fromhex(sys.argv[2]))' \
		"$STAGE/system/apps/Meshy" "$TYPE_HEX"
fi
tar --xattrs --xattrs-include='user.*' --owner=0 --group=0 --numeric-owner \
	--mtime="@$SOURCE_DATE_EPOCH" -cf "$PKG/usr/share/meshy/apps_attrs.tar" \
	-C "$STAGE" ./system/apps/Meshy

# 4. Docs
cp packaging/debian/copyright "$PKG/usr/share/doc/meshy/copyright"
gzip -9n -c packaging/debian/changelog > "$PKG/usr/share/doc/meshy/changelog.Debian.gz"
gzip -9n -c README.md > "$PKG/usr/share/doc/meshy/README.md.gz"

# 5. Control files
install -m 0755 packaging/debian/postinst "$PKG/DEBIAN/postinst"
SIZE=$(du -sk --apparent-size --exclude=DEBIAN "$PKG" | cut -f1)
cat > "$PKG/DEBIAN/control" <<EOF
Package: meshy
Version: ${VERSION}-${REVISION}
Architecture: ${ARCH}
Maintainer: ${MAINTAINER}
Installed-Size: ${SIZE}
Depends: ${VOS_DEPENDS}, libc6 (>= 2.38), libstdc++6 (>= 11), libgcc-s1 (>= 3.0), attr
Section: network
Priority: optional
Description: Native MeshCore mesh radio client for VitruvianOS
 Meshy for Haiku is a native C++ client for MeshCore mesh radios. It
 reimplements the MeshCore protocol, crypto, storage and transports in C++
 and provides a native Haiku Interface Kit UI with no Python, GTK or GLib
 at runtime.
 .
 Connections are USB/UART serial or TCP (Wi-Fi bridge). The map view draws
 nodes on a projection with a graticule. Chat history renders in a styled
 read-only BTextView.
EOF
( cd "$PKG" && find . -type f ! -path './DEBIAN/*' -printf '%P\0' \
	| sort -z | xargs -0 md5sum ) > "$PKG/DEBIAN/md5sums"

find "$PKG" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
chmod -R u+rwX,go+rX,go-w "$PKG"

dpkg-deb --root-owner-group -Zxz --build "$PKG" "$OUT"
echo "Built $OUT"
