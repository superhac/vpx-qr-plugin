// license:GPLv3+
//
// Example consumer of the QR plugin's API: a separate plugin that receives
// every scan and status change, logs it, and shows a VPX notification.
// Copy this pattern (and include/QRPlugin.h) into your own plugin.
//
// Build with -DQR_BUILD_EXAMPLES=ON, install the QRConsumer/ folder next to
// QR/ in VPX's plugins folder, and enable both plugins.

#include "plugin_id.h"

#include "plugins/LoggingPlugin.h"
#include "plugins/MsgPlugin.h"
#include "plugins/VPXPlugin.h"

#include "QRPlugin.h"

#include <string>

using namespace std::string_literals;

LPI_USE_CPP();
LPI_IMPLEMENT_CPP

namespace {

const MsgPluginAPI* msgApi = nullptr;
VPXPluginAPI* vpxApi = nullptr;
uint32_t endpointId = 0;
unsigned int getVpxApiId = 0, onScanId = 0, onStatusId = 0, onGameStartId = 0;

void MSGPIAPI OnScan(const unsigned int, void*, void* msgData)
{
   const auto* scan = static_cast<const QRScanEvent*>(msgData);
   const std::string text(scan->data, scan->length); // copy: only valid during this callback
   LPI_LOGI_CPP("Received scan #"s + std::to_string(scan->sequence) + " from " + scan->deviceId + ": " + text);
   if (vpxApi)
      vpxApi->PushNotification(("Consumer got: "s + text).c_str(), 4000);
}

void MSGPIAPI OnStatus(const unsigned int, void*, void* msgData)
{
   const auto* status = static_cast<const QRStatusEvent*>(msgData);
   LPI_LOGI_CPP("Scanner "s + (status->connected ? "connected" : "disconnected") + " (" + status->port + "): " + status->message);
}

void MSGPIAPI OnGameStart(const unsigned int, void*, void*)
{
   // The QR plugin may have loaded before or after us, so ask for its state
   // instead of relying on having seen its status event.
   QRPluginAPI* qr = nullptr;
   const unsigned int getQrApiId = msgApi->GetMsgID(QRPI_NAMESPACE, QRPI_MSG_GET_API);
   msgApi->BroadcastMsg(endpointId, getQrApiId, &qr);
   msgApi->ReleaseMsgID(getQrApiId);
   if (qr == nullptr)
      LPI_LOGW_CPP("QR plugin not loaded (enable it in the plugin settings)"s);
   else
      LPI_LOGI_CPP("QR plugin found, scanner "s + (qr->IsConnected() ? "connected" : "not connected"));
}

} // namespace

MSGPI_EXPORT void MSGPIAPI PLUGIN_LOAD(const uint32_t sessionId, const MsgPluginAPI* api)
{
   msgApi = api;
   endpointId = sessionId;
   LPISetup(endpointId, msgApi);
   msgApi->BroadcastMsg(endpointId, getVpxApiId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_API), &vpxApi);
   msgApi->SubscribeMsg(endpointId, onScanId = msgApi->GetMsgID(QRPI_NAMESPACE, QRPI_EVT_ON_SCAN), &OnScan, nullptr);
   msgApi->SubscribeMsg(endpointId, onStatusId = msgApi->GetMsgID(QRPI_NAMESPACE, QRPI_EVT_ON_STATUS_CHG), &OnStatus, nullptr);
   msgApi->SubscribeMsg(endpointId, onGameStartId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_EVT_ON_GAME_START), &OnGameStart, nullptr);
}

MSGPI_EXPORT void MSGPIAPI PLUGIN_UNLOAD()
{
   msgApi->UnsubscribeMsg(onScanId, &OnScan, nullptr);
   msgApi->UnsubscribeMsg(onStatusId, &OnStatus, nullptr);
   msgApi->UnsubscribeMsg(onGameStartId, &OnGameStart, nullptr);
   msgApi->ReleaseMsgID(getVpxApiId);
   msgApi->ReleaseMsgID(onScanId);
   msgApi->ReleaseMsgID(onStatusId);
   msgApi->ReleaseMsgID(onGameStartId);
   vpxApi = nullptr;
   msgApi = nullptr;
}
