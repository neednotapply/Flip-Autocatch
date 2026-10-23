# Flip Autocatch plan

## Where things stand

The previous code (`pgp.c`, `pgp_device.h`) did not match the Pokemon GO Plus (PGP)
protocol. It exposed one made-up characteristic, put part of the challenge into
manufacturer data, used PIN-entry pairing, and called `furi_hal_bt_start_app()`
directly behind the Bt service's back. It also no longer compiled against current
firmware. It has been replaced by a port of
[pgpemu](https://github.com/spezifisch/pgpemu), the ESP32 emulator that is known to
work.

What exists now:

- A BLE profile that exposes the same GATT layout as a real PGP (battery,
  LED/button and certificate services) and advertises as "Pokemon GO Plus" with
  your device's MAC address.
- The certificate handshake and its crypto, checked byte for byte against
  pgpemu's own code (2,000 random vectors), plus host unit tests.
- LED pattern parsing (Pokemon / Pokestop / caught / fled / bag full / ...) and
  pgpemu's randomized auto-press.
- Secrets loaded from the SD card instead of being compiled in.
- A clean build against official firmware 1.5.1 (API 88.2), Momentum (API 87.1)
  and Unleashed (dev, Oct 2026), with every imported symbol checked against each
  firmware's API table.
- GitHub Actions building `.fap` artifacts for Momentum and Unleashed (release and
  dev) and running the host tests on every push; version tags publish a release.

Nothing has run on a Flipper yet. Milestone 1 below is the next step.

## How the protocol works

The phone talks to three GATT services:

| Service | Characteristic | Use |
|---|---|---|
| Battery `180F` | Level | Flipper's battery service |
| LED/button `21c50462-67cb-63a3-5c4c-82b5b9939aeb` | `...aec` LED (write) | phone writes LED/vibration patterns |
| | `...aed` Button (notify) | we notify a 2 byte "pressed" bitmap |
| | `...aee`, `...aef`, `...af0` | unknown, update request, firmware version (unused) |
| Certificate `bbe87709-5b89-4433-ab7f-8b8eef0d8e37` | `...e38` CENTRAL_TO_SFIDA (write) | phone to device |
| | `...e39` SFIDA_COMMANDS (notify) | 4 byte "state changed" signals |
| | `...e3a` SFIDA_TO_CENTRAL (read) | device to phone; phone reads after each signal |

Handshake (`pgp_handshake.c`):

| # | Phone | Flipper |
|---|---|---|
| 1 | enables notifications on SFIDA_COMMANDS | publishes the 378 byte first challenge (encrypted with the device key, includes the MAC and blob), notifies `00 00 00 00` |
| 2 | reads it, gets it verified by Niantic, writes 20 bytes | publishes a 52 byte challenge under the new session key, notifies `01 ..` |
| 3 | writes its own 52 byte challenge | decrypts it and publishes the 16 byte answer, notifies `02 ..` |
| 4 | writes a 52 byte confirmation | notifies `04 00 23 00`: connected |

After that the phone writes LED patterns. The colors say what is happening, and a
button notification within the pattern's duration acts as a press.

## Flipper platform constraints

Apps can only use what the firmware exports. Neither the official firmware nor
Momentum exports raw `aci_*` calls, which led to three workarounds:

1. **378 byte characteristic value.** `ble_gatt_characteristic_update()` passes a
   `uint8_t` length to the stack, so it cannot publish the first challenge. The
   serial service's exported `ble_svc_serial_update_tx()` writes in 243 byte chunks
   through `aci_gatt_update_char_value_ext()` and only reads the service handle and
   TX characteristic from its struct. `ble_profile_pgp_set_cert()` passes it a
   look-alike struct that points at our characteristic. This relies on the private
   layout of `BleServiceSerial`, which is identical in official 1.5.1 and current
   Momentum. Proper fix: milestone 4.
2. **Advertising data.** A real PGP advertises its name plus 32-bit service data
   `0x21C50462` and puts the 16-bit UUID `0x00FF` in the scan response. Flipper's
   GAP only supports name + one 16/128-bit UUID, so we advertise name + `0x00FF`.
   If Pokemon GO filters on the service data, the phone will not list the Flipper.
   See risk R2.
3. **Pairing.** pgpemu uses "just works" bonding. Flipper's GAP only sends a security
   request in its PIN modes, so we use numeric comparison: confirm a 6 digit code on
   both sides once. Bonds go to `apps_data/autocatch/.pgp.keys`, separate from the
   Flipper's normal pairings.

Other limits: only one phone at a time (pgpemu supports four), and the app must
stay open.

## Milestones

### M0: Port and build (done)

### M1: Hardware bring-up

Goal: the phone pairs and the handshake reaches *Connected*.

1. Create `pgp_device.txt` with `tools/make_secrets.py` and copy it to
   `/ext/apps_data/autocatch/`. The MAC must be the one your PGP dump came from.
2. Set *Settings > System > Log Level* to *Debug*, run `ufbt launch`, then
   `ufbt cli` and type `log`.
3. Optional sanity check with nRF Connect on the phone: you should see
   "Pokemon GO Plus" with your PGP's MAC, and after connecting, the three services
   above.
4. Pair from Pokemon GO and watch the screen and log:

| Stage | Screen | Log to expect | If it fails |
|---|---|---|---|
| Advertising | Waiting for phone... | `BleGap: Start` | Bluetooth off in settings? |
| Phone lists device | | | R2 (advertising data) |
| Pairing | code dialog | `BleGap: Pairing complete` | R3 |
| Handshake 1/3 | Handshake 1/3 | no `Failed to publish 378 byte certificate` | R1 |
| Handshake 2-3 | Handshake 2/3, 3/3 | no `Unexpected N byte write` | R4, R5 |
| Connected | Connected, green blink | `Autocatch: Handshake complete` | R5 |

5. Walk near a Pokemon or Pokestop. The log should show `LED: Pokemon nearby, N ms`
   and a blue blink on the press.

### M2: Behaviour tuning

- Log real LED patterns from your app version and compare them with `pgp_led.c`.
  pgpemu's heuristics date from 2023; add cases to `tests/test_pgp.c` as you find
  them.
- Tune the press delay and press length if catches are missed.
- Add options such as "catch new species only" and "spin only".

### M3: Usability

- Save the auto catch / auto spin settings (Flipper Format in `apps_data`).
- Several secret files with a picker (pgpemu supports multiple PGPs).
- Optional vibration on catch, plus session statistics.

### M4: Remove the workarounds upstream

- PR to flipperzero-firmware (and Momentum): make
  `ble_gatt_characteristic_update()` use `aci_gatt_update_char_value_ext()` for
  values over 243 bytes, and fix its inverted return value (it returns `true` on
  failure). Then `ble_profile_pgp_set_cert()` can drop the serial-service shim.
- PR adding raw advertising/scan response data to `GapConfig`, so the advertisement
  can match a real PGP exactly (R2).

## Risks and fallbacks

- **R1 - certificate publish.** The shim's last chunk asks the stack to indicate,
  and our characteristic has no indicate property or CCCD. The stack should just
  skip it, but if `Failed to publish 378 byte certificate` appears, add
  `CHAR_PROP_INDICATE` to `SFIDA_TO_CENTRAL` in `pgp_profile.c`, or build a
  firmware that exports `aci_gatt_update_char_value_ext`.
- **R2 - device not listed.** Most likely the missing service data. Check with
  nRF Connect that the Flipper is advertising. A fix needs firmware support (M4) or
  a patched build of Momentum.
- **R3 - pairing fails.** Try `GapPairingNone` in `pgp_profile.c` (just works, as on
  a real PGP). If the phone then never pairs, also set `ATTR_PERMISSION_ENCRY_READ`
  / `_WRITE` on the certificate characteristics so the phone has to.
- **R4 - phone rejects the challenge.** Wrong MAC, key or blob, or a MAC byte-order
  problem. nRF Connect must show the same MAC as your dump.
- **R5 - disconnect after the handshake.** Compare the logs with pgpemu's. The
  reconnect path in `pgp_handshake.c` is implemented but disabled (the key is
  dropped on disconnect, as in pgpemu); enabling it is the next experiment.
- **R6 - account risk.** Niantic can detect cloned devices. Each PGP's secrets
  should only be in use on one device at a time.

## Secrets hygiene

`pgp_device.h` with real secrets was committed earlier. It is gone from the tree
but still in git history (last at `3cf8765`). The repository is private; if it is
ever made public, rewrite the history first, or treat that device's secrets as
leaked. To recreate the SD card file from it:

```bash
git show 3cf8765:pgp_device.h > /tmp/pgp_device.h
python3 tools/make_secrets.py /tmp/pgp_device.h -o pgp_device.txt
```
