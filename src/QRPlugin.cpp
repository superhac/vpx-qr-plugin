// license:GPLv3+
//
// QR plugin: reads a QR/barcode scanner while a table is running and
// broadcasts each scan to other plugins (see include/QRPlugin.h).
//
// Threading: the serial port is read on a background thread (ScannerWorker).
// Everything it produces (scans, status changes, log lines) is handed to the
// main thread with RunOnMainThread, and all VPX/plugin API calls happen there.

#include "plugin_id.h"

#include "plugins/LoggingPlugin.h"
#include "plugins/MsgPlugin.h"
#include "plugins/VPXPlugin.h"

#include "QRPlugin.h"

#include "core/ScanFilter.h"
#include "core/ScannerWorker.h"
#include "devices/DeviceRegistry.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace std::string_literals;

LPI_USE_CPP();
LPI_IMPLEMENT_CPP

#define LOGD LPI_LOGD_CPP
#define LOGI LPI_LOGI_CPP
#define LOGW LPI_LOGW_CPP
#define LOGE LPI_LOGE_CPP

namespace {

const MsgPluginAPI* msgApi = nullptr;
VPXPluginAPI* vpxApi = nullptr;
uint32_t endpointId = 0;
unsigned int getVpxApiId = 0, onScanId = 0, onStatusId = 0, getApiId = 0;

// ---------------------------------------------------------------------------
// Settings, saved by VPX under [Plugin.QR] in VPinballX.ini.
// Port, device, baud rate and terminator take effect when the next table starts.
// ---------------------------------------------------------------------------

MSGPI_STRING_VAL_SETTING(portProp, "Port", "Serial port",
   "Serial port of the scanner. Windows: COM3. Linux: /dev/serial/by-id/... (stable) or /dev/ttyUSB0. macOS: /dev/cu.usbserial-...",
   true, "", 512);

// The device list comes from the driver registry (see Devices()). VPX saves
// the position in that list.
int deviceProp_Val = 0;
int MSGPIAPI deviceProp_Get() { return deviceProp_Val; }
void MSGPIAPI deviceProp_Set(int v) { deviceProp_Val = v; }

const char* baudValues[] = { "Device default", "1200", "2400", "4800", "9600", "14400", "19200", "38400", "57600", "115200" };
constexpr int baudRates[] = { 0, 1200, 2400, 4800, 9600, 14400, 19200, 38400, 57600, 115200 };
constexpr int baudCount = static_cast<int>(sizeof(baudRates) / sizeof(baudRates[0]));
MSGPI_ENUM_VAL_SETTING(baudProp, "BaudRate", "Baud rate",
   "Serial speed. 'Device default' uses the selected device's factory setting (9600 for the Waveshare D).",
   true, 0, baudCount, baudValues, 0);

const char* terminatorValues[] = { "Auto (CR, LF or pause)", "CR", "LF", "CRLF", "Pause only" };
MSGPI_ENUM_VAL_SETTING(terminatorProp, "Terminator", "Scan terminator",
   "How the end of a scan is detected. Auto works for almost every scanner.",
   true, 0, 5, terminatorValues, 0);

MSGPI_INT_VAL_SETTING(duplicateProp, "DuplicateWindowMs", "Duplicate window (ms)",
   "Ignore the same code again within this time. A code held in front of the scanner counts once. 0 = off.",
   true, 0, 60000, 1500);

MSGPI_INT_VAL_SETTING(cooldownProp, "ScanCooldownMs", "Scan cooldown (ms)",
   "After a scan, ignore all scans (same or different code) for this time. 0 = off.",
   true, 0, 60000, 0);

MSGPI_BOOL_VAL_SETTING(notifyProp, "ShowNotification", "Show notification",
   "Show a VPX notification for every scan and connection change. Useful for testing.",
   true, false);

void RegisterSettings()
{
   // Built at runtime because the choices come from the driver registry.
   static MsgSettingDef deviceProp {
      .propId = "Device",
      .name = "Device",
      .description = "Scanner model (driver).",
      .isUserEditable = 1,
      .type = MSGPI_SETTING_TYPE_INT,
      .intDef = { 0, static_cast<int>(QR::Devices().size()) - 1, 0, QR::DeviceNames(), &deviceProp_Get, &deviceProp_Set },
   };
   msgApi->RegisterSetting(endpointId, &deviceProp);
   msgApi->RegisterSetting(endpointId, &portProp);
   msgApi->RegisterSetting(endpointId, &baudProp);
   msgApi->RegisterSetting(endpointId, &terminatorProp);
   msgApi->RegisterSetting(endpointId, &duplicateProp);
   msgApi->RegisterSetting(endpointId, &cooldownProp);
   msgApi->RegisterSetting(endpointId, &notifyProp);
}

// ---------------------------------------------------------------------------
// State (main thread only)
// ---------------------------------------------------------------------------

std::unique_ptr<QR::ScannerWorker> worker;
std::atomic<bool> unloading { false };
QR::ScanFilter filter;
std::string deviceId;
std::string portName;
std::string lastScan;
uint32_t sequence = 0;
bool connected = false;

// For logs and notifications: control characters shown as '.', long text cut.
std::string Printable(const std::string& s, size_t maxLength = 200)
{
   std::string out;
   for (const char c : s.substr(0, maxLength))
      out.push_back((static_cast<unsigned char>(c) < 0x20 || c == 0x7F) ? '.' : c);
   if (s.size() > maxLength)
      out += "...";
   return out;
}

void Notify(const std::string& text)
{
   if (notifyProp_Val && vpxApi)
      vpxApi->PushNotification(text.c_str(), 3000);
}

void HandleScan(const std::string& scan)
{
   using std::chrono::milliseconds;
   if (!filter.Accept(scan, QR::Clock::now(), milliseconds(duplicateProp_Val), milliseconds(cooldownProp_Val)))
   {
      LOGD("Filtered (duplicate or cooldown): "s + Printable(scan));
      return;
   }
   sequence++;
   lastScan = scan;
   LOGI("Scan #"s + std::to_string(sequence) + ": " + Printable(scan));

   const QRScanEvent event { 1, lastScan.c_str(), static_cast<unsigned int>(lastScan.size()), deviceId.c_str(), sequence };
   msgApi->BroadcastMsg(endpointId, onScanId, const_cast<QRScanEvent*>(&event));
   Notify("QR: "s + Printable(scan, 60));
}

void HandleStatus(bool isConnected, const std::string& message)
{
   connected = isConnected;
   const std::string text = (portName.empty() ? "QR scanner"s : portName) + ": " + message;
   if (isConnected)
      LOGI(text);
   else
      LOGW(text);

   const QRStatusEvent event { 1, isConnected ? 1 : 0, deviceId.c_str(), portName.c_str(), message.c_str() };
   msgApi->BroadcastMsg(endpointId, onStatusId, const_cast<QRStatusEvent*>(&event));
   Notify("QR scanner "s + (isConnected ? "connected" : "not connected: " + message));
}

// ---------------------------------------------------------------------------
// Worker thread -> main thread hand-off
// ---------------------------------------------------------------------------

struct Posted
{
   enum class Kind { Scan, Status, Log } kind;
   std::string text;
   bool connected = false;
   QR::LogLevel level = QR::LogLevel::Info;
};

void MSGPIAPI OnPosted(void* userData)
{
   const std::unique_ptr<Posted> p(static_cast<Posted*>(userData));
   switch (p->kind)
   {
   case Posted::Kind::Log:
      switch (p->level)
      {
      case QR::LogLevel::Info: LOGI(p->text); break;
      case QR::LogLevel::Warning: LOGW(p->text); break;
      case QR::LogLevel::Error: LOGE(p->text); break;
      }
      break;
   case Posted::Kind::Scan:
      if (!unloading)
         HandleScan(p->text);
      break;
   case Posted::Kind::Status:
      if (!unloading)
         HandleStatus(p->connected, p->text);
      break;
   }
}

// Called from the worker thread. RunOnMainThread is the one API function that
// is safe to call from any thread.
void Post(Posted* p) { msgApi->RunOnMainThread(endpointId, 0.0, &OnPosted, p); }

// ---------------------------------------------------------------------------
// API table for consumers (QRPI_MSG_GET_API)
// ---------------------------------------------------------------------------

int MSGPIAPI ApiIsConnected() { return connected ? 1 : 0; }

unsigned int MSGPIAPI ApiGetLastScan(char* buf, unsigned int bufSize, uint32_t* seq)
{
   if (seq)
      *seq = sequence;
   if (sequence == 0)
   {
      if (buf && bufSize)
         buf[0] = '\0';
      return 0;
   }
   if (buf && bufSize)
   {
      const size_t n = std::min<size_t>(bufSize - 1, lastScan.size());
      std::memcpy(buf, lastScan.data(), n);
      buf[n] = '\0';
   }
   return static_cast<unsigned int>(lastScan.size());
}

QRPluginAPI qrApi { 1, &ApiIsConnected, &ApiGetLastScan };

void MSGPIAPI OnGetApi(const unsigned int, void*, void* msgData)
{
   if (msgData)
      *static_cast<QRPluginAPI**>(msgData) = &qrApi;
}

std::string Trim(const std::string& s)
{
   const size_t b = s.find_first_not_of(" \t\r\n");
   if (b == std::string::npos)
      return {};
   const size_t e = s.find_last_not_of(" \t\r\n");
   return s.substr(b, e - b + 1);
}

} // namespace

MSGPI_EXPORT void MSGPIAPI PLUGIN_LOAD(const uint32_t sessionId, const MsgPluginAPI* api)
{
   msgApi = api;
   endpointId = sessionId;
   unloading = false;
   filter.Reset();
   lastScan.clear();
   sequence = 0;
   connected = false;

   LPISetup(endpointId, msgApi);
   RegisterSettings();

   msgApi->BroadcastMsg(endpointId, getVpxApiId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_API), &vpxApi);
   onScanId = msgApi->GetMsgID(QRPI_NAMESPACE, QRPI_EVT_ON_SCAN);
   onStatusId = msgApi->GetMsgID(QRPI_NAMESPACE, QRPI_EVT_ON_STATUS_CHG);
   msgApi->SubscribeMsg(endpointId, getApiId = msgApi->GetMsgID(QRPI_NAMESPACE, QRPI_MSG_GET_API), &OnGetApi, nullptr);

   const auto& devices = QR::Devices();
   const QR::DeviceEntry& device = devices[std::clamp(deviceProp_Val, 0, static_cast<int>(devices.size()) - 1)];
   deviceId = device.id;
   portName = Trim(portProp_Val);

   QR::WorkerConfig config;
   config.port = portName;
   config.device = &device;
   config.options.baud = baudRates[std::clamp(baudProp_Val, 0, baudCount - 1)];
   config.options.terminator = static_cast<QR::Terminator>(std::clamp(terminatorProp_Val, 0, 4));

   if (portName.empty())
   {
      HandleStatus(false, "no port configured (set Port in the QR plugin settings)");
   }
   else
   {
      LOGI("Starting "s + device.name + " on " + portName);
      QR::WorkerCallbacks callbacks;
      callbacks.onScan = [](std::string scan) { Post(new Posted { Posted::Kind::Scan, std::move(scan) }); };
      callbacks.onStatus = [](bool isConnected, std::string message) { Post(new Posted { Posted::Kind::Status, std::move(message), isConnected }); };
      callbacks.onLog = [](QR::LogLevel level, std::string message) { Post(new Posted { Posted::Kind::Log, std::move(message), false, level }); };
      worker = std::make_unique<QR::ScannerWorker>(std::move(config), std::move(callbacks));
      worker->Start();
   }
}

MSGPI_EXPORT void MSGPIAPI PLUGIN_UNLOAD()
{
   // Stop producing callbacks, then run the ones still queued (scans and
   // status changes are dropped at this point, log lines still go out).
   unloading = true;
   if (worker)
   {
      worker->Stop();
      worker.reset();
   }
   msgApi->FlushPendingCallbacks(endpointId);

   msgApi->UnsubscribeMsg(getApiId, &OnGetApi, nullptr);
   msgApi->ReleaseMsgID(getVpxApiId);
   msgApi->ReleaseMsgID(onScanId);
   msgApi->ReleaseMsgID(onStatusId);
   msgApi->ReleaseMsgID(getApiId);
   vpxApi = nullptr;
   msgApi = nullptr;
}
