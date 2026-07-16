# Third-party notices

The MIT License in the repository root applies to original MantisHID+MSC source code and documentation unless a file or directory states otherwise.

## Build dependencies

The project downloads external dependencies during the PlatformIO build. Those dependencies are not relicensed by this repository.

- PIOArduino / Arduino-ESP32
- M5Stack M5Cardputer and M5Unified libraries
- TinyUSB components distributed through the selected platform package

Consult the exact dependency revisions downloaded by PlatformIO for their authoritative license texts.

## Keyboard layout data

The binary `.kl` keyboard-layout files under `sd_card/MantisSD/Layouts/` were imported from the user-supplied `sdcard.zip` associated with the ElicoftZ **Flipper-Zero-meets-M5Stack-Cardputer** project. That package is based on Flipper Zero BadUSB resources. The official Flipper Zero firmware repository is GPL-3.0 licensed.

These `.kl` files are treated as separate third-party data and are **not relicensed under MIT**. A copy of GPL-3.0 is provided in `LICENSES/GPL-3.0-only.txt` for reference. Before a public release, the maintainer should confirm the exact provenance and license notice for the specific layout archive being redistributed.

## Product image

The root README links to an official M5Stack Cardputer ADV product image hosted by M5Stack. The image is not included in this archive and remains the property of M5Stack.

## Project names and trademarks

M5Stack, Cardputer, Flipper Zero, Hak5, Rubber Ducky, Windows, Linux, and macOS are names or trademarks of their respective owners. Their mention does not imply endorsement.
