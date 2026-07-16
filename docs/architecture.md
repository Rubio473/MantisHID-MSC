# Architecture

MantisHID+MSC combines four USB interfaces in one ESP32-S3 device:

- Keyboard HID
- Mouse HID
- Consumer Control HID
- Mass Storage Class

The IMG backend maps sectors from `MANTISUSB.IMG`, which is stored as a normal file on a FAT32 physical microSD card. Payloads and external keyboard layouts are synchronized into internal cache storage so they can be used without concurrent filesystem access while Windows owns the virtual disk.

The public USB name remains `MantisUSB` regardless of image capacity.
