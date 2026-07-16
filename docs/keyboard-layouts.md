# Keyboard layouts

Built-in layouts:

- US
- ES
- LATAM

External layouts are read from:

```text
/MantisSD/Layouts/*.kl
```

Each accepted file must contain exactly 256 bytes. The layout maps printable ASCII characters to HID key usages and modifiers. Unicode coverage is not guaranteed.

After adding or removing layouts, open **USB Drive**, press **R**, and eject `MANTISUSB` when prompted. Save the selected layout in Settings.
