// license:GPLv3+
#include "core/ScannerWorker.h"

#include "transport/SerialPort.h"

namespace QR {

ScannerWorker::ScannerWorker(WorkerConfig config, WorkerCallbacks callbacks)
   : m_config(std::move(config))
   , m_callbacks(std::move(callbacks))
{
}

ScannerWorker::~ScannerWorker() { Stop(); }

void ScannerWorker::Start()
{
   if (m_thread.joinable() || m_config.device == nullptr)
      return;
   m_stopping = false;
   m_thread = std::thread(&ScannerWorker::Run, this);
}

void ScannerWorker::Stop()
{
   {
      std::lock_guard lock(m_mutex);
      m_stopping = true;
   }
   m_wake.notify_all();
   if (m_thread.joinable())
      m_thread.join();
}

void ScannerWorker::SetStatus(bool connected, const std::string& message)
{
   if (m_hasStatus && connected == m_connected && message == m_statusMessage)
      return;
   m_hasStatus = true;
   m_connected = connected;
   m_statusMessage = message;
   if (m_callbacks.onStatus)
      m_callbacks.onStatus(connected, message);
}

void ScannerWorker::WaitForRetry()
{
   std::unique_lock lock(m_mutex);
   m_wake.wait_for(lock, m_config.retryInterval, [this] { return m_stopping.load(); });
}

void ScannerWorker::Run()
{
   auto log = [this](LogLevel level, std::string message) {
      if (m_callbacks.onLog)
         m_callbacks.onLog(level, std::move(message));
   };

   while (!m_stopping)
   {
      std::unique_ptr<ScannerDevice> device = m_config.device->create(m_config.options);
      device->SetScanHandler([this](std::string_view scan) {
         if (m_callbacks.onScan)
            m_callbacks.onScan(std::string(scan));
      });
      device->SetLogHandler([&log](std::string_view warning) { log(LogLevel::Warning, std::string(warning)); });

      SerialPort port;
      std::string error;
      const SerialConfig portConfig = device->PortConfig();
      if (!port.Open(m_config.port, portConfig, error))
      {
         SetStatus(false, error);
         WaitForRetry();
         continue;
      }

      std::string message;
      switch (device->Identify(port))
      {
      case IdentifyResult::Confirmed: message = "connected, device identified"; break;
      case IdentifyResult::NotSupported: message = "connected"; break;
      case IdentifyResult::NoAnswer:
         message = "connected, but the device did not answer the identify query (check the Device setting and baud rate)";
         break;
      }
      log(LogLevel::Info, "opened " + m_config.port + " at " + std::to_string(portConfig.baud) + " baud (" + m_config.device->id + ")");
      SetStatus(true, message);

      uint8_t buffer[512];
      while (!m_stopping)
      {
         // Short timeout: keeps Stop() responsive and gives silence-based
         // framing ~50 ms resolution.
         const int n = port.Read(buffer, sizeof(buffer), 50);
         if (n < 0)
         {
            SetStatus(false, "device disconnected");
            break;
         }
         if (n > 0)
            device->OnBytes(buffer, static_cast<size_t>(n), Clock::now());
         else
            device->OnIdle(Clock::now());
      }
      port.Close();
      if (!m_stopping)
         WaitForRetry();
   }
}

} // namespace QR
