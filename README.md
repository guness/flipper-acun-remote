# Flipper Acun Remote

A Flipper Zero app that learns a button of your own Acun gate or garage remote
and sends the next code on demand, so the Flipper can act as a spare remote.
Sub-GHz, 433.92 MHz, internal radio.

These remotes don't repeat a fixed code: each press sends a new value from a
counter. The app listens to five presses of one button to learn how that
counter moves, stores it on the SD card, and advances it on every send.

## Features

- **Read**: learn a new button from five presses, or bring a saved button up
  to date from a single press when the original remote has been used since.
- **Saved**: named remotes with numbered buttons ("Garage B1", "Garage B2").
  Send, Info, Sync from a `.sub` file, Rename, Delete.
- **Send**: hold OK to transmit. Each hold sends one new code.
- The next counter value is written to the SD card and read back before the
  radio starts, so an interrupted send never reuses a code.

See the [user guide](docs/user-guide.md) for step-by-step use.

## Install manually

Acun Remote is distributed through
[GitHub Releases](https://github.com/guness/flipper-acun-remote/releases), outside
the official app catalog. No compiled app is stored in this repository.

1. Open a release and download the file matching your installed firmware:
   `acun_remote-official.fap` for official firmware or
   `acun_remote-unleashed.fap` for Unleashed.
2. Rename it to `acun_remote.fap`.
3. Connect your Flipper by USB and open qFlipper's file manager. Upload the file
   to **SD Card → apps → Sub-GHz** (create the folder if needed). Alternatively,
   copy it to `apps/Sub-GHz/` using an SD card reader.
4. On the Flipper, open **Apps → Sub-GHz → Acun Remote**.

To update, close the app and replace the same `.fap` file. Keep the app's data
folder to preserve saved remotes. If the Flipper reports an API mismatch,
check the release's `build_info-*.json` for the SDK version and use a matching
firmware, or build with your installed firmware's SDK.

## One-command USB install

Download and extract this repository's source archive (or clone it). Install
Python 3.9 or newer, connect your Flipper over USB with its SD card inserted,
and close qFlipper and any serial terminal. From the repository root, run:

```sh
python3 scripts/install_acun_remote.py --firmware official
```

For Unleashed, use `--firmware unleashed`. On Windows, replace `python3` with
`py -3`. On Debian/Ubuntu, install `python3-venv` if Python reports that venv or
ensurepip is unavailable. Linux users also need permission to access the
Flipper's USB serial port.

The script installs [uFBT](https://github.com/flipperdevices/flipperzero-ufbt)
and downloads the selected firmware's latest release SDK/toolchain into the
ignored `.install/` folder, builds this checkout, then uploads and starts the
app. It installs the app only; it does not update your firmware. The first run
needs internet access and can take several minutes.

For older or custom firmware, supply its SDK zip:

```sh
python3 scripts/install_acun_remote.py --firmware unleashed --sdk /path/to/matching-sdk.zip
```

## Build

Build without connecting a device:

```sh
python3 scripts/install_acun_remote.py --firmware official --build-only
```

Use `--firmware unleashed` for Unleashed, or add `--sdk /path/to/sdk.zip` to
select an exact SDK. Outputs are `flipper_apps/acun_remote/dist/acun_remote.fap`
and `dist/build_info.json`; the entire `dist/` directory is ignored by Git.
`application.fam` stays in the source because it is the required build manifest.

If you already have the sibling firmware checkout and its toolchain, the
existing local build helper avoids downloading them:

```sh
python3 scripts/build_acun_remote.py \
  --sdk ../unleashed-firmware/dist/f7-C/flipper-z-f7-sdk-local.zip \
  --toolchain ../unleashed-firmware/toolchain/current
```

## Publish a release

The [release workflow](.github/workflows/release.yml) runs for every pushed tag.
It runs the host and installer tests, builds against both official and Unleashed
release SDKs, and publishes a GitHub Release after both builds succeed. Assets
include both `.fap` files, SDK/build metadata, and SHA-256 checksums. If a build
fails, fix the failure and rerun the workflow before a release is published.

Update `fap_version` in `application.fam` and [CHANGELOG.md](CHANGELOG.md), commit
the changes, then tag that commit and push the tag, for example:

```sh
git tag v1.0
git push origin v1.0
```

The workflow uses GitHub's built-in token; no extra publishing secret is needed.
GitHub Actions must be enabled for the repository. The workflow must be included
in the tagged commit. Avoid pushing more than three tags at once, because GitHub
does not emit tag-push workflow events for those batches.

## Test

```sh
python3 tests/acun_remote/run.py
python3 -m unittest discover -s tests/scripts -v
```

Needs Python 3 and a C compiler (`cc`), nothing else. The core logic
(`sequence_core.c`, `remote_name.c`) has no firmware dependencies, so the tests
compile it for your computer and run it against recorded presses in
`tests/acun_remote/data/`: five consecutive presses per remote and button, with
the expected values in `expected.json`.

Keep C sources formatted using the application's `.clang-format` configuration.

## Layout

| Path | Contents |
|---|---|
| `flipper_apps/acun_remote/` | App source, build manifest and icon |
| `flipper_apps/acun_remote/scenes/` | One file per screen |
| `docs/user-guide.md`, `docs/screenshots/` | User guide and screenshots |
| `CHANGELOG.md` | Version history |
| `.github/workflows/release.yml` | Tag-triggered tests, builds and releases |
| `docs/acun_remote.md` | Technical notes: storage format, framing, timing, test coverage |
| `scripts/` | Build and USB install scripts |
| `tests/acun_remote/` | Host tests and recorded fixtures |

## Status

Host tests cover recorded samples from a limited number of remotes. Radio
behaviour on hardware remains unverified. Use it only with remotes and receivers you own.

## License

Copyright (C) 2026 guness. Licensed under the GNU General Public License v3.0; see [LICENSE](LICENSE).
