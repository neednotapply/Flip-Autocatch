#Flip - Autocatch
A "Pokemon Go Autocatcher" for the Flipper Zero.

## Building

This project uses the [uFBT](https://github.com/flipperdevices/ufbt) toolchain.
The app relies on the Momentum SDK, which extends the official Flipper
firmware with additional Bluetooth APIs. Before building you need to update
ufbt to use the Momentum firmware index. To build the application locally:

```bash
pip install ufbt
ufbt update \
  --index-url https://up.momentum-fw.dev/firmware/directory.json \
  --channel dev
ufbt build
```

If you build against the official Flipper firmware, the Bluetooth spoofing
helpers used by Momentum won't be present. The source provides weak stub
implementations so the app still compiles, but real functionality requires
building with the Momentum SDK.

If you encounter linking errors about functions like furi_hal_bt_set_blob, verify the Momentum firmware index was installed using `ufbt update`.

The resulting `.fap` file will be placed in the build directory.

### Custom Icon

If you want an icon for the application, place a 10x10 1-bit PNG named
`autocatch.png` in the project root. The build will work without it.

## Features

* Basic Bluetooth pairing dialog
* Example notification using the Flipper's LED/buzzer

This is only a starting point for implementing full "Pokemon Go Plus"
functionality.

### Device Credentials

The file `pgp_device.h` contains the MAC address, device key, and blob
used when emulating a Pokemon Go Plus. Replace these constants with the
values from your own hardware if needed.
