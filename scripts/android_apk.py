import argparse
import re
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

MANIFEST = """<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.opentakengine.viewer" android:versionCode="1" android:versionName="{version}">
    <uses-permission android:name="android.permission.INTERNET"/>
    <uses-feature android:glEsVersion="0x00030000" android:required="true"/>
    <application android:label="OpenTAKEngine" android:hasCode="false" android:extractNativeLibs="true"
        android:usesCleartextTraffic="true">
        <activity android:name="android.app.NativeActivity" android:exported="true"
            android:configChanges="orientation|screenSize|screenLayout|keyboardHidden|uiMode"
            android:theme="@android:style/Theme.NoTitleBar.Fullscreen">
            <meta-data android:name="android.app.lib_name" android:value="ote_viewer"/>
            <intent-filter>
                <action android:name="android.intent.action.MAIN"/>
                <category android:name="android.intent.category.LAUNCHER"/>
            </intent-filter>
        </activity>
    </application>
</manifest>
"""


def sdk_root():
    for key in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        if os.environ.get(key):
            return Path(os.environ[key])
    if sys.platform == "win32":
        return Path(os.environ["LOCALAPPDATA"]) / "Android" / "Sdk"
    return Path.home() / "Android" / "Sdk"


def latest(path):
    items = sorted((p for p in path.iterdir() if p.is_dir()), key=lambda p: [int(x) if x.isdigit() else -1 for x in p.name.replace("-", ".").split(".")])
    return items[-1]


def tool(build_tools, name):
    for ext in ("", ".exe", ".bat"):
        p = build_tools / (name + ext)
        if p.exists():
            return str(p)
    raise SystemExit(f"missing {name} in {build_tools}")


def run(cmd, **kw):
    print(">", " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--abi", default="arm64-v8a")
    ap.add_argument("--install", action="store_true")
    ap.add_argument("--run", action="store_true")
    args = ap.parse_args()

    sdk = sdk_root()
    ndk = latest(sdk / "ndk")
    build_tools = latest(sdk / "build-tools")
    platform = latest(sdk / "platforms") / "android.jar"
    out = ROOT / "build" / f"android-{args.abi}"
    toolchain = ndk / "build" / "cmake" / "android.toolchain.cmake"
    if not (out / "build.ninja").exists():
        run(["cmake", "-S", ROOT, "-B", out, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
             f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", f"-DANDROID_ABI={args.abi}", "-DANDROID_PLATFORM=android-26"])
    run(["cmake", "--build", out])

    lib = out / "apps" / "viewer" / "libote_viewer.so"
    apk_dir = out / "apk"
    shutil.rmtree(apk_dir, ignore_errors=True)
    apk_dir.mkdir(parents=True)
    version = re.search(r"project\(\w+ VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text()).group(1)
    (apk_dir / "AndroidManifest.xml").write_text(MANIFEST.format(version=version), encoding="utf-8")
    unsigned = apk_dir / "unsigned.apk"
    run([tool(build_tools, "aapt2"), "link", "-o", unsigned, "--manifest", apk_dir / "AndroidManifest.xml", "-I", platform,
         "--min-sdk-version", "26", "--target-sdk-version", "35"])
    stripped = apk_dir / "libote_viewer.so"
    strip = next((ndk / "toolchains" / "llvm" / "prebuilt").glob("*/bin/llvm-strip*"))
    run([strip, "--strip-unneeded", "-o", stripped, lib])
    with zipfile.ZipFile(unsigned, "a", zipfile.ZIP_DEFLATED) as z:
        z.write(stripped, f"lib/{args.abi}/libote_viewer.so")
    aligned = apk_dir / "aligned.apk"
    run([tool(build_tools, "zipalign"), "-f", "-p", "4", unsigned, aligned])
    keystore = Path.home() / ".android" / "debug.keystore"
    if not keystore.exists():
        keystore.parent.mkdir(parents=True, exist_ok=True)
        run(["keytool", "-genkeypair", "-v", "-keystore", keystore, "-storepass", "android", "-alias", "androiddebugkey",
             "-keypass", "android", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
             "-dname", "CN=Android Debug,O=Android,C=US"])
    final = out / "opentakengine-viewer.apk"
    run([tool(build_tools, "apksigner"), "sign", "--ks", keystore, "--ks-pass", "pass:android", "--key-pass", "pass:android",
         "--out", final, aligned])
    print(final)
    adb = sdk / "platform-tools" / ("adb.exe" if sys.platform == "win32" else "adb")
    if args.install or args.run:
        run([adb, "install", "-r", final])
    if args.run:
        run([adb, "shell", "am", "start", "-n", "com.opentakengine.viewer/android.app.NativeActivity"])


if __name__ == "__main__":
    main()
