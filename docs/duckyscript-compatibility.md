# DuckyScript compatibility

## Classic DuckyScript 1.x and supported extensions

Supported areas include comments, delays, strings, string delays, repeat, keyboard combinations, left/right modifiers, function keys, navigation keys, keypad keys, hold/release, USB waits, Caps Lock waits, Cardputer button waits, Alt codes, SysRq, Mouse HID, Consumer HID, and external `.kl` keyboard layouts.

## Mantis DuckyScript 3 Core

The integrated Core supports:

- `VAR` and `DEFINE`
- assignments such as `$COUNT = ($COUNT + 1)`
- integer and quoted string values
- arithmetic and modulo
- comparisons
- logical operators
- `IF`, `ELSE_IF`, `ELSE`, and `END_IF`
- `WHILE` and `END_WHILE`
- `FUNCTION`, `END_FUNCTION`, calls, and `RETURN`
- variable expansion inside classic commands

The interpreter applies limits to variables, functions, nesting, loop iterations, function calls, and compiled output size.

Use this public description:

> Compatible with classic DuckyScript 1.x, supported Flipper BadUSB-style extensions, and Mantis DuckyScript 3 Core.

Do not describe V1.0 as complete Hak5 DuckyScript 3 compatibility.
