#!/usr/bin/env python3
"""Build using an existing firmware SDK ZIP/toolchain, without modifying firmware.

Example:
python3 scripts/build_acun_remote.py \
  --sdk ../unleashed-firmware/dist/f7-C/flipper-z-f7-sdk-local.zip \
  --toolchain ../unleashed-firmware/toolchain/current
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', required=True, type=Path)
    parser.add_argument('--toolchain', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    app = root / 'flipper_apps/acun_remote'
    sdk = args.sdk.resolve()
    toolchain = args.toolchain.resolve()
    with tempfile.TemporaryDirectory(prefix='acun-remote-sdk-') as temp:
        state = Path(temp).resolve()
        current = state / 'current'
        current.mkdir()
        with zipfile.ZipFile(sdk) as archive:
            # Reject archive paths escaping the temporary extraction directory.
            for name in archive.namelist():
                if not (current / name).resolve().is_relative_to(current):
                    raise ValueError(f'Unsafe archive member: {name}')
            archive.extractall(current)
        (current / 'ufbt_state.json').write_text('{}')
        scripts = current / 'scripts/ufbt'
        env = os.environ.copy()
        env.update(UFBT_STATE_DIR=str(state), UFBT_SCRIPT_DIR=str(scripts),
                   FBT_TOOLCHAIN_PATH=str(toolchain.parent.parent))
        env['PATH'] = str(toolchain / 'bin') + os.pathsep + env.get('PATH', '')
        subprocess.run([str(toolchain / 'bin/python3'), '-m', 'SCons', '-Q',
                        '-C', str(scripts), f'UFBT_APP_DIR={app}'], env=env, check=True)
        binary = app / 'dist/acun_remote.fap'
        metadata = {'sdk': sdk.name, 'sdk_sha256': hashlib.sha256(sdk.read_bytes()).hexdigest(),
                    'components': json.loads((current/'components.json').read_text()),
                    'fap_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                    'fap_bytes': binary.stat().st_size,
                    'hardware_tested': False}
        (app / 'dist/build_info.json').write_text(json.dumps(metadata, indent=2) + '\n')
        print(f'Built {binary}')


if __name__ == '__main__':
    main()
