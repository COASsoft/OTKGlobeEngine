import argparse
import os
import platform
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, 'bindings', 'web')


def find_emsdk():
    for candidate in (os.environ.get('EMSDK'), os.path.expanduser('~/emsdk')):
        if candidate and os.path.isdir(os.path.join(candidate, 'upstream', 'emscripten')):
            return candidate
    return None


def emsdk_env(emsdk):
    script = 'emsdk_env.bat' if platform.system() == 'Windows' else 'emsdk_env.sh'
    path = os.path.join(emsdk, script)
    if platform.system() == 'Windows':
        out = subprocess.run(f'call "{path}" >nul 2>&1 && set', shell=True, capture_output=True, text=True, check=True).stdout
    else:
        out = subprocess.run(['bash', '-c', f'source "{path}" >/dev/null 2>&1 && env'], capture_output=True, text=True, check=True).stdout
    env = {}
    for line in out.splitlines():
        if '=' in line:
            k, v = line.split('=', 1)
            env[k] = v
    return env


def version():
    with open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8') as f:
        for line in f:
            if line.startswith('project(OTKGlobeEngine VERSION '):
                return line.split()[2]
    raise SystemExit('version not found in CMakeLists.txt')


def main():
    ap = argparse.ArgumentParser(description='Build the WebAssembly engine and the npm package in bindings/web/dist')
    ap.add_argument('--build-dir', default=os.path.join(ROOT, 'build', 'web'))
    ap.add_argument('--pack', action='store_true', help='also run npm pack')
    args = ap.parse_args()

    emsdk = find_emsdk()
    if not emsdk:
        print('Installing the Emscripten SDK into ~/emsdk')
        emsdk = os.path.expanduser('~/emsdk')
        subprocess.run(['git', 'clone', '--depth', '1', 'https://github.com/emscripten-core/emsdk.git', emsdk], check=True)
        tool = os.path.join(emsdk, 'emsdk.bat' if platform.system() == 'Windows' else 'emsdk')
        subprocess.run([tool, 'install', 'latest'], check=True)
        subprocess.run([tool, 'activate', 'latest'], check=True)
    env = {**os.environ, **emsdk_env(emsdk)}
    emcmake = 'emcmake.bat' if platform.system() == 'Windows' and shutil.which('emcmake.bat', path=env.get('PATH')) else 'emcmake'
    subprocess.run([emcmake, 'cmake', '-S', ROOT, '-B', args.build_dir, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release'],
                   check=True, env=env, shell=platform.system() == 'Windows')
    subprocess.run(['cmake', '--build', args.build_dir], check=True, env=env)

    dist = os.path.join(WEB, 'dist')
    shutil.rmtree(dist, ignore_errors=True)
    os.makedirs(dist)
    built = os.path.join(args.build_dir, 'web')
    for name in os.listdir(built):
        if name.startswith('otkglobeengine.'):
            shutil.copy2(os.path.join(built, name), dist)
    for name in ('index.js', 'index.d.ts'):
        shutil.copy2(os.path.join(WEB, 'src', name), dist)
    shutil.copy2(os.path.join(ROOT, 'LICENSE'), WEB)

    v = version()
    pkg = os.path.join(WEB, 'package.json')
    with open(pkg, encoding='utf-8') as f:
        text = f.read()
    import json
    data = json.loads(text)
    if data.get('version') != v:
        data['version'] = v
        with open(pkg, 'w', encoding='utf-8', newline='\n') as f:
            f.write(json.dumps(data, indent=2) + '\n')

    sizes = {n: os.path.getsize(os.path.join(dist, n)) for n in sorted(os.listdir(dist))}
    for n, s in sizes.items():
        print(f'{n:28} {s / 1048576:7.2f} MB')
    if args.pack:
        subprocess.run(['npm', 'pack', '--pack-destination', os.path.join(ROOT, 'build')], cwd=WEB, check=True,
                       shell=platform.system() == 'Windows')
    return 0


if __name__ == '__main__':
    sys.exit(main())
