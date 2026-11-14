# QR plugin for VPinballX

Reads a QR/barcode scanner while a table is running and publishes every scan
to other VPinballX plugins and to the table script, tagged with the running
PinMAME ROM name.

## Supported devices

| Device | Status | Wiki | Manual | Buy |
|--------|--------|------|--------|-----|
| Waveshare Round Module (D) | Supported | [Waveshare wiki](https://www.waveshare.com/wiki/Barcode_Scanner_Module_(D)) | [Setup manual (PDF)](manual/barcode-scanner-module-%28D%29-setup-manual.pdf) | [Amazon](https://www.amazon.com/Waveshare-Scanner-Barcode-Indicator-Integrate/dp/B0CHJVSCHW) |
| GM67 | Coming soon | — | [Manual (PDF)](manual/GM67_Barcode.pdf) | [Amazon](https://www.amazon.com/Ruitutedianzi-Scanner-Module-Reader-Android/dp/B0DMNRJVC5) |

Any other serial scanner that sends each scan as a line of text should work
with the generic driver (`Device = 1`). Other models can be added as drivers
(see [Adding a scanner](#adding-a-scanner)).

## QR code format

A QR code for the plugin holds one JSON object. The top-level key says what
kind of code it is, e.g. `player` for a player logging in. Other kinds will be
added as new keys.

```json
{"v":1,"player":{"name":"Jane Smith","initials":"JNS"}}
```

| Key        | Set by     | Description |
|------------|------------|-------------|
| `v`        | QR code    | Format version, currently `1`. |
| `player`   | QR code    | A player logging in, see below. |
| `rom`      | the plugin | The running PinMAME ROM name, `""` if the table doesn't use PinMAME. Leave it out of the code; if a code has one, the plugin overwrites it. |

`player` fields:

| Field      | Description |
|------------|-------------|
| `name`     | Player name, e.g. `Jane Smith`. |
| `initials` | Player initials, e.g. `JNS`. |
| `passcode` | Optional. A secret unique to the player, so that nobody can make a valid code for a player just by knowing their name and initials. Consumers check it against the passcode they have on record for that player. The plugin passes it on unchanged. |

What other plugins receive for the code above, on Attack from Mars:

```json
{"v":1,"player":{"name":"Jane Smith","initials":"JNS"},"rom":"afm_113b"}
```

The plugin adds `rom` and re-writes the JSON compactly, keeping the key order.
It doesn't check the other keys, so new kinds of code and new fields need no
plugin change. Consumers should ignore keys they don't know.

When making codes:

- Put the JSON on **one line**. The scanner sends line breaks as they are, and
  the plugin would read each line as a separate scan. JSON tools' compact
  output is fine, e.g.
  `qrencode -o jane.png '{"v":1,"player":{"name":"Jane Smith","initials":"JNS"}}'`.
- Names can use any language (UTF-8).

A scan that isn't a JSON object, e.g. a product barcode or plain text, is
passed on as scanned, with the ROM name appended in brackets:
`[PLAYER][FIELD1]` becomes `[PLAYER][FIELD1][afm_113b]` (`[]` if no ROM).

## Overview

- **Supported scanners:** see [Supported devices](#supported-devices).
- **Platforms:** Windows (x64, x86), Linux (x64, aarch64), macOS (universal).
- **Delivers scans to:** other plugins ([Using scans in your plugin](#using-scans-in-your-plugin))
  and table scripts ([Using scans in a table script](#using-scans-in-a-table-script)).

Built from `vpx-plugin-builder-template`; the design and decisions are in
[PLAN.md](PLAN.md).

## Installing

1. Download the zip for your platform from the GitHub release (or build it,
   see below) and copy the `QR/` folder into the plugins folder inside VPX's
   preferences folder, creating `plugins/` if it doesn't exist. That's the
   place for third-party plugins; the plugins folder next to the VPX
   executable is for the plugins that ship with VPX.
   - Linux: `~/.local/share/VPinballX/<version>/plugins/`, e.g.
     `~/.local/share/VPinballX/10.8/plugins/QR/`
   - Windows and macOS: `<VPX preferences folder>/plugins/`. VPX's log shows the
     folder at startup, in the line "Preference path: ...".

   VPX only scans this folder if it exists. It also scans the plugins folder
   next to the executable.
2. Enable the plugin and set the port, in VPX's plugin settings or in
   `VPinballX.ini`:

   ```ini
   [Plugin.QR]
   Enable = 1
   Port = COM3
   ```

3. Start a table. The plugin opens the port when the table starts and closes
   it when the table ends. Scans made outside of a game are ignored.

To check it's working, turn on `ShowNotification`: VPX then pops up every scan
and connection change. Details also go to VPX's log.

### Finding the port

| OS      | Port name                                  | Notes |
|---------|--------------------------------------------|-------|
| Windows | `COM3`                                     | See Device Manager → Ports (COM & LPT). |
| Linux   | `/dev/serial/by-id/usb-Silicon_Labs_...`   | Stays the same across reboots and replugging; `/dev/ttyUSB0` may change. Your user must be in the `dialout` group. |
| macOS   | `/dev/cu.usbserial-...` / `/dev/cu.SLAB_USBtoUART` | Use the `cu.*` name, not `tty.*`. |

`qrprobe --list` prints the ports it finds (see [Tools](#tools)).

## Settings

Saved under `[Plugin.QR]` in `VPinballX.ini`. Port, device, baud rate and
terminator take effect when the next table starts.

| Setting             | Default        | Description |
|---------------------|----------------|-------------|
| `Device`            | Waveshare (D)  | Scanner model (driver). Saved as a number: 0 = Waveshare Barcode Scanner (D), 1 = Generic serial scanner. |
| `Port`              | (empty)        | Serial port, see above. Empty = plugin does nothing. |
| `BaudRate`          | Device default | Serial speed. Saved as a number: 0 = device default, 1 = 1200, 2 = 2400, 3 = 4800, 4 = 9600, 5 = 14400, 6 = 19200, 7 = 38400, 8 = 57600, 9 = 115200. 14400 isn't available on Linux. |
| `Terminator`        | Auto           | How a scan ends: 0 = Auto (CR, LF or a 50 ms pause), 1 = CR, 2 = LF, 3 = CRLF, 4 = pause only. Use CR/LF/CRLF if codes contain line breaks. |
| `DuplicateWindowMs` | 1500           | Ignore the same code again within this time (0 to 60000 ms). A code held in front of the scanner counts once. 0 = off. |
| `ScanCooldownMs`    | 0              | After a scan, ignore *all* scans for this time (0 to 60000 ms). 0 = off. |
| `ShowNotification`  | off            | Show a VPX notification for every scan and connection change. |

If the scanner is unplugged, the plugin keeps retrying every 2 seconds and
picks it up again when it's plugged back in.

## Supported scanners

### Waveshare Barcode Scanner Module (D) (`waveshare-d`)

Works with the factory settings: 9600 8N1, continuous mode, CR after each
scan. When the port opens, the plugin sends the module's read-only "query baud
rate" command to check it's really there, and logs a warning if it doesn't
answer. It never changes the module's settings.

In continuous mode the module may re-send a code about once a second while it's
in view. The plugin's duplicate window handles that. You can also turn on the
module's own "same code read delay" with a setup barcode from the manual.

The setup manual is in [manual/](manual/), and the module's wiki page is
<https://www.waveshare.com/wiki/Barcode_Scanner_Module_(D)>.

### Generic serial scanner (`serial-line`)

For scanners that send each scan as text followed by CR and/or LF (most
"serial/ASCII" scanners). Set `BaudRate` and `Terminator` to match. Listen-only.

USB scanners that act as a keyboard (HID) aren't supported: VPX would see them
as key presses.

## Using scans in your plugin

Copy [include/QRPlugin.h](include/QRPlugin.h) into your plugin (it's also in
the release zip under `QR/include/`). It only depends on the VPX SDK's
`MsgPlugin.h`.

```cpp
#include "QRPlugin.h"

unsigned int onScanId;

void MSGPIAPI OnScan(const unsigned int, void*, void* msgData)
{
   const auto* scan = static_cast<const QRScanEvent*>(msgData);
   std::string text(scan->data, scan->length); // copy: only valid during the callback
   // ... use text, scan->sequence, scan->deviceId
}

// In your PluginLoad:
msgApi->SubscribeMsg(endpointId, onScanId = msgApi->GetMsgID(QRPI_NAMESPACE, QRPI_EVT_ON_SCAN), OnScan, nullptr);

// In your PluginUnload:
msgApi->UnsubscribeMsg(onScanId, OnScan, nullptr);
msgApi->ReleaseMsgID(onScanId);
```

| Message                 | Data                 | When |
|-------------------------|----------------------|------|
| `QR` / `OnScan:1`          | `const QRScanEvent*`   | Every accepted scan (after duplicate filtering), with the ROM name added. |
| `QR` / `OnStatusChanged:1` | `const QRStatusEvent*` | The scanner connected or disconnected (with a reason). |
| `QR` / `GetAPI:1`          | `QRPluginAPI**`        | Broadcast it to get the API table: `IsConnected()`, `GetLastScan()`. |

- All events arrive on the main (plugin API) thread, and the API functions
  must be called from it.
- The published text is the scanned text (without its terminator) with the
  **ROM name** added: a `"rom"` key for JSON codes, a `[rom]` field for
  anything else (see [QR code format](#qr-code-format)). Use `length`, not
  `strlen`.
- Plugins load in no particular order, so you may miss the first status
  event. Use `GetAPI` → `IsConnected()` to check the state, e.g. on
  `OnGameStart`.

[examples/consumer/](examples/consumer/) is a complete consumer plugin.

## Using scans in a table script

To react to scans in a table, add a `QR_OnScan` Sub to the table script. The
plugin calls it once for each accepted scan, with a `Scripting.Dictionary` of
the scan's fields. The table doesn't need to parse any JSON.

```vb
Sub QR_OnScan(scan)
   If scan("type") = "player" Then
      ' e.g. show a welcome message, set up the player
      LoginPlayer scan("player.name"), scan("player.initials")
   End If
End Sub
```

For the player code in [QR code format](#qr-code-format), on Attack from Mars,
`scan` contains:

| Key               | Value |
|-------------------|-------|
| `type`            | `"player"` |
| `v`               | `1` |
| `player.name`     | `"Jane Smith"` |
| `player.initials` | `"JNS"` |
| `rom`             | `"afm_113b"` (`""` if the table doesn't use PinMAME) |
| `json`            | the full JSON, as other plugins receive it |

How the JSON becomes keys:

- **`type`** is the code's type: its first top-level key whose value is an
  object (`player`). `""` if there is none.
- Nested fields are joined with dots: `player.name`.
- Numbers, `true`/`false` and `null` arrive as VBScript numbers, `True`/`False`
  and `Null`. Arrays become `list.0`, `list.1`, ... plus `list.count`.
- Keys aren't case-sensitive: `scan("Player.Name")` works too.
- A field the code doesn't have reads as `Empty`. Use `scan.Exists("player.initials")`
  to check whether it's there. (Reading a missing key adds it to the
  Dictionary, as usual for `Scripting.Dictionary`.)
- `type` and `json` are set by the plugin, so a code can't override them;
  `rom` too.

Scans that aren't JSON, e.g. a product barcode, also call `QR_OnScan`, with
`type` = `"text"`, `text` = the scanned text, and `rom`.

Notes:

- The table doesn't need to do anything else. If it has no `QR_OnScan`, the
  plugin skips it silently.
- Errors in `QR_OnScan` show as normal script errors.
- It's only called while the table is running, after duplicate filtering, on
  the same thread as the rest of the script. A scan in the first moments of
  loading, before the table script has started, isn't delivered to it.
- It works the same on Windows, Linux and macOS.
- Leave `QRPlugin_` names alone: the plugin defines a `QRPlugin_Dispatch` Sub
  in the table script to make the call.

## Building

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
scripts/package.sh linux-x64        # dist/QR/ + dist/qr-<version>-linux-x64.zip
```

Options (all off by default):

| Option               | Builds |
|----------------------|--------|
| `-DQR_BUILD_TOOLS=ON`    | `qrprobe`, a command-line scanner tester |
| `-DQR_BUILD_TESTS=ON`    | the unit tests and a fake-VPX plugin test; run with `ctest --test-dir build` |
| `-DQR_BUILD_EXAMPLES=ON` | the example consumer plugin (`QRConsumer/` when installed) |

GitHub Actions builds all five platforms on every push, runs the tests on each,
and makes a release from a version tag
([`.github/workflows/build.yml`](.github/workflows/build.yml)).

### Versions and releases

The version lives in one place: `version` in [plugin.cfg](plugin.cfg). VPX
shows it in its plugin list, and it's part of every zip name:

| build                      | zip name |
|----------------------------|----------|
| release (tag `v0.1.0`)     | `qr-0.1.0-linux-x64.zip` |
| any other push             | `qr-0.1.0-dev-<commit>-linux-x64.zip` |
| `scripts/package.sh`       | `qr-0.1.0-linux-x64.zip` |

To release:

1. Set `version` in `plugin.cfg` (e.g. `0.2.0`) and commit.
2. Tag that commit with the same version and push the tag:
   `git tag v0.2.0 && git push origin v0.2.0`

The workflow refuses a tag that doesn't match `plugin.cfg` (tag `v0.2.0` with
`version = "0.1.0"` fails with an error saying so), so the files never report
a different version than their name.

## Tools

**qrprobe** tests a scanner without VPX, using the plugin's own code:

```bash
qrprobe --list                                # list serial ports
qrprobe /dev/ttyUSB0                          # print decoded scans (Ctrl+C to stop)
qrprobe COM3 --raw                            # hex dump of everything received
qrprobe COM3 --device serial-line --baud 115200 --terminator crlf
```

**qr_fake_host** loads the built plugin into a minimal fake VPX host. Without
a port it's an automated test (part of `ctest`). With a port it shows exactly
what consuming plugins receive:

```bash
build/qr_fake_host build/plugin-qr.so /dev/ttyUSB0 60
```

## Adding a scanner

Each scanner model is a driver in [src/devices/](src/devices/):

1. Create `src/devices/<Name>.cpp` with a class derived from `ScannerDevice`
   ([ScannerDevice.h](src/devices/ScannerDevice.h)). Implement `PortConfig()`
   (serial settings), `OnBytes()` and `OnIdle()`, and optionally `Identify()`.
   For line-based devices, use `LineFramer` as `WaveshareD.cpp` and
   `SerialLine.cpp` do.
2. Add a `Create<Name>()` factory and **append** it to the list in
   [DeviceRegistry.cpp](src/devices/DeviceRegistry.cpp). Never reorder the
   list: VPX saves the `Device` setting as a position in it.
3. Add tests to [tests/qr_tests.cpp](tests/qr_tests.cpp).

A scanner that isn't a serial port (e.g. USB HID) needs a new transport next
to [SerialPort](src/transport/SerialPort.h), implementing
[Transport](src/transport/Transport.h).

## Source layout

```
include/QRPlugin.h        public API for consumer plugins
src/QRPlugin.cpp          VPX glue: settings, events, main-thread hand-off
src/core/                 ScannerWorker (thread, reconnect), ScanFilter (duplicates/cooldown),
                          ScanFormat (adds the ROM name, builds the table script call)
third_party/nlohmann/     nlohmann/json (MIT), for JSON codes
src/devices/              drivers, registry, LineFramer, Waveshare protocol
src/transport/            SerialPort: POSIX (Linux/macOS) and Win32
tools/qrprobe.cpp         scanner test CLI
tests/                    unit tests, fake VPX host
examples/consumer/        example consumer plugin
manual/                   scanner manuals
```

## License

GPLv3, see [LICENSE](LICENSE). The VPX SDK headers in `sdk/plugins/` are
GPLv3+. [nlohmann/json](https://github.com/nlohmann/json) in `third_party/`
is MIT; its license ships in the plugin folder as `LICENSE-nlohmann-json.txt`.
