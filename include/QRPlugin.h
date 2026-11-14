// license:GPLv3+
#pragma once

// Public API of the QR plugin, for other VPinballX plugins.
//
// Copy this header into your plugin (it only depends on the VPX SDK's
// MsgPlugin.h). The QR plugin reads a QR/barcode scanner while a table is
// running and broadcasts every scan to all plugins:
//
//    unsigned int onScanId = msgApi->GetMsgID(QRPI_NAMESPACE, QRPI_EVT_ON_SCAN);
//    msgApi->SubscribeMsg(endpointId, onScanId, onScan, nullptr);
//
//    void onScan(const unsigned int msgId, void* userData, void* msgData)
//    {
//       const QRScanEvent* scan = static_cast<const QRScanEvent*>(msgData);
//       std::string text(scan->data, scan->length); // copy: only valid during the callback
//    }
//
// Unsubscribe and release the id in your PluginUnload, as for any message.
//
// Threading: every event is broadcast on the plugin API (main) thread, and
// the QRPluginAPI functions must be called from that thread too.
//
// Versioning follows the VPX SDK conventions: message names end in a version
// marker (":1"), and every struct starts with an `int version` that grows when
// fields are appended. Check it before reading a field added in a later version.

#include "plugins/MsgPlugin.h"

#define QRPI_NAMESPACE          "QR"

// Broadcast for every accepted scan (after duplicate filtering).
// msgData: const QRScanEvent*
//
// The published text is the scanned text followed by a ROM name field: the
// name of the running PinMAME ROM in brackets, or "[]" if the table doesn't
// use PinMAME. E.g. scanning "[PLAYER][FIELD1]" on Attack from Mars gives
// "[PLAYER][FIELD1][afm_113b]".
#define QRPI_EVT_ON_SCAN        "OnScan:1"

// Broadcast when the scanner connects or disconnects.
// msgData: const QRStatusEvent*
#define QRPI_EVT_ON_STATUS_CHG  "OnStatusChanged:1"

// Broadcast by a consumer to get the API table.
// msgData: QRPluginAPI** (set to the table, or left untouched if the QR plugin isn't loaded)
#define QRPI_MSG_GET_API        "GetAPI:1"

typedef struct QRScanEvent
{
   int version;              // 1
   const char* data;         // scanned text + "[<rom>]" (see above), NUL-terminated; use `length`
   unsigned int length;      // payload length in bytes, excluding the NUL
   const char* deviceId;     // id of the device driver that read it, e.g. "waveshare-d"
   uint32_t sequence;        // 1 for the first scan after load, +1 for each accepted scan
} QRScanEvent;               // only valid during the callback: copy what you need

typedef struct QRStatusEvent
{
   int version;              // 1
   int connected;            // 1 = scanner port open and being read, 0 = not connected
   const char* deviceId;     // e.g. "waveshare-d"
   const char* port;         // e.g. "/dev/ttyUSB0" or "COM3" (empty if none configured)
   const char* message;      // human-readable detail, e.g. "port not found"
} QRStatusEvent;             // only valid during the callback

typedef struct QRPluginAPI
{
   int version;              // 1

   // 1 if the scanner is connected, 0 otherwise.
   int (MSGPIAPI* IsConnected)();

   // Copies the last accepted scan into buf (NUL-terminated, truncated to
   // bufSize - 1 bytes). Returns the scan's full length, or 0 if there has been
   // no scan since the plugin loaded. sequence, if not null, receives the
   // scan's sequence number. Call with buf = NULL / bufSize = 0 to get the length.
   unsigned int (MSGPIAPI* GetLastScan)(char* buf, unsigned int bufSize, uint32_t* sequence);
} QRPluginAPI;
