# Flip Autocatch

A Pokemon GO Plus emulator for the Flipper Zero. The Flipper pairs with Pokemon GO
as a "Pokemon GO Plus" accessory and presses the button for you, catching Pokemon
and spinning Pokestops while the app is open.

It is a port of the ESP32 [pgpemu](https://github.com/spezifisch/pgpemu) project
to the Flipper's BLE stack. Like pgpemu, it needs the secrets (MAC address, device
key and blob) dumped from **your own** Pokemon GO Plus. Without them the phone will
not accept the device.

> **Status:** builds against the official firmware (1.5.1), Momentum and Unleashed, and the
> protocol code is unit tested against pgpemu. **It has not been tested on
> hardware yet.** See [docs/PLAN.md](docs/PLAN.md) for the bring-up checklist and
> known risks.
>
> Using accessory emulators may violate the Pokemon GO Terms of Service. Use at
> your own risk.

## Building

Install [uFBT](https://github.com/flipperdevices/flipperzero-ufbt) and build:

```bash
pip install ufbt
ufbt update            # official firmware SDK (release channel)
ufbt                   # builds dist/autocatch.fap
ufbt launch            # build, install and start on a connected Flipper
```

For Momentum or Unleashed, point ufbt at their SDK index first:

```bash
# Momentum
ufbt update --index-url=https://up.momentum-fw.dev/firmware/directory.json --channel=release
# Unleashed
ufbt update --index-url=https://up.unleashedflip.com/directory.json --channel=release
ufbt
```

A `.fap` is built for the firmware you run, so the API version has to match. Use
`--channel=dev` if you run a dev build.

GitHub Actions (`.github/workflows/build.yml`) builds the app for Momentum and
Unleashed (release and dev channels) on every push, and runs the host tests. Each
run's **Artifacts** section has a ready `.fap` per firmware. Dev-channel failures
are reported but do not fail the run.

On Windows use `python -m ufbt ...` if `ufbt` is not on your `PATH`.

## Device secrets

The app reads your dump from the SD card at
`/ext/apps_data/autocatch/pgp_device.txt`. It is a Flipper Format text file:

```text
Filetype: Flip Autocatch PGP
Version: 1
Name: My PGP
MAC: 98 B6 E9 12 34 56
Device Key: 00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF
Blob: 00 01 02 ... (256 bytes on one line)
```

`MAC` is in the usual display order (as printed by nRF Connect or pgpemu).
`tools/make_secrets.py` creates the file from a C header with byte arrays (like the
old `pgp_device.h`), from pgpemu's `secrets.c`/`secrets.yaml`, or from hex strings:

```bash
python3 tools/make_secrets.py pgp_device.h --name "My PGP" -o pgp_device.txt
```

Copy `pgp_device.txt` to `apps_data/autocatch/` with qFlipper or the mobile app.
Keep it out of git; `.gitignore` already excludes it.

## Usage

1. Start **PoGo Autocatch** (Apps > Bluetooth). The Flipper advertises as
   "Pokemon GO Plus" using your device's MAC address.
2. In Pokemon GO open *Settings > Connected Devices and Services > Pokemon GO Plus*
   and pick the device.
3. Confirm the 6-digit pairing code on the phone and on the Flipper. This is needed
   only the first time; the bond is stored in `apps_data/autocatch/.pgp.keys`.
4. The status line goes through *Handshake 1/3 to 3/3* to *Connected*.

The app has to stay open; Flipper apps do not run in the background. The screen can
turn off.

| Button | Action                               |
|--------|--------------------------------------|
| Left   | Toggle auto catch                    |
| Right  | Toggle auto spin                     |
| OK     | Press the PGP button now             |
| Back   | Disconnect, restore Bluetooth, exit  |

The screen shows the last thing the phone signalled (Pokemon nearby, caught, fled,
bag full, and so on) and counters for this session. Like pgpemu, the button is
pressed after a random 1.0-2.5 s delay with a random press length.

## Troubleshooting

Watch the logs while pairing: run `ufbt cli`, then `log` (set *Settings > System >
Log Level* to *Info* or *Debug* first). The interesting tags are `Autocatch`,
`PgpProfile`, `BleGap` and `GattChar`. [docs/PLAN.md](docs/PLAN.md#hardware-bring-up)
lists what each stage should log and what to try when it does not.

## Development

```text
autocatch.c       app: UI, event loop, Bluetooth lifecycle, auto press
pgp_profile.c     BLE profile: GATT services, advertising, GATT write events
pgp_handshake.c   certificate handshake state machine (pure C)
pgp_cert.c        handshake crypto, AES-128 based (pure C)
pgp_led.c         LED pattern parsing and button press patterns (pure C)
pgp_secrets.c     loads the device secrets from the SD card
aes.c             tiny-AES-c (ECB only)
tests/            host tests for the pure C parts
tools/            secrets file converter
```

Run the host tests with `make -C tests`.

## Credits

The protocol work is by Yohanes Nugroho
([reverse engineering write-up](https://tinyhack.com/2018/11/21/reverse-engineering-pokemon-go-plus/),
[pgpemu](https://github.com/yohanes/pgpemu)) and
[spezifisch's pgpemu fork](https://github.com/spezifisch/pgpemu). See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
