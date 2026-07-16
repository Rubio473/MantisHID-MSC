# Troubleshooting

## `MANTISUSB.IMG` not found

Place `MANTISUSB.IMG` in the root of a physical microSD card containing one MBR FAT32 partition.

## Image rejected

The file must be sector aligned, between 64 MiB and the FAT32 single-file limit, and contain one valid FAT32 partition. Use the included generator.

## External layouts do not appear

Confirm the files are in `/MantisSD/Layouts`, press **R** in USB Drive, eject `MANTISUSB`, and save the layout selection.

## Windows shows an old volume label

Safely remove the device, reconnect it, and clear stale Windows device/volume metadata if necessary. Generated images use a size-derived disk and FAT volume identifier.
