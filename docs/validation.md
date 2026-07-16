# V1.0 validation report

## Passed

- Firmware source, include files, and target configuration were hash-compared against the approved V1.0 package; no firmware file changed during the MIT/README update.
- PlatformIO resolved the root configuration and selected `m5stack-cardputer-adv` as the default environment.
- Target-specific partitions and include paths resolved through `targets/m5stack-cardputer-adv/platformio.ini`.
- `SDWholeCardStorage.cpp` passed a host-side C++ syntax check after the runtime image-size detection changes.
- The image generator passed Python syntax validation.
- The generator successfully created and verified 64 MiB, 256 MiB, and 500 MiB FAT32 images.
- The packaged 500 MiB image contains one MBR FAT32 partition, label `MANTISUSB`, a size-derived disk/volume identifier, and the `MantisSD` directory.
- All 30 bundled `.kl` files are exactly 256 bytes.
- All five mixed regression payloads passed DuckyScript 3 Core compilation, payload safety validation, and host-simulated classic parser execution.
- The root MIT License text was installed and reviewed.
- The release archive contains no `.pio` build directory or editor cache.

## Hardware evidence

The five mixed regression payloads were executed on the user's Cardputer ADV. Visible text output, volume changes, media actions, mouse movement/clicks, loops, variables, functions, conditions, and DuckyScript 1.x extensions behaved as expected.

## Not completed in this environment

A complete PlatformIO firmware build could not be completed because the isolated build environment could not resolve `github.com` to download the PIOArduino platform and M5Cardputer dependency. The project configuration itself was resolved successfully.

## License boundary requiring maintainer confirmation

The `.kl` files are separate third-party data and are not covered by the root MIT license. Their exact provenance and redistribution notice should be confirmed before making the GitHub repository public. The repository includes a conservative GPL-3.0 notice and does not relicense those files.
