Download the app for the firmware installed on your Flipper Zero:

- **Official firmware:** `acun_remote-official.fap`
- **Unleashed:** `acun_remote-unleashed.fap`

Rename the downloaded file to `acun_remote.fap`. In qFlipper, open the file
manager and upload it to `SD Card/apps/Sub-GHz/`. You can also copy it there
with an SD card reader. Open **Apps → Sub-GHz → Acun Remote** on the Flipper.

Each build uses that firmware's release SDK at build time. The corresponding
`build_info-*.json` records the SDK and binary hash; `SHA256SUMS` covers all
attached binaries and metadata. If you see an API mismatch, use a matching
firmware release or build with the SDK for your installed firmware.

For the one-command USB installer and usage instructions, see the README in
the source archive for this tag. Changes are listed in `CHANGELOG.md`.

Radio behaviour on hardware remains unverified. Use only with remotes and
receivers you own.
