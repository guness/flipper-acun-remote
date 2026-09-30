#!/usr/bin/env python3
"""Build and install Acun Remote over USB with a repository-local uFBT setup."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from urllib.request import Request, urlopen
import venv

ROOT = Path(__file__).resolve().parents[1]
UFBT_VERSION = '0.2.6'
INDEXES = {
    'official': 'https://update.flipperzero.one/firmware/directory.json',
    'unleashed': 'https://up.unleashedflip.com/directory.json',
    'momentum': 'https://up.momentum-fw.dev/firmware/directory.json',
}
ROGUEMASTER_REPO = 'RogueMaster/flipperzero-firmware-wPlugins'
# Firmware configuration loads these app manifests even for an external build.
ROGUEMASTER_PATHS = [
    'applications/main', 'applications/services', 'applications/settings',
    'applications/system', 'applications/drivers', 'applications/debug',
    'applications/examples', 'applications/external/dab_timer',
    'applications/external/ir_remote', 'applications/external/subghz_remote',
    'applications/external/subghz_playlist', 'furi', 'lib', 'targets',
    'scripts', 'site_scons', 'assets/icons', 'assets/dolphin/internal',
    'assets/dolphin/blocking',
]


def write_metadata(binary, metadata):
    metadata.update(
        fap_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        fap_bytes=binary.stat().st_size,
        hardware_tested=False,
    )
    (binary.parent / 'build_info.json').write_text(json.dumps(metadata, indent=2) + '\n')


def build_roguemaster(app, cache, tag, build_only):
    if not shutil.which('git'):
        raise ValueError('RogueMaster builds require Git. Install Git and try again.')
    if not tag:
        headers = {'User-Agent': 'acun-remote-installer'}
        token = os.environ.get('GH_TOKEN') or os.environ.get('GITHUB_TOKEN')
        if token:
            headers['Authorization'] = f'Bearer {token}'
        request = Request(f'https://api.github.com/repos/{ROGUEMASTER_REPO}/releases/latest',
                          headers=headers)
        with urlopen(request, timeout=30) as response:
            tag = json.load(response)['tag_name']
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', tag):
        raise ValueError('Invalid RogueMaster tag; use its release tag, such as RM0819-2255-b3dd8981.')
    source = cache / 'roguemaster' / tag
    if not (source / '.git').is_dir():
        source.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', 'clone', '--depth', '1', '--filter=blob:none', '--sparse',
                        '--branch', tag, f'https://github.com/{ROGUEMASTER_REPO}.git',
                        str(source)], check=True)

    def git(*arguments, **kwargs):
        return subprocess.run(['git', *arguments], cwd=source, check=True, **kwargs)

    git('sparse-checkout', 'set', *ROGUEMASTER_PATHS)
    modules = git('config', '--file', '.gitmodules', '--get-regexp', r'\.path$',
                  capture_output=True, text=True).stdout
    paths = [line.split(maxsplit=1)[1] for line in modules.splitlines()]
    paths = [path for path in paths if path.startswith('lib/') or path == 'assets/protobuf']
    if not paths:
        raise ValueError('RogueMaster core submodules were not found.')
    git('submodule', 'update', '--init', '--depth', '1', '--jobs', '4', '--', *paths)
    revision = git('rev-parse', 'HEAD', capture_output=True, text=True).stdout.strip()
    tagged_revision = git('rev-parse', f'refs/tags/{tag}^{{commit}}',
                          capture_output=True, text=True).stdout.strip()
    if revision != tagged_revision:
        raise ValueError(f'Cached RogueMaster checkout no longer matches {tag}.')

    destination = source / 'applications_user/acun_remote'
    # This is our generated copy, not the user's app source or firmware checkout.
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(app, destination, ignore=shutil.ignore_patterns('dist', '.vscode', '.ufbt'))
    env = os.environ.copy()
    env.update(FBT_NO_SYNC='1', FBT_TOOLCHAIN_PATH=str(cache))
    fbt = ['cmd', '/c', 'fbt.cmd'] if os.name == 'nt' else ['./fbt']

    def run(*arguments):
        subprocess.run(fbt + list(arguments), cwd=source, env=env, check=True)

    run('fap_acun_remote')
    built = source / 'build/f7-firmware-C/.extapps/acun_remote.fap'
    binary = app / 'dist/acun_remote.fap'
    binary.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(built, binary)
    symbols = source / 'targets/f7/api_symbols.csv'
    write_metadata(binary, {
        'firmware': 'roguemaster',
        'build_system': 'fbt',
        'firmware_tag': tag,
        'firmware_commit': revision,
        'firmware_repository': f'https://github.com/{ROGUEMASTER_REPO}',
        'api_symbols_sha256': hashlib.sha256(symbols.read_bytes()).hexdigest(),
    })
    if build_only:
        print(f'Built {binary} for RogueMaster {tag}')
    else:
        print('Installing over USB. Close qFlipper and any serial terminal first.', flush=True)
        run('launch', 'APPSRC=acun_remote')
        print('Installed and launched Acun Remote.')
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', required=True, choices=[*INDEXES, 'roguemaster'],
                        help='firmware installed on your Flipper')
    parser.add_argument('--sdk', type=Path,
                        help='use a local SDK zip matching your firmware instead of latest release')
    parser.add_argument('--build-only', action='store_true',
                        help='build without connecting to or installing on a device')
    parser.add_argument('--roguemaster-tag', help='exact RogueMaster release tag (default: latest)')
    parser.add_argument('--app-dir', type=Path, default=ROOT / 'flipper_apps/acun_remote',
                        help='app source directory (used by CI to build an existing release tag)')
    args = parser.parse_args(argv)
    if args.sdk and not args.sdk.is_file():
        parser.error(f'SDK file does not exist: {args.sdk}')
    if args.roguemaster_tag and (args.firmware != 'roguemaster' or args.sdk):
        parser.error('--roguemaster-tag requires --firmware roguemaster without --sdk')

    app = args.app_dir.resolve()
    if not (app / 'application.fam').is_file():
        parser.error(f'App manifest not found in {app}')
    cache = ROOT / '.install'
    if args.firmware == 'roguemaster' and not args.sdk:
        return build_roguemaster(app, cache, args.roguemaster_tag, args.build_only)
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
    }
    write_metadata(binary, metadata)
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
