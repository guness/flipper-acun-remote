Download the app for the firmware installed on your Flipper Zero:

- **Official firmware:** `acun_remote-official.fap`
- **Unleashed:** `acun_remote-unleashed.fap`
- **Momentum:** `acun_remote-momentum.fap`
- **RogueMaster:** `acun_remote-roguemaster.fap`

Rename the downloaded file to `acun_remote.fap`. In qFlipper, open the file
manager and upload it to `SD Card/apps/Sub-GHz/`. You can also copy it there
with an SD card reader. Open **Apps → Sub-GHz → Acun Remote** on the Flipper.

Official, Unleashed, and Momentum builds use their release SDKs at build time.
RogueMaster is built from its latest tagged firmware source using `fbt`.
The corresponding `build_info-*.json` records the SDK or firmware tag/commit
and binary hash; `SHA256SUMS` covers all
attached binaries and metadata. If you see an API mismatch, use a matching
firmware release or build against the SDK/source for your installed firmware.

For the current one-command USB installer and usage instructions, see the
[repository README](https://github.com/guness/flipper-acun-remote#one-command-usb-install).
The app source comes from this release tag; release tooling can be updated to
add firmware targets without moving the tag. Changes are listed in `CHANGELOG.md`.

Radio behaviour on hardware remains unverified. Use only with remotes and
receivers you own.
