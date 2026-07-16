# Storage modes

## IMG mode

IMG mode is the source-available and recommended build. It locates `MANTISUSB.IMG`, reads the file size, validates sector alignment, validates the MBR and FAT32 partition, and publishes the detected sector count through MSC.

V1.0 officially validates only the supplied 500 MiB image. Other generated sizes are configurable but uncertified.

The physical card must use one MBR FAT32 partition and `MANTISUSB.IMG` must be in its root directory.

## RAW SD mode

RAW SD mode will expose the physical card directly and will be released separately as a precompiled asset. It is not interchangeable with IMG mode because safe internal access and eject behavior require a different backend.
