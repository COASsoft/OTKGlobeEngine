#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/build/apple"
rm -rf "$out"
mkdir -p "$out/headers"
cp "$root/include/ote/ote.h" "$root/bindings/apple/include/module.modulemap" "$out/headers/"

build() {
    local name="$1"
    shift
    cmake -S "$root" -B "$out/$name" -G Xcode -DOTE_BUILD_VIEWER=OFF "$@"
    cmake --build "$out/$name" --config Release --target opentakengine -- CODE_SIGNING_ALLOWED=NO -quiet
    local libs
    libs=$(find "$out/$name" -name '*.a' -path '*Release*')
    libtool -static -o "$out/$name/libOpenTAKEngine.a" $libs
}

build ios -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
build sim -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
build mac "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0

xcodebuild -create-xcframework \
    -library "$out/ios/libOpenTAKEngine.a" -headers "$out/headers" \
    -library "$out/sim/libOpenTAKEngine.a" -headers "$out/headers" \
    -library "$out/mac/libOpenTAKEngine.a" -headers "$out/headers" \
    -output "$out/OpenTAKEngine.xcframework"

swiftc -typecheck -sdk "$(xcrun --sdk iphoneos --show-sdk-path)" -target arm64-apple-ios15.0 -I "$out/headers" "$root/bindings/apple/OpenTAKView.swift"
swiftc -typecheck -sdk "$(xcrun --sdk macosx --show-sdk-path)" -target arm64-apple-macos12.0 -I "$out/headers" "$root/bindings/apple/OpenTAKView.swift"
echo "$out/OpenTAKEngine.xcframework"
