# Acun Remote

Learn a button of your own Acun gate or garage remote and use Flipper Zero as a
spare. The app listens to five presses of one button, then sends the next code
each time you press OK. It uses the internal radio at 433.92 MHz.

Each saved button keeps its own counter on the SD card. The counter is saved
before every transmission, so the remote and the Flipper stay in step.

## Read

Open **Read** and hold a button on your remote until the Flipper beeps.

- **New button**: press the same button five times, releasing it between presses.
  Then pick a remote name and a button number (1 to 8).
- **Known button**: the app shows how far the remote is ahead of the saved state.
  Choose **Sync** to catch up.

## Saved

All learned buttons in one list, for example "Garage B1", "Garage B2". Open one for:

- **Send**: hold OK to transmit. Each hold sends one new code.
- **Info**: counter, step and timing details.
- **Sync from file**: catch up from a RAW or BinRAW .sub recording of the same button.
- **Rename** and **Delete**.

## Notes

- Use it only with remotes and receivers you own.
- Pressing the original remote moves it ahead of the Flipper. If the gate stops
  responding, use **Read** on the original remote and **Sync**.
- Transmission follows your firmware's regional frequency rules.
- Tested on a limited number of remotes. Behaviour with other receivers may differ.
