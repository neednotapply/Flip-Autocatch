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

If you are on Windows and `ufbt` is not found, ensure Python's Scripts
directory is in your `PATH` or prefix the commands with `python -m`,
e.g.:

```cmd
pip install ufbt
python -m ufbt update --index-url https://up.momentum-fw.dev/firmware/directory.json --channel dev
python -m ufbt build
```

Windows `cmd.exe` does not support the backslash (`\`) line continuation
used above, so run the commands on a single line as shown.

Building against the official Flipper firmware will now trigger a compile-
time error. The application depends on the extra Bluetooth helpers from the
Momentum SDK and will not compile without them. Make sure the Momentum
firmware index is installed with `ufbt update` before running `ufbt build`.

By default the compiled `.fap` file is written under the uFBT state
directory, usually `~/.ufbt/build` unless you created a local environment
with `ufbt dotenv_create` (then it will be `.ufbt/build` in your project).
To keep the artifact in your working folder set `UFBT_HOME` to `.` while
building:

```cmd
set UFBT_HOME=. && python -m ufbt build
```

On Unix-like shells use:

```bash
UFBT_HOME=. ufbt build
```

### Custom Icon

If you want an icon for the application, place a 10x10 1-bit PNG named
`autocatch.png` in the project root. The build will work without it.

## Features

* Basic Bluetooth pairing dialog
* Example notification using the Flipper's LED/buzzer
* Automatically presses the catch button when the game requests input

This is only a starting point for implementing full "Pokemon Go Plus"
functionality.

### Device Credentials

The file `pgp_device.h` contains the MAC address, device key, and blob
used when emulating a Pokemon Go Plus. Replace these constants with the
values from your own hardware if needed.

### Pairing

1. Launch **Autocatch** on your Flipper and press the `Pair` button.
2. A "Pairing" screen will remain open while the device advertises over BLE.
3. On your phone open *Pokemon Go* ➡ *Accessory Devices* and select
   "Pokemon GO Plus" from the list.
4. After the connection succeeds press `Back` on the Flipper to exit the
   pairing screen.
5. While connected, the app automatically taps the virtual catch button
   whenever Pokemon Go prompts for input.
