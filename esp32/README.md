# Toy Pad Emulator for Lego Dimensions – ESP32-S3 firmware

This directory contains a standalone port of the emulator for **ESP32-S3** boards as an **Arduino sketch** (`LDToyPadEmulator/`)
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
  ├─ ESPAsyncWebServer   WebApi.cpp            HTTP API + static files
  ├─ ToyPadService       ToyPadService.cpp     glue, locking, persistence
  ├─ Protocol            ToyPadEmulator.cpp,   protocol (port of node-ld), compact tag store,
  │                      ToyPadProtocol.cpp,   compiled-in character/vehicle catalog
  │                      TagStore.cpp, Catalog.cpp
  ├─ LittleFS            web UI (gzip compressed static files), toytags.bin
  └─ TinyUSB HID         UsbToyPad.cpp         "LEGO READER V2.10"
           │ USB
Console / PC
```

All files are tabs of the sketch `LDToyPadEmulator/`:

| File | Content |
| --- | --- |
| `ToyPadProtocol.*` | Frame format (32 byte reports, checksum), command ids, TEA cipher and Burtle random generator used in the authentication, UID helpers, colour conversion. |
| `ToyPadEmulator.*` | Toy Pad state machine (port of `ToyPadEmu.js` from [node-ld](https://github.com/Berny23/node-ld)): answers `WAKE`, `SEED`, `CHAL`, `READ`, `WRITE`, `MODEL`, `COL`, `FADE`, `COLAL`, `FADAL`, …, keeps the tokens on the pad and queues place/remove events. Hardware independent. |
| `TagStore.*` | Compact token storage (fixed array of 17 byte records in RAM, binary records for the file on flash) and the chunked JSON writer for the web UI. Hardware independent. |
| `Catalog.*`, `CatalogData.h` | Character/vehicle ids and names compiled into the firmware. `CatalogData.h` is generated from `server/json/charactermap.json` and `tokenmap.json` by `esp32/tools/generate_catalog.py`, so the firmware never parses these files. |
| `UsbToyPad.*` | TinyUSB HID device with the same identity and report descriptor as `usb_setup_script.sh`. |
| `ToyPadService.*` | Connects USB, protocol, storage and web UI. Port of the logic in `index.js`. |
| `WebApi.*` | HTTP routes (see below). |
| `JsonFields.*` | Reads fields of the small, flat JSON request bodies directly, without a JSON library. Hardware independent. |
| `LDToyPadEmulator.ino` | `setup()` / `loop()`: startup, Wi-Fi, mDNS, OTA and USB request handling. |
| `config.h`, `wifi_secrets.example.h` | Settings (Wi-Fi, access point, host name, OTA password, …). |
| `partitions.csv` | 16 MB partition table with a LittleFS partition, picked up automatically by the Arduino build. |

USB reports are processed in the Arduino loop task, while web requests run in the AsyncTCP task.
All shared state is protected by a mutex and flash writes are debounced and never done from USB callbacks.
The firmware uses plain C data (fixed size arrays, C strings, function pointers) instead of `std::string`, `std::vector`
or a JSON library, so RAM usage is static apart from the web server's own allocations.

### Memory usage

Most of the *Global variables* reported by the compiler belong to the ESP32 core and not to the sketch: a sketch that
only starts Wi-Fi, the async web server with Server-Sent Events, the TinyUSB HID device, LittleFS, mDNS and OTA,
without any emulator code, already reports about 70 KB (Wi-Fi driver, lwIP, TinyUSB endpoint buffers of the
precompiled core, …). The emulator adds about 8 KB, mostly the token list (17 bytes × 400 tokens) and the tag memory
of the tokens on the pad (7 × 180 bytes).

Constant data (`static const` tables such as the character catalog, the USB descriptors and all string literals) is
placed in flash on the ESP32 and does not use RAM. Unlike on AVR boards, `PROGMEM` is empty and `F()` only changes
the pointer type in the ESP32 Arduino core, so they would not save any memory and are not used.

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
| `POST` | `/character` | `{"id": 1}` | Create a character token, responds with its UID |
| `POST` | `/vehicle` | `{"id": 1000}` | Create a vehicle/gadget token, responds with its UID |
| `POST` | `/place` | `{"uid": "…", "position": 2, "index": 4}` | Place a token on the pad (`position` 1 = centre, 2 = left, 3 = right; `index` 1–7 like the UI's toy pad boxes) |
| `DELETE` | `/remove` | `{"index": 4}` | Remove a token from the pad (other fields such as `uid` are ignored) |
| `DELETE` | `/token` | `{"uid": "…"}` | Delete a saved token (Socket.IO `deleteToken`) |
| `POST` | `/sync` | – | Ask the web UI to reload the token list (Socket.IO `syncToyPad`); positions are always taken from the pad |
| `GET` / `POST` | `/status` | – | `{"connected": …, …}`; `POST` also re-sends `Connection True` (Socket.IO `connectionStatus`) |
| `GET` | `/json/toytags.json` | – | All saved tokens as minified JSON (`[{"uid","id","type","index","name"}, …]`), streamed in chunks |
| `GET` | `/events` | – | Server-Sent Events: `refreshTokens`, `Color One`, `Color All`, `Fade One`, `Fade All`, `Connection True` (`data` is the JSON array that was emitted via Socket.IO before) |

Everything else is served from LittleFS: the `server/` directory of this repository, gzip compressed by
`esp32/tools/build_data.py` (about 24 KB instead of 128 KB). The browser receives the files with `Content-Encoding: gzip`.

## Requirements

- An ESP32-S3 board, e.g. LilyGO T-Dongle-S3 (16 MB flash) or ESP32-S3-DevKitC-1 (use the port labelled **USB**, not **UART**).
- [Arduino IDE 2](https://www.arduino.cc/en/software) or [arduino-cli](https://arduino.github.io/arduino-cli/).
- The **esp32** core by Espressif, version **3.x** (tested to compile with 3.2.0). Add
  `https://espressif.github.io/arduino-esp32/package_esp32_index.json` under *File > Preferences > Additional boards manager URLs*
  and install "esp32" in the Boards Manager.
- Libraries (Library Manager):
  - **ESP Async WebServer** by ESP32Async (3.9.x) and its dependency **Async TCP** by ESP32Async (3.5.x)
- Python 3 to prepare the web UI files.
- For uploading the web UI from the IDE: the [arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload) plugin.

## Build and flash (Arduino IDE)

1. Open `esp32/LDToyPadEmulator/LDToyPadEmulator.ino`.

2. Configure Wi-Fi: copy `wifi_secrets.example.h` to `wifi_secrets.h` in the sketch folder and enter your network
   (the file is ignored by git). All settings and their defaults are in `config.h`.
   Without a configured network, or if connecting fails, the emulator opens the access point
   **`LD-ToyPad`** with the password **`legodimensions`** (change it with `TOYPAD_AP_PASSWORD`).

3. Select the board **ESP32S3 Dev Module** and these options in the *Tools* menu:

   | Option | Value |
   | --- | --- |
   | USB Mode | **USB-OTG (TinyUSB)** |
   | USB CDC On Boot | **Disabled** |
   | Flash Size | **16MB (128Mb)** (T-Dongle-S3) |
   | Partition Scheme | **Custom** (uses `partitions.csv` from the sketch folder) |
   | Core Debug Level | optional, e.g. Info |

   The sketch refuses to compile with the wrong USB options.
   For boards with less than 16 MB flash, delete `partitions.csv` and pick a scheme with SPIFFS,
   e.g. *8M with spiffs* for 8 MB boards.

4. Put the board into download mode. The firmware uses the native USB port for the Toy Pad and therefore does **not**
   provide a USB serial port, so the board has to be put into download mode manually:
   hold the **BOOT** button while plugging it in (T-Dongle-S3: the button next to the USB plug) and select its port.

5. *Sketch > Upload*.

6. Upload the web UI: run `python3 esp32/tools/build_data.py` in the repository root. It writes the gzip compressed
   files of `server/` to `esp32/LDToyPadEmulator/data/` (ignored by git). Then press `Ctrl`+`Shift`+`P` and run
   **Upload LittleFS to Pico/ESP8266/ESP32** (the serial monitor must be closed). Repeat after updating the repository.

7. Re-plug the board (without pressing BOOT) into the console or PC. It shows up as `LEGO READER V2.10`.

8. Open `http://ldtoypad.local/` (or the IP address shown in your router / on the serial log, or `http://192.168.4.1/` in access point mode)
   and use the UI exactly like the Node.js version.

### Build and flash (arduino-cli)

From the repository root, with the board in download mode on `/dev/ttyACM0`:

```bash
FQBN=esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,FlashSize=16M,PartitionScheme=custom
arduino-cli compile --fqbn "$FQBN" --upload --port /dev/ttyACM0 esp32/LDToyPadEmulator

# Web UI: gzip server/ into esp32/LDToyPadEmulator/data, build a LittleFS image from it
# and write it to the "spiffs" partition of partitions.csv
python3 esp32/tools/build_data.py
TOOLS=~/.arduino15/packages/esp32/tools
$TOOLS/mklittlefs/*/mklittlefs -c esp32/LDToyPadEmulator/data -p 256 -b 4096 -s 0x360000 littlefs.bin
python3 -m esptool --chip esp32s3 --port /dev/ttyACM0 write_flash 0xc90000 littlefs.bin
```

(`esptool` is available via `pip install esptool` or in `$TOOLS/esptool_py/`.)

### Updates without download mode

If you set `TOYPAD_OTA_PASSWORD`, the board appears as network port `ldtoypad` in the Arduino IDE
(*Tools > Port*) and can be updated over Wi-Fi, the IDE asks for the password.

### Logs

Log output (*Tools > Core Debug Level*) goes to UART0 (U0TXD, 115200 baud), because the native USB port is used by the Toy Pad.

## Saved tokens

Tokens are kept in RAM in a fixed array of compact records (17 bytes each, 6.8 KB for the maximum of 400 tokens) and saved to `/toytags.bin` on LittleFS
(a 5 byte header `LDTT` + version 2, then 17 bytes per token: UID, id and the two vehicle upgrade words, little endian).
Names are not stored, they come from the compiled-in catalog, and the type follows from the id (vehicles and gadgets
use ids from 1000). Positions on the pad are not saved, the pad is empty after a restart.
The format is not compatible with the Node.js version's `toytags.json`.

> [!WARNING]
> Uploading the web UI (LittleFS image) overwrites the whole file system including your saved tokens.

If the file is corrupted it is renamed to `toytags.bin.bak` and an empty list is used. At most 400 tokens can be stored.

Files written by earlier versions of this firmware (version 1) are treated as corrupted.

### Changing the character/vehicle lists

The web UI reads `server/json/charactermap.json` and `tokenmap.json`, the firmware uses the generated `CatalogData.h`.
After changing the JSON files, regenerate it and rebuild the firmware:

```bash
python3 esp32/tools/generate_catalog.py
```

## Tests

The hardware independent parts (protocol, emulator, tag store, catalog, request body reader) have host side unit
tests in `esp32/test/`, using [Unity](https://github.com/ThrowTheSwitch/Unity). The expected protocol values were
generated with node-ld. They need `g++`, `make` and `git` (Unity is downloaded into `esp32/test/build/` on first use):

```bash
make -C esp32/test
```

## Limitations and differences to the Node.js version

- **Not yet verified on all consoles.** The protocol is ported 1:1 from node-ld, but compatibility with each game platform has to be confirmed on real hardware.
- The HID endpoints use a max packet size of 64 bytes (TinyUSB default) instead of 32 bytes like the original pad; the reports themselves are 32 bytes. The interface string is `TinyUSB HID`.
- There is no USB serial port while the firmware is running (see download mode / OTA above).
- jQuery and jQuery UI are loaded from a CDN by `index.html`, so the device that opens the UI needs Internet access.
  In access point mode without Internet the UI does not work; use your normal Wi-Fi network (station mode) instead.
- Images for the "Adding Images" feature have to fit into the LittleFS partition (3.4 MB with the included `partitions.csv`, 1.5 MB with *8M with spiffs*; enlarge it in `partitions.csv` if needed).
- `COLAL` (set all colours) is implemented, the Node.js version never forwarded it to the UI.
- The web UI has no authentication, like the Node.js version. Only use it in trusted networks.
