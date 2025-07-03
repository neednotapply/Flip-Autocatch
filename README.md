#Flip - Autocatch
A "Pokemon Go Autocatcher" for the Flipper Zero.

## Building

This project uses the [uFBT](https://github.com/flipperdevices/ufbt) toolchain.
To build the application locally:

```bash
pip install ufbt
ufbt build
```

The resulting `.fap` file will be placed in the build directory.

### Custom Icon

If you want an icon for the application, place a 10x10 1-bit PNG named
`autocatch.png` in the project root. The build will work without it.

## Features

* Basic Bluetooth pairing dialog
* Example notification using the Flipper's LED/buzzer

This is only a starting point for implementing full "Pokemon Go Plus"
functionality.
