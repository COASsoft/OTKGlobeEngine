import argparse
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from android_apk import ROOT, latest, run, sdk_root

MANIFEST = """<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.otkglobeengine">
    <uses-sdk android:minSdkVersion="26" />
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
    <uses-feature android:glEsVersion="0x00030000" android:required="true" />
</manifest>
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--abis", default="arm64-v8a,armeabi-v7a,x86_64")
    args = ap.parse_args()
    sdk = sdk_root()
    ndk = latest(sdk / "ndk")
    platform = latest(sdk / "platforms") / "android.jar"
    toolchain = ndk / "build" / "cmake" / "android.toolchain.cmake"
    out = ROOT / "build" / "aar"
    shutil.rmtree(out, ignore_errors=True)
    (out / "classes").mkdir(parents=True)
    libs = {}
    for abi in args.abis.split(","):
        build = ROOT / "build" / f"android-{abi}"
        if not (build / "build.ninja").exists():
            run(["cmake", "-S", ROOT, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                 f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", f"-DANDROID_ABI={abi}", "-DANDROID_PLATFORM=android-26"])
        run(["cmake", "--build", build, "--target", "otkglobeengine_jni"])
        lib = next(build.rglob("libotkglobeengine_jni.so"))
        stripped = out / abi / lib.name
        stripped.parent.mkdir(parents=True, exist_ok=True)
        strip = next((ndk / "toolchains" / "llvm" / "prebuilt").glob("*/bin/llvm-strip*"))
        run([strip, "--strip-unneeded", "-o", stripped, lib])
        libs[abi] = stripped
    sources = [str(p) for p in (ROOT / "bindings" / "android" / "java").rglob("*.java")]
    run(["javac", "-source", "1.8", "-target", "1.8", "-nowarn", "-Xlint:-options", "-bootclasspath", platform,
         "-d", out / "classes", *sources])
    run(["jar", "cf", out / "classes.jar", "-C", out / "classes", "."])
    version = re.search(r"project\(\w+ VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text()).group(1)
    aar = ROOT / "build" / f"otkglobeengine-{version}.aar"
    with zipfile.ZipFile(aar, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("AndroidManifest.xml", MANIFEST)
        z.write(out / "classes.jar", "classes.jar")
        z.writestr("R.txt", "")
        for abi, lib in libs.items():
            z.write(lib, f"jni/{abi}/libotkglobeengine_jni.so")
    print(aar)


if __name__ == "__main__":
    main()
