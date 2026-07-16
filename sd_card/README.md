# SD card package

## Contents

- `USB_IMG/MANTISUSB_500MB.img.zip`
- `MantisSD/Payloads/`
- `MantisSD/Settings/`
- `MantisSD/Layouts/`

## Recommended 500 MiB setup

1. Extract `USB_IMG/MANTISUSB_500MB.img.zip`.
2. Copy `MANTISUSB.IMG` to the root of a physical microSD card formatted as one MBR FAT32 partition.
3. Insert the card into the Cardputer ADV.
4. Connect the Cardputer ADV to the computer.
5. The supplied image already contains the packaged `MantisSD` directory. The separate `MantisSD` folder in this repository can be used to restore or update its contents.
6. After changing payloads, settings, or layouts, open **USB Drive**, press **R**, and eject **MANTISUSB** from the computer when prompted.

> **IMPORTANT: IF YOU ARE USING THE RAW SD RELEASE VARIANT WITHOUT `.IMG`, IGNORE THE IMAGE-COPY STEPS ABOVE.**

The officially tested and recommended IMG capacity for V1.0 is **500 MiB**.
