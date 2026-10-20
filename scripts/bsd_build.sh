#!/bin/sh
set -eu

root=$(pwd)
build=${OTE_BUILD_DIR:-$HOME/ote-build}
package=${OTE_PACKAGE:-0}
os=$(uname -s)
prefix_path=""

case "$os" in
FreeBSD)
    sudo env ASSUME_ALWAYS_YES=yes pkg install -y cmake ninja curl git pkgconf libX11 libXi libXcursor mesa-libs
    ;;
OpenBSD)
    sudo pkg_add -I cmake ninja curl git
    prefix_path="/usr/X11R6;/usr/local"
    ;;
NetBSD)
    sudo pkgin -y install cmake ninja-build curl git-base
    prefix_path="/usr/X11R7;/usr/pkg"
    ;;
*)
    echo "unsupported system $os" >&2
    exit 1
    ;;
esac

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$prefix_path"
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure

[ "$package" = 1 ] || exit 0

rm -rf "$root/dist" "$build/stage"
mkdir -p "$root/dist"
(cd "$build" && cpack -G TGZ)
cp "$build"/opentakengine-*.tar.gz "$root/dist/"

DESTDIR="$build/stage" cmake --install "$build" --prefix /usr/local
cmake -S "$root/tests/package_consumer" -B "$build/consumer" -G Ninja -DCMAKE_PREFIX_PATH="$build/stage/usr/local;$prefix_path"
cmake --build "$build/consumer"
LD_LIBRARY_PATH="$build/stage/usr/local/lib" "$build/consumer/consumer"

if [ "$os" = FreeBSD ]; then
    version=$(sed -n 's/^project(OpenTAKEngine VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
    deps=""
    for p in curl mesa-libs libX11 libXi libXcursor; do
        line=$(pkg query '%n %o %v' "$p")
        set -- $line
        deps="$deps  \"$1\": { origin: \"$2\", version: \"$3\" },
"
    done
    cat > "$build/+MANIFEST" <<EOF
name: opentakengine
version: "$version"
origin: graphics/opentakengine
comment: "Cross-platform map engine for TAK-style applications"
desc: "OpenTAKEngine renders a 3D globe with imagery, terrain, 3D Tiles, MIL-STD-2525 symbols and features, works offline, and exposes a C ABI."
maintainer: grangedevgroup@gmail.com
www: https://github.com/grangedevgroup-code/OpenTAKEngine
prefix: /usr/local
licenselogic: single
licenses: [APACHE20]
categories: [graphics, devel]
deps: {
$deps}
EOF
    (cd "$build/stage" && find . -type f -o -type l | sed 's|^\.||' | sort) > "$build/plist"
    pkg create -M "$build/+MANIFEST" -r "$build/stage" -p "$build/plist" -o "$root/dist"
    for f in "$root"/dist/opentakengine-*.pkg; do
        mv "$f" "$root/dist/opentakengine-$version-freebsd-$(uname -m).pkg"
    done
fi

ls -l "$root/dist"
