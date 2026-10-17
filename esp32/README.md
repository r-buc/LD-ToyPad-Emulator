# Toy Pad Emulator for Lego Dimensions – ESP32-S3 firmware

This directory contains a standalone port of the emulator for **ESP32-S3** boards
(developed for the [LilyGO T-Dongle-S3](https://github.com/Xinyuan-LilyGO/T-Dongle-S3),
but any ESP32-S3 with access to the native USB pins works).

The ESP32-S3 replaces the Raspberry Pi / virtual machine, the Linux USB gadget and the Node.js server:
plug it into the console or PC and it shows up as the Toy Pad, then open the web UI on your phone or computer.

## Architecture

```text
Browser (same web UI as the Node.js version: server/)
  ├─ REST requests: create, place, remove and delete tokens
  └─ Server-Sent Events (/events): lights, connection state, refreshes
           │ Wi-Fi
ESP32-S3
  ├─ ESPAsyncWebServer   src/WebApi.cpp       HTTP API + static files
  ├─ ToyPadService       src/ToyPadService.cpp glue, locking, persistence
  ├─ ToyPadCore          lib/ToyPadCore/       protocol (port of node-ld) + toytags.json store
  ├─ LittleFS            web UI, character/vehicle maps, toytags.json
  └─ TinyUSB HID         src/UsbToyPad.cpp     "LEGO READER V2.10"
           │ USB
Console / PC
```

| Module | Content |
| --- | --- |
| `lib/ToyPadCore/src/ToyPadProtocol.*` | Frame format (32 byte reports, checksum), command ids, TEA cipher and Burtle random generator used in the authentication, UID helpers, colour conversion. |
| `lib/ToyPadCore/src/ToyPadEmulator.*` | Toy Pad state machine (port of `ToyPadEmu.js` from [node-ld](https://github.com/Berny23/node-ld)): answers `WAKE`, `SEED`, `CHAL`, `READ`, `WRITE`, `MODEL`, `COL`, `FADE`, `COLAL`, `FADAL`, …, keeps the tokens on the pad and queues place/remove events. Hardware independent. |
| `lib/ToyPadCore/src/TagStore.*` | `toytags.json` compatible token storage and character/vehicle catalogs. Hardware independent. |
| `src/UsbToyPad.*` | TinyUSB HID device with the same identity and report descriptor as `usb_setup_script.sh`. |
| `src/ToyPadService.*` | Connects USB, protocol, storage and web UI. Port of the logic in `index.js`. |
| `src/WebApi.*` | HTTP routes (see below). |
| `src/main.cpp` | Startup, Wi-Fi, mDNS, OTA and main loop. |

USB reports are processed in the Arduino loop task, while web requests run in the AsyncTCP task.
All shared state is protected by a mutex and flash writes are debounced and never done from USB callbacks.

### USB identity

Same as the Linux gadget of the Node.js version:

| Property | Value |
| --- | --- |
| VID / PID | `0x0E6F` / `0x0241` |
| Manufacturer / Product / Serial | `PDP LIMITED. ` / `LEGO READER V2.10` / `P.D.P.000000` |
| `bcdDevice` | `0x0200` |
| Interface | HID, vendor-defined usage page `0xFF00`, 32 byte input and output reports, no report ids |

### HTTP API

The routes of the Node.js server are kept, the Socket.IO messages are replaced by REST calls and Server-Sent Events.
`server/scripts/main.js` automatically uses Socket.IO when it is available (Node.js server) and the routes below otherwise (ESP32-S3).

| Method | Path | Body | Description |
| --- | --- | --- | --- |
| `POST` | `/character` | `{"id": 1}` | Create a character token |
| `POST` | `/vehicle` | `{"id": 1000}` | Create a vehicle/gadget token, responds with its UID |
| `POST` | `/place` | `{"uid": "…", "id": 1, "position": 2, "index": 4}` | Place a token on the pad (`position` 1 = centre, 2 = left, 3 = right; `index` 1–7 like the UI's toy pad boxes) |
| `DELETE` | `/remove` | `{"index": 4, "uid": "…"}` | Remove a token from the pad |
| `DELETE` | `/token` | `{"uid": "…"}` | Delete a token from `toytags.json` (Socket.IO `deleteToken`) |
| `POST` | `/sync` | – | Clear the pad and the saved positions (Socket.IO `syncToyPad`) |
| `GET` / `POST` | `/status` | – | `{"connected": …, …}`; `POST` also re-sends `Connection True` (Socket.IO `connectionStatus`) |
| `GET` | `/json/toytags.json` | – | All saved tokens (also usable as backup) |
| `GET` | `/events` | – | Server-Sent Events: `refreshTokens`, `Color One`, `Color All`, `Fade One`, `Fade All`, `Connection True` (`data` is the JSON array that was emitted via Socket.IO before) |

Everything else is served from LittleFS (the `server/` directory of this repository).

## Requirements

- An ESP32-S3 board, e.g. LilyGO T-Dongle-S3 (16 MB flash) or ESP32-S3-DevKitC-1 (8 MB flash, use the port labelled **USB**, not **UART**).
- [PlatformIO](https://platformio.org/install) (CLI or the VS Code extension). The Arduino core 3.x is pulled in automatically through the [pioarduino](https://github.com/pioarduino/platform-espressif32) platform.

## Build and flash

1. Configure Wi-Fi: copy `include/wifi_secrets.example.h` to `include/wifi_secrets.h` and enter your network
   (the file is ignored by git). All settings and their defaults are in `include/config.h`.
   Without a configured network, or if connecting fails, the emulator opens the access point
   **`LD-ToyPad`** with the password **`legodimensions`** (change it with `TOYPAD_AP_PASSWORD`).

2. Put the board into download mode. The firmware uses the native USB port for the Toy Pad and therefore does **not**
   provide a USB serial port, so after the first flash the board has to be put into download mode manually:
   hold the **BOOT** button while plugging it in (T-Dongle-S3: the button next to the USB plug).

3. Build and upload the firmware and the web UI (from this directory):

   ```bash
   pio run -e t-dongle-s3 -t upload
   pio run -e t-dongle-s3 -t uploadfs
   ```

   Use `-e esp32-s3-devkitc-1` for other ESP32-S3 boards with 8 MB flash. The web UI (`../server`) is
   uploaded unchanged to LittleFS.

4. Re-plug the board (without pressing BOOT) into the console or PC. It shows up as `LEGO READER V2.10`.

5. Open `http://ldtoypad.local/` (or the IP address shown in your router / on the serial log, or `http://192.168.4.1/` in access point mode)
   and use the UI exactly like the Node.js version.

### Updates without download mode

If you set `TOYPAD_OTA_PASSWORD`, the firmware can be updated over Wi-Fi:

```bash
pio run -e t-dongle-s3 -t upload --upload-port ldtoypad.local -- --auth=<password>
```

(or set `upload_protocol = espota`, `upload_port` and `upload_flags = --auth=<password>` in `platformio.ini`).

### Logs

Log output (`CORE_DEBUG_LEVEL=3`) goes to UART0 (U0TXD, 115200 baud), because the native USB port is used by the Toy Pad.

## Saved tokens

Tokens are stored in `/json/toytags.json` on LittleFS in the same format as the Node.js version, so the file can be copied between both versions.

> [!WARNING]
> `uploadfs` overwrites the whole file system including your saved tokens. Download `http://ldtoypad.local/json/toytags.json`
> first and copy it to `server/json/toytags.json` before uploading the file system again.

If the file is corrupted it is renamed to `toytags.json.bak` and an empty list is used. At most 400 tokens can be stored.

## Tests

The protocol and storage code in `lib/ToyPadCore` does not depend on Arduino and has host unit tests
(test vectors were generated with node-ld):

```bash
pio test -e native
```

## Limitations and differences to the Node.js version

- **Not yet verified on all consoles.** The protocol is ported 1:1 from node-ld and covered by unit tests, but compatibility with each game platform has to be confirmed on real hardware.
- The HID endpoints use a max packet size of 64 bytes (TinyUSB default) instead of 32 bytes like the original pad; the reports themselves are 32 bytes. The interface string is `TinyUSB HID`.
- There is no USB serial port while the firmware is running (see download mode / OTA above).
- jQuery and jQuery UI are loaded from a CDN by `index.html`, so the device that opens the UI needs Internet access.
  In access point mode without Internet the UI does not work; use your normal Wi-Fi network (station mode) instead.
- Images for the "Adding Images" feature have to fit into the LittleFS partition (3.4 MB with `default_16MB.csv`, 1.5 MB with `default_8MB.csv`; use a custom partition table for more).
- `COLAL` (set all colours) is implemented, the Node.js version never forwarded it to the UI.
- The web UI has no authentication, like the Node.js version. Only use it in trusted networks.
