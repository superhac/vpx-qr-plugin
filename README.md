# QR plugin for VPinballX

Reads a QR/barcode scanner while a table is running and publishes every scan
to other VPinballX plugins and to the table script, tagged with the running
PinMAME ROM name.

<img width="189" height="218" alt="image" src="https://github.com/user-attachments/assets/fb303e18-2e43-43bc-8566-df5fe62b26b3" />
<img width="202" height="131" alt="image" src="https://github.com/user-attachments/assets/2bdff0df-96ca-4e85-a35a-adf3e223f37e" />



## Overview

- **Supported devices:** see [Supported devices](#supported-devices).
- **Platforms:** Windows (x64, x86), Linux (x64, aarch64), macOS (universal).
- **Delivers scans to:** other plugins ([Using scans in your plugin](#using-scans-in-your-plugin))
  and table scripts ([Using scans in a table script](#using-scans-in-a-table-script)).

Built from `vpx-plugin-builder-template`; the design and decisions are in
[PLAN.md](PLAN.md).

## Supported devices

| Device | Status | Wiki | Manual | Buy |
|--------|--------|------|--------|-----|
| Waveshare Round Module (D) | Supported | [Waveshare wiki](https://www.waveshare.com/wiki/Barcode_Scanner_Module_(D)) | [Setup manual (PDF)](manual/barcode-scanner-module-%28D%29-setup-manual.pdf) | [Amazon](https://www.amazon.com/Waveshare-Scanner-Barcode-Indicator-Integrate/dp/B0CHJVSCHW) |
| GM67/M3027 | Supported | — | [Manual (PDF)](manual/GM67_Barcode.pdf) | [Amazon](https://www.amazon.com/Ruitutedianzi-Scanner-Module-Reader-Android/dp/B0DMNRJVC5) |

Any other serial scanner that sends each scan as a line of text should work
with the generic driver (`Device = 1`). Other models can be added as drivers
(see [Adding a scanner](#adding-a-scanner)).

USB scanners that act as a keyboard (HID) aren't supported: VPX would see them
as key presses.

### Waveshare Barcode Scanner Module (D) (`Device = 0`, driver `waveshare-d`)

Works with the factory settings: 9600 8N1, continuous mode, CR after each
scan. When the port opens, the plugin sends the module's read-only "query baud
rate" command to check it's really there, and logs a warning if it doesn't
answer. It never changes the module's settings.

In continuous mode the module may re-send a code about once a second while it's
in view. The plugin's duplicate window handles that. You can also turn on the
module's own "same code read delay" with a setup barcode from the
[setup manual](manual/barcode-scanner-module-%28D%29-setup-manual.pdf).

### GM67/M3027 (`Device = 2`, driver `gm67`)

The GM67/M3027 starts in USB keyboard mode, which won't work with VPX. Scan
this **USB COM** setup code once to switch it to a virtual serial port (from
section 3.3 of the [manual](manual/GM67_Barcode.pdf)):

<img src="manual/gm67-usb-com.png" alt="GM67/M3027 USB COM setup code" width="150">

It keeps the setting. It then appears as a COM port on Windows, as
`/dev/ttyACM0` (or under `/dev/serial/by-id/`) on Linux, and as
`/dev/cu.usbmodem...` on macOS. Wired to its TTL serial pins, it works as is.

Factory settings work: 9600 8N1, and it reads a code as soon as it's held in
front of it, no button needed. Our unit sends a CR after each scan, although
the manual lists no terminator as the default; `Terminator` = Auto handles
both (CR, LF or a short pause). When the port
opens, the plugin sends the module's read-only "request revision" command to
check it's really there, and logs a warning if it doesn't answer. It never
changes the module's settings.

While a code is in view, the GM67/M3027 reads it again about every half second,
beeping each time. The plugin's duplicate window turns that into one scan. To
stop the repeated beeps too, scan the module's **Delay 3s** setup code under
"Same Code Quickly Set Up" (section 4.6.1 of the
[manual](manual/GM67_Barcode.pdf)).

### Generic serial scanner (`Device = 1`, driver `serial-line`)

For scanners that send each scan as text followed by CR and/or LF (most
"serial/ASCII" scanners). Set `BaudRate` and `Terminator` to match. Listen-only.

## QR code format

**Anything can be encoded.** The plugin passes on every scan, whatever it
holds: plain text, a product barcode, or JSON of your own design. What a scan
means is up to the plugins and tables that receive it.

For a code to work on any table or plugin, and not just the one it was made
for, they all have to read it the same way. So this README also defines
**standard codes**: shared formats for common uses. Each standard has a name,
e.g. `player`, and a set of fields. A plugin or table that understands a
standard can use any code made to it, whoever made the code. If you're making
codes for a common use, use the standard one if there is one.

### How standard codes are built

A standard code is one JSON object, with the standard's name as the key and
its fields as an object under it:

```json
{"v":1,"player":{"name":"Jane Smith","initials":"JNS"}}
```

| Key            | Set by     | Description |
|----------------|------------|-------------|
| `v`            | QR code    | Format version, currently `1`. |
| *standard name* | QR code   | The standard, e.g. `player`, with its fields. |
| `rom`          | the plugin | **Added automatically** to every scan: the running PinMAME ROM name, `""` if the table doesn't use PinMAME. Leave it out of the code; see [What receivers get](#what-receivers-get). |

### Standards

These are the standard codes defined so far. Plugins and tables that support
one can use any code made to it.

| Standard | Key | What it's for |
|----------|-----|---------------|
| **[Player](#player)** | `player` | Says who is playing, so plugins and tables can keep scores, achievements and other progress for that player. |

#### Player

**Key:** `player`

Tells plugins and tables who is playing. Scan your player code at the start
of a game and anything listening knows it's you, so it can, for example:

- keep high scores and stats for each player, not just per table
- unlock and track achievements for that player
- put the player's name or initials on a score without typing them in

```json
{"v":1,"player":{"name":"Jane Smith","initials":"JNS"}}
```

| Field      | Required | Description |
|------------|----------|-------------|
| `name`     | yes      | Player name, e.g. `Jane Smith`. |
| `initials` | yes      | Player initials, e.g. `JNS`. |
| `passcode` | no       | A secret unique to the player, so that nobody can make a valid code for a player just by knowing their name and initials. Consumers check it against the passcode they have on record for that player. The plugin passes it on unchanged. |

### Custom codes

Codes that don't fit a standard work too. A custom JSON code follows the same
shape with a key of your own, e.g. `{"v":1,"mytable_bonus":{"level":3}}`.
Pick a name that won't clash with a standard, e.g. prefixed with your table
or plugin name. If a format would be useful to others, propose it as a new
standard in an issue or pull request.

### What receivers get

> **The plugin always adds the ROM name.** Every scan it sends on has the
> running PinMAME ROM name added, so receivers know which table it was scanned
> on. `""` means the table doesn't use PinMAME. Don't put it in your codes:
> the plugin sets it.

| Scan            | How the ROM name is added | Example, on Attack from Mars |
|-----------------|---------------------------|------------------------------|
| A JSON object   | As a `rom` key, added at the end. If the code already has a `rom` key, its value is replaced. | `{"v":1,"player":{"name":"Jane Smith","initials":"JNS"}}` → `{"v":1,"player":{"name":"Jane Smith","initials":"JNS"},"rom":"afm_113b"}` |

Apart from that, JSON is re-written compactly, keeping the key order. The
plugin doesn't check the other keys, so new standards, custom codes and new
fields need no plugin change. Consumers should ignore keys they don't know.

### Making codes

- Put the JSON on **one line**. The scanner sends line breaks as they are, and
  the plugin would read each line as a separate scan. JSON tools' compact
  output is fine, e.g.
  `qrencode -o jane.png '{"v":1,"player":{"name":"Jane Smith","initials":"JNS"}}'`.
- Text can be in any language (UTF-8).

## Installing

1. Download the zip for your platform from the
   [GitHub releases](https://github.com/superhac/vpx-qr-plugin/releases)
   (or build it, see below):

   | Your system           | Zip |
   |-----------------------|-----|
   | Windows, 64-bit VPX   | `qr-<version>-windows-x64.zip` |
   | Windows, 32-bit VPX   | `qr-<version>-windows-x86.zip` |
   | Linux, PC             | `qr-<version>-linux-x64.zip` |
   | Linux, ARM (e.g. Raspberry Pi) | `qr-<version>-linux-aarch64.zip` |
   | macOS (Intel or Apple Silicon) | `qr-<version>-macos-universal.zip` |

2. Unzip it and copy the `QR/` folder into the `plugins` folder in your
   user's VPX preferences folder. Create `plugins` if it isn't there yet.

   | OS      | Copy `QR/` into |
   |---------|-----------------|
   | Windows | `%APPDATA%\VPinballX\10.8\plugins\` (i.e. `C:\Users\<you>\AppData\Roaming\VPinballX\10.8\plugins\`) |
   | Linux   | `~/.local/share/VPinballX/10.8/plugins/` |
   | macOS   | `~/Library/Application Support/VPinballX/10.8/plugins/` |

   You should end up with e.g. `.../VPinballX/10.8/plugins/QR/plugin.cfg`.

   - `10.8` is VPX's major.minor version; use the folder matching yours. VPX
     creates the preferences folder on its first start, so run VPX once if it
     doesn't exist yet.
   - To open the folder on Windows, paste `%APPDATA%\VPinballX` into the
     Explorer address bar. On macOS, `~/Library` is hidden: in Finder use
     Go → Go to Folder… and paste the path.
   - If you're not sure, VPX's log prints the folder at startup in the line
     `Preference path: ...`. If you start VPX with `-PrefPath <folder>`, use
     `<folder>/plugins/` instead.
   - Using this folder keeps the plugin when you update or reinstall VPX. The
     `plugins` folder next to the VPX executable (`VPinballX.app/Contents/PlugIns`
     on macOS) also works, but holds the plugins that ship with VPX and is
     replaced on upgrade.

3. Enable the plugin and set the port, in VPX's plugin settings or in
   `VPinballX.ini`:

   ```ini
   [Plugin.QR]
   Enable = 1
   Port = COM3
   ```

4. Start a table. The plugin opens the port when the table starts and closes
   it when the table ends. Scans made outside of a game are ignored.

To check it's working, turn on `ShowNotification`: VPX then pops up every scan
and connection change. Details also go to VPX's log.

### Finding the port

| OS      | Port name                                  | Notes |
|---------|--------------------------------------------|-------|
| Windows | `COM3`                                     | See Device Manager → Ports (COM & LPT). |
| Linux   | `/dev/serial/by-id/usb-Silicon_Labs_...`   | Stays the same across reboots and replugging; `/dev/ttyUSB0` may change. Your user must be in the `dialout` group. |
| macOS   | `/dev/cu.usbserial-...` / `/dev/cu.SLAB_USBtoUART` / `/dev/cu.usbmodem...` | Use the `cu.*` name, not `tty.*`. |

`qrprobe --list` prints the ports it finds (see [Tools](#tools)).

## Settings

Saved under `[Plugin.QR]` in `VPinballX.ini`. Port, device, baud rate and
terminator take effect when the next table starts.

| Setting             | Default        | Description |
|---------------------|----------------|-------------|
| `Device`            | Waveshare (D)  | Scanner model (driver). Saved as a number: 0 = Waveshare Barcode Scanner (D), 1 = Generic serial scanner, 2 = GM67/M3027. |
| `Port`              | (empty)        | Serial port, see above. Empty = plugin does nothing. |
| `BaudRate`          | Device default | Serial speed. Saved as a number: 0 = device default, 1 = 1200, 2 = 2400, 3 = 4800, 4 = 9600, 5 = 14400, 6 = 19200, 7 = 38400, 8 = 57600, 9 = 115200. 14400 isn't available on Linux. |
| `Terminator`        | Auto           | How a scan ends: 0 = Auto (CR, LF or a 50 ms pause), 1 = CR, 2 = LF, 3 = CRLF, 4 = pause only. Use CR/LF/CRLF if codes contain line breaks. |
| `DuplicateWindowMs` | 1500           | Ignore the same code again within this time (0 to 60000 ms). A code held in front of the scanner counts once. 0 = off. |
| `ScanCooldownMs`    | 0              | After a scan, ignore *all* scans for this time (0 to 60000 ms). 0 = off. |
| `ShowNotification`  | off            | Show a VPX notification for every scan and connection change. |

If the scanner is unplugged, the plugin keeps retrying every 2 seconds and
picks it up again when it's plugged back in.

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

For the [`player` code](#player), on Attack from Mars,
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
what consuming plugins receive. The optional last argument is the `Device`
number (default 0):

```bash
build/qr_fake_host build/plugin-qr.so /dev/ttyUSB0 60        # Waveshare (D)
build/qr_fake_host build/plugin-qr.so /dev/ttyACM0 60 2      # GM67/M3027
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
src/devices/              drivers, registry, LineFramer, Waveshare and GM67/M3027 protocols
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
