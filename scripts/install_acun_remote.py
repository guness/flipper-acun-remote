#!/usr/bin/env python3
"""Build and install Acun Remote over USB with a repository-local uFBT setup."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import venv

ROOT = Path(__file__).resolve().parents[1]
UFBT_VERSION = '0.2.6'
INDEXES = {
    'official': 'https://update.flipperzero.one/firmware/directory.json',
    'unleashed': 'https://up.unleashedflip.com/directory.json',
}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', required=True, choices=INDEXES,
                        help='firmware installed on your Flipper')
    parser.add_argument('--sdk', type=Path,
                        help='use a local SDK zip matching your firmware instead of latest release')
    parser.add_argument('--build-only', action='store_true',
                        help='build without connecting to or installing on a device')
    args = parser.parse_args(argv)
    if args.sdk and not args.sdk.is_file():
        parser.error(f'SDK file does not exist: {args.sdk}')

    app = ROOT / 'flipper_apps/acun_remote'
    cache = ROOT / '.install'
    python = cache / 'venv' / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')
    if not python.exists():
        venv.EnvBuilder(with_pip=True).create(cache / 'venv')
    subprocess.run([str(python), '-m', 'pip', 'install', f'ufbt=={UFBT_VERSION}'], check=True)

    env = os.environ.copy()
    env['UFBT_HOME'] = str(cache / args.firmware)
    env['FBT_TOOLCHAIN_PATH'] = str(cache)
    ufbt = [str(python), '-m', 'ufbt']

    def run(*arguments, **kwargs):
        return subprocess.run(ufbt + list(arguments), cwd=app, env=env, check=True, **kwargs)

    if args.sdk:
        run('update', '--local', str(args.sdk.resolve()))
    else:
        run('update', '--index-url', INDEXES[args.firmware], '--channel', 'release')
    run()
    status = json.loads(run('status', '--json', capture_output=True, text=True).stdout)
    binary = app / 'dist/acun_remote.fap'
    metadata = {
        'firmware': args.firmware,
        'ufbt_version': UFBT_VERSION,
        'sdk_status': status,
        'fap_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'fap_bytes': binary.stat().st_size,
        'hardware_tested': False,
    }
    (binary.parent / 'build_info.json').write_text(json.dumps(metadata, indent=2) + '\n')
    if args.build_only:
        print(f'Built {binary}')
    else:
        print('Installing over USB. Close qFlipper and any serial terminal first.', flush=True)
        run('launch')
        print('Installed and launched Acun Remote.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f'Installation/build failed: {error}', file=sys.stderr)
        sys.exit(1)
