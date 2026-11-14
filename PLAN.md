# QR plugin: plan

A VPinballX plugin that reads a QR/barcode scanner and publishes each scan to
other plugins. The first supported device is the **Waveshare Barcode Scanner
Module (D)**. The code is structured so other scanner models can be added
without touching the plugin core.

Built from `vpx-plugin-builder-template` (plugin id `QR`, library `plugin-qr`).

## Status (2026-11-13)

| step | state |
|------|-------|
| 1. Serial layer + qrprobe | done; identifies the Waveshare D on Linux |
| 2. Device layer + unit tests | done; tests pass on Linux |
| 3. Plugin | done; tested with the real scanner in VPX on Linux (2026-11-13) |
| 4. Consumer example + README | done (`examples/consumer/`, README) |
| 5. Cross-platform | done; GitHub Actions builds and tests all five targets, and the real scanner was tested in VPX on Windows (2026-11-13). macOS: CI only, no real-device test yet |

Differences from the plan while implementing:
- Added `tests/fake_host.cpp`, a minimal fake VPX host that loads the real
  plugin. Without a port it's a ctest lifecycle test; with a port it's an
  end-to-end test.
- Windows builds use a generated `.def` file so MinGW exports only the two
  entry points (MSVC already did).
- If the Waveshare doesn't answer the identify query, the plugin still
  connects and listens (with a warning) instead of refusing, because the scan
  output may work even when commands don't.

## 1. What we know

### The device: Waveshare Barcode Scanner Module (D)

Sources: the [Waveshare wiki](https://www.waveshare.com/wiki/Barcode_Scanner_Module_(D))
and the setup manual ([`manual/barcode-scanner-module-(D)-setup-manual.pdf`](manual/)). Page numbers below refer to the manual.

| item               | value                                                        | source         |
|--------------------|--------------------------------------------------------------|----------------|
| Interface          | UART (TTL) only; here via a CP2102 USB-serial adapter         | p. 6, wiki     |
| Serial settings    | **9600 8N1** default, no flow control. Baud 1200–115200, parity none/odd/even | p. 6–7, App. C |
| Output format      | `[Prefix][Code ID][Data][Suffix][Tail]`. Defaults: no prefix, suffix or Code ID; **tail = CR (0x0D)** | p. 16–19, App. C |
| Other tails        | None, TAB, or CRLF (set with setup barcodes)                  | p. 19          |
| Output protocol    | Default "data only". Optional framed mode: `03 <length> <data>` | p. 20          |
| Scan modes         | **Continuous (default)**: reads every 1000 ms. Sensing: reads when the scene changes | p. 8–11, App. C |
| Same-code delay    | Off by default, so a code left in view is re-sent every ~1 s. Can be set to 0–12.7 s or "infinite" with setup barcodes | p. 10, App. C |
| Code ID            | Optional one-character symbology tag, e.g. `Q` for QR        | App. E         |
| Symbologies        | QR, DataMatrix, PDF417, plus 1D codes (some disabled by default) | wiki, p. 21+ |
| Configuration      | Mostly by scanning printed setup barcodes ("batch" codes, App. G) | App. B, G   |
| Serial commands    | Only four documented: set baud, save to flash, query baud, factory reset | App. D |

**Serial command protocol** (App. D, checked and verified on your scanner):

```
request:   7E 00 <type> <len> <addr hi> <addr lo> <data...> <crc hi> <crc lo>
           type: 07 = read, 08 = write, 09 = save/restore
response:  02 00 00 <len> <data...> <crc hi> <crc lo>
crc:       CRC-16/XMODEM (poly 0x1021, init 0), over everything after
           "7E 00" (request) or after "02 00" (response), excluding the CRC itself
```

| command               | request                         | response                  |
|-----------------------|---------------------------------|---------------------------|
| Query baud rate       | `7E 00 07 01 00 2A 02 D8 0F`    | `02 00 00 02 39 01 C1 4C` (0x0139 = 9600) |
| Set baud rate to 9600 | `7E 00 08 02 00 2A 39 01 A7 EA` | `02 00 00 01 00 33 31`    |
| Save to flash         | `7E 00 09 01 00 00 00 DE C8`    | `02 00 00 01 00 33 31`    |
| Restore factory       | `7E 00 09 01 00 00 FF C0 38`    | `02 00 00 01 00 33 31`    |

I sent the query to the scanner on this machine on 2026-11-13 and got exactly
`02 00 00 02 39 01 C1 4C`. All four CRCs in the manual check out as
CRC-16/XMODEM.

**A real scan** captured the same day with default settings:

```
text: [PLAYER][FIELD1][FIELD2][FIELD3]\r
hex:  5B 50 4C 41 59 45 52 5D 5B 46 49 45 4C 44 31 5D 5B 46 49 45 4C 44 32 5D
      5B 46 49 45 4C 44 33 5D 0D
      33 bytes, arrived over 33 ms (~1 ms/byte at 9600 baud)
```

That matches the manual: plain ASCII, no prefix, a single CR at the end.

What this means for the design:
- **Framing:** a scan ends at CR by default. The framer also accepts LF/CRLF,
  and falls back to "end after 50 ms of silence" in case someone sets the tail
  to none (at 9600 baud a byte takes ~1 ms, so 50 ms of silence safely means
  the scan is finished). The optional `03 <len> <data>` mode can be supported
  as a driver option later. It isn't needed with the default settings.
- **Duplicates:** with the default settings, a code held in front of the
  scanner is re-sent about once a second. The plugin drops repeated identical
  scans within a configurable window. The device's own same-code delay
  (set with a setup barcode) is an alternative, and the README will mention it.
- **Device check:** the read-only "query baud rate" command lets the driver
  confirm a Waveshare D is really on the configured port when it opens it, and
  report "wrong device" otherwise. A scan that arrives during the check (data
  that doesn't start with `02 00 00`) is still treated as a scan.
- **No other commands:** there is no documented trigger command, so
  continuous or sensing mode (chosen with setup barcodes) is how it's used.
  The plugin never writes settings to the device's flash.

On this machine the scanner is
`/dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0`
(→ `/dev/ttyUSB0`, group `dialout`, and you're in `dialout`, so no extra
permissions are needed).

### The host: VPX plugin lifecycle

These facts come from the vpinball source (`src/core/player.cpp`,
`plugins/plugins/MsgPluginManager.cpp`):

- **Plugins load when a table starts and unload when it ends** (they live as
  long as the `Player`). So the serial port is opened at table start and
  closed at table end, and scans made outside of play are ignored. That fits
  a "scan to do something in the game" use.
- **Plugin callbacks must run on the main (plugin API) thread.** Serial reads
  block, so they run on a background thread, and each finished scan is handed
  over with `msgApi->RunOnMainThread(endpointId, 0, ...)`. VPX runs those
  callbacks from its main loop, and the plugin broadcasts the scan from there.
- **On unload**, the plugin must stop the reader thread, call
  `FlushPendingCallbacks`, then unsubscribe and release its message ids.

## 2. What other plugins see (public API)

A small public header, `include/QRPlugin.h`, that other plugin authors copy
into their project, the same way VPX's own `plugins/*.h` headers work:

```c
#define QRPI_NAMESPACE          "QR"
#define QRPI_EVT_ON_SCAN        "OnScan:1"           // msgData: const QRScanEvent*
#define QRPI_EVT_ON_STATUS_CHG  "OnStatusChanged:1"  // msgData: const QRStatusEvent*
#define QRPI_MSG_GET_API        "GetAPI:1"           // msgData: QRPluginAPI**

typedef struct QRScanEvent {
   int version;              // 1
   const char* data;         // scanned payload, NUL-terminated (may contain any bytes; use length)
   unsigned int length;      // payload length in bytes, excluding the NUL
   const char* deviceId;     // driver id, e.g. "waveshare-d"
   uint32_t sequence;        // increments with every scan
} QRScanEvent;               // valid only during the callback: copy what you need

typedef struct QRStatusEvent {
   int version;              // 1
   int connected;            // 1 = device open and reading, 0 = not connected
   const char* deviceId;
   const char* port;         // e.g. "/dev/ttyUSB0" or "COM3"
   const char* message;      // human-readable reason, e.g. "port not found"
} QRStatusEvent;

typedef struct QRPluginAPI {
   int version;                                         // 1
   int  (MSGPIAPI* IsConnected)();
   // Copies the last scan into buf (NUL-terminated, truncated to bufSize).
   // Returns its full length, or 0 if there has been no scan yet.
   unsigned int (MSGPIAPI* GetLastScan)(char* buf, unsigned int bufSize, uint32_t* sequence);
} QRPluginAPI;
```

This follows the upstream conventions (versioned message names, API tables
starting with `int version`), and the payload is passed as length + bytes, so
binary QR content isn't truncated at a NUL.

A consumer subscribes like any VPX event:

```cpp
msgApi->SubscribeMsg(endpointId, onScanId = msgApi->GetMsgID(QRPI_NAMESPACE, QRPI_EVT_ON_SCAN), onScan, nullptr);
```

## 3. Architecture

```
          background thread                         main thread
 ┌─────────────────────────────────────┐   ┌───────────────────────────────┐
 │ Transport ── bytes ──► Device driver │   │ QRPlugin                      │
 │ (SerialPort)           (framing,     │──►│  debounce → OnScan broadcast  │
 │                         decoding)    │   │  status   → OnStatusChanged   │
 │        ScannerWorker: open, read,    │   │  settings, GetAPI, notification│
 │        reconnect on unplug           │   └───────────────────────────────┘
 └─────────────────────────────────────┘     RunOnMainThread hand-off
```

Three layers, so new hardware only touches one of them:

1. **Transport**: moves bytes. v1 has `SerialPort`, with one implementation for
   POSIX (`termios`; covers Linux and macOS) and one for Win32
   (`CreateFile` + `DCB` + `COMMTIMEOUTS`). Reads time out every ~100 ms so the
   thread can notice it's being stopped. No third-party library is needed,
   which keeps the template's self-contained builds on every CI target.
2. **Device driver**: turns bytes into scans. It knows its device's default
   serial settings, framing, and (later) commands. Interface, roughly:

   ```cpp
   struct DeviceInfo { const char* id; const char* name; SerialConfig defaults; };
   class ScannerDevice {
   public:
      virtual ~ScannerDevice() = default;
      virtual const DeviceInfo& Info() const = 0;
      virtual void OnOpen(Transport& t) {}                // e.g. send setup commands later
      virtual void OnBytes(std::span<const uint8_t> bytes, Clock::time_point now) = 0;
      virtual void OnIdle(Clock::time_point now) = 0;     // lets silence-based framing finish a scan
      std::function<void(std::string_view)> onScan;       // set by the worker
   };
   ```

   Drivers register themselves in a **device registry** (id → factory). The
   "Device" setting's choices are built from the registry, so adding a model
   is: write one `.cpp`, register it, done.
3. **Plugin core**: settings, the worker thread lifecycle, debouncing,
   broadcasting, status, and the optional VPX notification. It knows nothing
   about specific devices.

Drivers in v1:

| id            | name                              | behaviour                                           |
|---------------|-----------------------------------|-----------------------------------------------------|
| `waveshare-d` | Waveshare Barcode Scanner (D)     | 9600 8N1, CR (or LF / 50 ms silence) ends a scan, checks the device with "query baud rate" when it opens the port |
| `serial-line` | Generic serial scanner (text)     | baud and terminator from settings; covers most "serial ASCII" scanners |

Possible future drivers, which the layering allows for:
- Scanners with a command protocol (trigger mode, beep, LED), e.g. other
  Waveshare modules or Grow GM-series modules: same transport, new driver.
- **USB HID scanners.** Many scanners act as a keyboard instead of a serial
  port. VPX would see those as key presses, so they'd need a new *transport*
  (HID POS via hidapi, or raw input), not just a driver. It's out of scope for
  v1, but the transport interface is where it would plug in.

### Planned layout

```
qr/
├── plugin.cfg                  # id = QR
├── include/QRPlugin.h          # public API for consumer plugins (shipped in the zip too)
├── src/
│   ├── plugin_id.h             # from the template
│   ├── QRPlugin.cpp            # load/unload, settings, messaging, debounce
│   ├── ScannerWorker.{h,cpp}   # thread: open → read → reconnect, hands scans to the main thread
│   ├── transport/
│   │   ├── Transport.h
│   │   ├── SerialPort.h
│   │   ├── SerialPortPosix.cpp # whole file inside #if !defined(_WIN32)
│   │   └── SerialPortWin32.cpp # whole file inside #if defined(_WIN32)
│   └── devices/
│       ├── ScannerDevice.h
│       ├── DeviceRegistry.{h,cpp}
│       ├── LineFramer.{h,cpp}  # terminator + silence framing, shared by drivers
│       ├── WaveshareD.cpp
│       └── SerialLine.cpp
├── manual/                   # Waveshare setup manual (PDF)
├── tools/qrprobe.cpp           # stand-alone CLI, no VPX needed (CMake option QR_BUILD_TOOLS)
└── tests/                      # framer + debounce unit tests (CMake option QR_BUILD_TESTS)
```

`tools/` and `tests/` stay outside `src/` because the template compiles
everything under `src/` into the plugin.

## 4. Settings

These appear in VPX's plugin settings and are saved under `[Plugin.QR]` in
`VPinballX.ini`.

| setting            | type   | default         | notes                                                       |
|--------------------|--------|-----------------|-------------------------------------------------------------|
| `Device`           | enum   | `waveshare-d`   | choices come from the device registry                       |
| `Port`             | string | empty           | `/dev/serial/by-id/...`, `/dev/ttyUSB0`, `/dev/cu.usbserial-*`, `COM3` |
| `BaudRate`         | int    | 0 = device default | override for the generic driver                          |
| `Terminator`       | enum   | Auto            | Auto / CR / LF / CRLF / Silence only (generic driver)       |
| `DuplicateWindowMs`| int    | 1500            | drop an identical scan within this window. 0 = off, up to 60000 (60 s) |
| `ScanCooldownMs`   | int    | 0               | after any scan, ignore *all* scans for this long. 0 = off, up to 60000 |
| `ShowNotification` | bool   | false           | VPX pop-up showing each scan; handy for testing             |

Port names by platform:
- **Linux:** prefer `/dev/serial/by-id/...`. It stays the same across reboots
  and replugging, unlike `/dev/ttyUSB0`.
- **macOS:** use the `/dev/cu.*` device, not `/dev/tty.*`, which can block on
  open.
- **Windows:** `COM3` style. The code adds the `\\.\` prefix internally,
  because `COM10` and higher need it.

If `Port` is empty, v1 does nothing except report "no port configured".
Auto-detection is listed under later work because a CP2102 doesn't identify
*what* is attached to it.

## 5. Error handling

- **Port missing or unplugged:** keep retrying every 2 s and broadcast
  `OnStatusChanged` on every connect/disconnect, so the game can react. It
  never blocks or crashes the game.
- **Port busy or permission denied:** report the reason in the status message
  and VPX's log. On Linux, the log tells the user to join the `dialout` group.
- **Junk on the line:** drop control characters other than the terminator,
  cap a scan at 4 KB, and drop anything longer with a warning.
- **Encoding:** the payload is passed through as bytes. The device sends
  ASCII, and other scanners may send UTF-8.

## 6. Steps

Each step ends with something you can run.

1. **Serial layer + probe tool.** Write the `SerialPort` layer and
   `tools/qrprobe`, which sends the "query baud rate" command, then prints a
   hex dump plus the text of each burst it receives. You scan a few QR codes
   to confirm what the manual says: a trailing CR, no prefix, a re-send about
   every 1 s while a code stays in view.
   *Done when:* qrprobe identifies the scanner and shows real scans.
2. **Device layer.** `LineFramer`, the `waveshare-d` and `serial-line`
   drivers, the registry, and unit tests for framing (terminator, silence,
   bytes split across reads, oversize) and debouncing. `qrprobe` switches to
   printing decoded scans through the real driver.
3. **Plugin.** Settings, `ScannerWorker` with reconnect, the main-thread
   hand-off, the `OnScan` / `OnStatusChanged` broadcasts, `GetAPI`, and the
   optional notification. Test in VPX on Linux with `ShowNotification` on.
4. **Consumer check.** A tiny test consumer plugin (`examples/consumer/`,
   built only on request) that subscribes to `OnScan` and logs it, to prove
   the API works from another plugin. Then write the README: what the plugin
   does, settings, supported devices, the consumer API, and how to add a
   driver.
5. **Cross-platform.** Push to GitHub so CI builds all five targets. Test on
   Windows with the scanner on a COM port, and on macOS if a Mac is available.

### Later (not in v1)

- **Payload format.** The bracketed format (`[PLAYER][FIELD1][FIELD2][FIELD3]`)
  is test data and will be finalized later. Until then the plugin passes the
  scanned text through unchanged, plus the ROM name field (decision 6). Once the format is final, the plugin could also
  split it into fields: a new `OnScan:2` message, or a field added to the end
  of `QRScanEvent` with its `version` bumped, so existing consumers keep
  working.

- **REVISIT: table scripts.** Exposing scans to table scripts through
  `ScriptablePlugin.h` (e.g. a `QR.LastScan` property or a scan event the
  script can poll). Deferred on 2026-11-13. Bring it up again once the
  plugin-to-plugin API is working.
- **Port auto-detection**: list the serial ports and send "query baud rate" to
  each. This must be opt-in, because cabinets often have other serial
  hardware (DOF controllers, Pinscape and so on) that shouldn't receive
  unexpected bytes.
- **Output-protocol mode** (`03 <len> <data>`) and **Code ID** parsing for the
  Waveshare D, so consumers can be told the symbology (e.g. QR vs. 1D).
- **Several scanners at once**, one worker per configured device.
- **HID scanners** through a HID transport.

## 7. Decisions

Settled on 2026-11-13:

1. **Table scripts:** not in v1. Listed under "Later" as a REVISIT item.
2. **Duplicates:** a 1.5 s duplicate window is the default. It's configurable
   up to 60 s (`DuplicateWindowMs`), and a separate `ScanCooldownMs` setting
   can enforce a longer pause after *any* scan, not just repeats of the same
   code.
3. **Only during play:** yes. The plugin works only while a table is running,
   which is also when VPX loads it. Scans outside a game are ignored.
4. **Manual:** the Waveshare setup manual is kept in the repo under
   [`manual/`](manual/).
5. **Payload:** passed through raw. The format is test data for now and will
   be finalized later (see "Later").
6. **ROM name field** (added 2026-11-13): every published scan gets the
   running PinMAME ROM name appended as a bracketed field, e.g.
   `[PLAYER][FIELD1][FIELD2][FIELD3][afm_113b]`, or `[]` when the table
   doesn't use PinMAME. The ROM comes from VPX's controller list
   (`Controller` / `GetControllers:1`, entry `pinmame::<rom>`), read at the
   moment of the scan. Duplicate filtering still uses the raw scan.
