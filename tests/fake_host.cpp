// license:GPLv3+
// A minimal stand-in for VPX that loads the built QR plugin and checks it end
// to end: settings, the main-thread hand-off, the QR events and API, and that
// unloading cleans up every subscription and message id.
//
//   qr_fake_host <plugin library>                      lifecycle test (no device), used by ctest
//   qr_fake_host <plugin library> --pty                scan test through a pseudo-terminal (Linux/macOS), used by ctest
//   qr_fake_host <plugin library> <port> [seconds]     real scanner: prints scans as a consumer would see them
//
// The host also plays the PinMAME controller ("pinmame::<rom>" in the
// controller list), so the ROM name field can be checked.
//
// Build with -DQR_BUILD_TESTS=ON.

#include "plugins/LoggingPlugin.h"
#include "plugins/MsgPlugin.h"
#include "plugins/VPXPlugin.h"

#include "QRPlugin.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

constexpr uint32_t kHostEndpoint = 1;
constexpr uint32_t kPluginEndpoint = 2;

int g_failures = 0;
#define CHECK(cond)                                                       \
   do                                                                     \
   {                                                                      \
      if (!(cond))                                                        \
      {                                                                   \
         g_failures++;                                                    \
         std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
      }                                                                   \
   } while (0)

// --- Messaging ---------------------------------------------------------------

struct MsgEntry
{
   std::string name;
   int refs = 0;
};
std::vector<MsgEntry> g_msgs; // id = index + 1

struct Subscription
{
   uint32_t endpoint;
   unsigned int msgId;
   msgpi_msg_callback cb;
   void* ctx;
};
std::vector<Subscription> g_subs;

std::map<std::string, std::string> g_settingValues; // propId -> value (from the command line)
std::vector<std::string> g_registeredSettings;

std::mutex g_queueMutex;
std::deque<std::pair<msgpi_timer_callback, void*>> g_queue;
std::thread::id g_mainThread;

unsigned int MSGPIAPI GetMsgID(const char* nameSpace, const char* name)
{
   const std::string full = std::string(nameSpace) + "." + name;
   for (size_t i = 0; i < g_msgs.size(); i++)
      if (g_msgs[i].name == full)
      {
         g_msgs[i].refs++;
         return static_cast<unsigned int>(i + 1);
      }
   g_msgs.push_back({ full, 1 });
   return static_cast<unsigned int>(g_msgs.size());
}

void MSGPIAPI ReleaseMsgID(const unsigned int msgId)
{
   CHECK(msgId >= 1 && msgId <= g_msgs.size() && g_msgs[msgId - 1].refs > 0);
   if (msgId >= 1 && msgId <= g_msgs.size())
      g_msgs[msgId - 1].refs--;
}

void MSGPIAPI SubscribeMsg(const uint32_t endpointId, const unsigned int msgId, const msgpi_msg_callback callback, void* context)
{
   g_subs.push_back({ endpointId, msgId, callback, context });
}

void MSGPIAPI UnsubscribeMsg(const unsigned int msgId, const msgpi_msg_callback callback, void* context)
{
   const auto it = std::find_if(g_subs.begin(), g_subs.end(), [&](const Subscription& s) { return s.msgId == msgId && s.cb == callback && s.ctx == context; });
   CHECK(it != g_subs.end());
   if (it != g_subs.end())
      g_subs.erase(it);
}

void MSGPIAPI BroadcastMsg(const uint32_t, const unsigned int msgId, void* data)
{
   CHECK(std::this_thread::get_id() == g_mainThread); // the plugin API is main-thread only
   const auto subs = g_subs;
   for (const Subscription& s : subs)
      if (s.msgId == msgId)
         s.cb(msgId, s.ctx, data);
}

void MSGPIAPI SendMsg(const uint32_t endpointId, const unsigned int msgId, const uint32_t targetEndpointId, void* data)
{
   (void)endpointId;
   const auto subs = g_subs;
   for (const Subscription& s : subs)
      if (s.msgId == msgId && s.endpoint == targetEndpointId)
         s.cb(msgId, s.ctx, data);
}

unsigned int MSGPIAPI GetPluginEndpoint(const char* id) { return std::strcmp(id, "QR") == 0 ? kPluginEndpoint : 0; }

void MSGPIAPI GetEndpointInfo(const uint32_t endpointId, MsgEndpointInfo* info)
{
   *info = {};
   if (endpointId == kPluginEndpoint)
   {
      info->id = "QR";
      info->name = "QR Scanner";
   }
}

void MSGPIAPI RegisterSetting(const uint32_t, MsgSettingDef* def)
{
   g_registeredSettings.emplace_back(def->propId);
   const auto it = g_settingValues.find(def->propId);
   const bool has = it != g_settingValues.end();
   switch (def->type)
   {
   case MSGPI_SETTING_TYPE_INT: def->intDef.Set(has ? std::atoi(it->second.c_str()) : def->intDef.defVal); break;
   case MSGPI_SETTING_TYPE_BOOL: def->boolDef.Set(has ? std::atoi(it->second.c_str()) : def->boolDef.defVal); break;
   case MSGPI_SETTING_TYPE_FLOAT: def->floatDef.Set(has ? static_cast<float>(std::atof(it->second.c_str())) : def->floatDef.defVal); break;
   case MSGPI_SETTING_TYPE_STRING: def->stringDef.Set(has ? it->second.c_str() : def->stringDef.defVal); break;
   default: break;
   }
}

void MSGPIAPI SaveSetting(const uint32_t, MsgSettingDef*) { }

void MSGPIAPI RunOnMainThread(const uint32_t, const double, const msgpi_timer_callback callback, void* userData)
{
   std::lock_guard lock(g_queueMutex);
   g_queue.emplace_back(callback, userData);
}

void ProcessQueue()
{
   for (;;)
   {
      std::pair<msgpi_timer_callback, void*> item;
      {
         std::lock_guard lock(g_queueMutex);
         if (g_queue.empty())
            return;
         item = g_queue.front();
         g_queue.pop_front();
      }
      item.first(item.second);
   }
}

void MSGPIAPI FlushPendingCallbacks(const uint32_t) { ProcessQueue(); }

MsgPluginAPI g_msgApi {
   .version = 1,
   .GetPluginEndpoint = &GetPluginEndpoint,
   .GetEndpointInfo = &GetEndpointInfo,
   .GetMsgID = &GetMsgID,
   .SubscribeMsg = &SubscribeMsg,
   .UnsubscribeMsg = &UnsubscribeMsg,
   .BroadcastMsg = &BroadcastMsg,
   .SendMsg = &SendMsg,
   .ReleaseMsgID = &ReleaseMsgID,
   .RegisterSetting = &RegisterSetting,
   .SaveSetting = &SaveSetting,
   .RunOnMainThread = &RunOnMainThread,
   .FlushPendingCallbacks = &FlushPendingCallbacks,
};

// --- Host services the plugin asks for -----------------------------------------

unsigned int MSGPIAPI PushNotification(const char* msg, const int) { std::printf("  [notification] %s\n", msg); return 1; }
VPXPluginAPI g_vpxApi {};

void MSGPIAPI Log(const char* source, const char*, int, unsigned int level, const char* message)
{
   const char* lvl = level >= LPI_LVL_ERROR ? "ERROR" : level >= LPI_LVL_WARN ? "WARN" : level >= LPI_LVL_INFO ? "INFO" : "DEBUG";
   std::printf("  [log %s %s] %s\n", source, lvl, message);
}
LoggingPluginAPI g_logApi { 1, &Log };

void MSGPIAPI OnGetVpxApi(const unsigned int, void*, void* data) { *static_cast<VPXPluginAPI**>(data) = &g_vpxApi; }
void MSGPIAPI OnGetLogApi(const unsigned int, void*, void* data) { *static_cast<LoggingPluginAPI**>(data) = &g_logApi; }

// --- The host acting as a QR consumer ------------------------------------------

int g_scans = 0, g_statusEvents = 0, g_lastConnected = -1;
std::string g_lastStatusMessage;
std::string g_lastScanData;

// --- The host acting as a controller (PinMAME) ---------------------------------

std::vector<std::string> g_controllerGameIds; // e.g. "pinmame::afm_113b"

void MSGPIAPI OnGetControllers(const unsigned int, void*, void* data)
{
   auto* msg = static_cast<GetControllersMsg*>(data);
   for (const std::string& id : g_controllerGameIds)
   {
      if (msg->count < msg->maxEntryCount)
         msg->entries[msg->count] = ControllerDef { kHostEndpoint + 10 + msg->count, id.c_str() };
      msg->count++;
   }
}

void MSGPIAPI OnScan(const unsigned int, void*, void* data)
{
   const auto* e = static_cast<const QRScanEvent*>(data);
   CHECK(e->version >= 1);
   CHECK(e->data[e->length] == '\0');
   g_scans++;
   g_lastScanData.assign(e->data, e->length);
   std::printf("CONSUMER got OnScan #%u from %s (%u bytes): %.*s\n", e->sequence, e->deviceId, e->length, static_cast<int>(e->length), e->data);
}

void MSGPIAPI OnStatus(const unsigned int, void*, void* data)
{
   const auto* e = static_cast<const QRStatusEvent*>(data);
   CHECK(e->version >= 1);
   g_statusEvents++;
   g_lastConnected = e->connected;
   g_lastStatusMessage = e->message;
   std::printf("CONSUMER got OnStatusChanged: connected=%d port='%s' device=%s message='%s'\n", e->connected, e->port, e->deviceId, e->message);
}

// --- Plugin loading ----------------------------------------------------------

using LoadFn = void(MSGPIAPI*)(const uint32_t, const MsgPluginAPI*);
using UnloadFn = void(MSGPIAPI*)();

struct Library
{
   void* handle = nullptr;
   bool Open(const char* path)
   {
#ifdef _WIN32
      handle = LoadLibraryA(path);
#else
      handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
      if (!handle)
         std::printf("dlopen: %s\n", dlerror());
#endif
      return handle != nullptr;
   }
   void* Get(const char* name)
   {
#ifdef _WIN32
      return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
      return dlsym(handle, name);
#endif
   }
   void Close()
   {
#ifdef _WIN32
      FreeLibrary(static_cast<HMODULE>(handle));
#else
      dlclose(handle);
#endif
   }
};

} // namespace

int main(int argc, char** argv)
{
   if (argc < 2)
   {
      std::printf("usage: qr_fake_host <plugin library> [port [seconds]]\n");
      return 2;
   }
   g_mainThread = std::this_thread::get_id();
   const bool ptyMode = argc > 2 && std::strcmp(argv[2], "--pty") == 0;
   const char* port = argc > 2 && !ptyMode ? argv[2] : nullptr;
   const double seconds = argc > 3 ? std::atof(argv[3]) : 30.0;
   if (port)
   {
      g_settingValues["Port"] = port;
      g_settingValues["ShowNotification"] = "1";
      g_controllerGameIds = { "pinmame::afm_113b" }; // pretend Attack from Mars is running
   }

#ifndef _WIN32
   // --pty: a pseudo-terminal stands in for the scanner. We write to the
   // master side; the plugin opens the slave side as its serial port.
   int ptyMaster = -1;
   std::string ptyName;
   if (ptyMode)
   {
      ptyMaster = posix_openpt(O_RDWR | O_NOCTTY);
      if (ptyMaster < 0 || grantpt(ptyMaster) != 0 || unlockpt(ptyMaster) != 0 || ptsname(ptyMaster) == nullptr)
      {
         std::printf("Cannot create a pseudo-terminal\n");
         return 1;
      }
      ptyName = ptsname(ptyMaster);
      g_settingValues["Port"] = ptyName;
      g_settingValues["Device"] = "1"; // generic serial: no identify query to wait for
   }
#else
   if (ptyMode)
   {
      std::printf("--pty is not supported on Windows\n");
      return 0;
   }
#endif

   g_vpxApi.version = 1;
   g_vpxApi.PushNotification = &PushNotification;

   // Host-side subscriptions (stay for the whole run).
   const unsigned int vpxApiId = GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_API);
   const unsigned int logApiId = GetMsgID(LOGPI_NAMESPACE, LOGPI_MSG_GET_API);
   const unsigned int scanId = GetMsgID(QRPI_NAMESPACE, QRPI_EVT_ON_SCAN);
   const unsigned int statusId = GetMsgID(QRPI_NAMESPACE, QRPI_EVT_ON_STATUS_CHG);
   SubscribeMsg(kHostEndpoint, vpxApiId, &OnGetVpxApi, nullptr);
   SubscribeMsg(kHostEndpoint, logApiId, &OnGetLogApi, nullptr);
   SubscribeMsg(kHostEndpoint, scanId, &OnScan, nullptr);
   SubscribeMsg(kHostEndpoint, statusId, &OnStatus, nullptr);
   const unsigned int controllersId = GetMsgID(CTLPI_NAMESPACE, CTLPI_CONTROLLERS_GET_MSG);
   SubscribeMsg(kHostEndpoint, controllersId, &OnGetControllers, nullptr);
   const size_t hostSubs = g_subs.size();
   std::vector<int> hostRefs;
   for (const MsgEntry& m : g_msgs)
      hostRefs.push_back(m.refs);

   Library lib;
   if (!lib.Open(argv[1]))
   {
      std::printf("Cannot load %s\n", argv[1]);
      return 1;
   }
   const auto load = reinterpret_cast<LoadFn>(lib.Get("QRPluginLoad"));
   const auto unload = reinterpret_cast<UnloadFn>(lib.Get("QRPluginUnload"));
   CHECK(load && unload);
   if (!load || !unload)
      return 1;

   const std::string portSetting = g_settingValues.count("Port") ? g_settingValues["Port"] : std::string();
   std::printf("Loading plugin%s\n", portSetting.empty() ? " (no port configured)" : (" with Port=" + portSetting).c_str());
   load(kPluginEndpoint, &g_msgApi);

   for (const char* s : { "Device", "Port", "BaudRate", "Terminator", "DuplicateWindowMs", "ScanCooldownMs", "ShowNotification" })
      CHECK(std::find(g_registeredSettings.begin(), g_registeredSettings.end(), s) != g_registeredSettings.end());

   // A consumer asks for the API table.
   QRPluginAPI* qr = nullptr;
   const unsigned int getApiId = GetMsgID(QRPI_NAMESPACE, QRPI_MSG_GET_API);
   BroadcastMsg(kHostEndpoint, getApiId, &qr);
   ReleaseMsgID(getApiId);
   CHECK(qr != nullptr);
   if (qr)
   {
      CHECK(qr->version >= 1);
      uint32_t seq = 99;
      char buf[16];
      CHECK(qr->GetLastScan(buf, sizeof(buf), &seq) == 0 && seq == 0 && buf[0] == '\0');
   }

   // Runs the host main loop until cond() is true or timeoutMs has passed.
   auto pumpUntil = [](const std::function<bool()>& cond, int timeoutMs) {
      const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
      while (!cond() && std::chrono::steady_clock::now() < end)
      {
         ProcessQueue();
         std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      ProcessQueue();
      return cond();
   };

   if (ptyMode)
   {
#ifndef _WIN32
      auto send = [&](const std::string& text) { CHECK(write(ptyMaster, text.data(), text.size()) == static_cast<ssize_t>(text.size())); };

      CHECK(pumpUntil([] { return g_lastConnected == 1; }, 3000));

      // PinMAME running: the ROM name is appended (a non-PinMAME controller is ignored).
      g_controllerGameIds = { "other::something", "pinmame::afm_113b" };
      send("[PLAYER][FIELD1][FIELD2][FIELD3]\r");
      CHECK(pumpUntil([] { return g_scans == 1; }, 2000));
      CHECK(g_lastScanData == "[PLAYER][FIELD1][FIELD2][FIELD3][afm_113b]");

      // Same code again right away: filtered as a duplicate (on the raw scan).
      send("[PLAYER][FIELD1][FIELD2][FIELD3]\r");
      pumpUntil([] { return false; }, 300);
      CHECK(g_scans == 1);

      // No PinMAME ROM: empty field.
      g_controllerGameIds.clear();
      send("[PLAYER][OTHER]\r");
      CHECK(pumpUntil([] { return g_scans == 2; }, 2000));
      CHECK(g_lastScanData == "[PLAYER][OTHER][]");

      if (qr)
      {
         char buf[64];
         uint32_t seq = 0;
         CHECK(qr->GetLastScan(buf, sizeof(buf), &seq) == g_lastScanData.size() && seq == 2 && g_lastScanData == buf);
         CHECK(qr->IsConnected() == 1);
      }
#endif
   }
   else if (!port)
   {
      // No port: the plugin must report that right away and not start reading.
      ProcessQueue();
      CHECK(g_statusEvents == 1 && g_lastConnected == 0);
      CHECK(g_lastStatusMessage.find("no port configured") != std::string::npos);
      CHECK(qr && qr->IsConnected() == 0);
   }
   else
   {
      std::printf("Reading for %.0f s: scan some codes now (Ctrl+C to abort)\n", seconds);
      const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
      while (std::chrono::steady_clock::now() < end)
      {
         ProcessQueue(); // the host's main loop
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (qr)
      {
         char buf[256];
         uint32_t seq = 0;
         const unsigned int len = qr->GetLastScan(buf, sizeof(buf), &seq);
         std::printf("GetLastScan: len=%u seq=%u text='%s', IsConnected=%d\n", len, seq, buf, qr->IsConnected());
         CHECK(static_cast<int>(seq) == g_scans);
      }
   }

   std::printf("Unloading plugin\n");
   unload();
   CHECK(g_queue.empty());

   // Everything the plugin subscribed to or acquired must be released.
   CHECK(g_subs.size() == hostSubs);
   for (size_t i = 0; i < g_msgs.size(); i++)
   {
      const int expected = i < hostRefs.size() ? hostRefs[i] : 0;
      if (g_msgs[i].refs != expected)
         std::printf("Message '%s' has %d refs after unload, expected %d\n", g_msgs[i].name.c_str(), g_msgs[i].refs, expected);
      CHECK(g_msgs[i].refs == expected);
   }

   lib.Close();
#ifndef _WIN32
   if (ptyMaster >= 0)
      close(ptyMaster);
#endif
   std::printf("%s: %d scan(s), %d status event(s), %d failure(s)\n", g_failures ? "FAILED" : "OK", g_scans, g_statusEvents, g_failures);
   return g_failures ? 1 : 0;
}
