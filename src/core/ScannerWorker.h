// license:GPLv3+
#pragma once

#include "devices/DeviceRegistry.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace QR {

enum class LogLevel { Info, Warning, Error };

struct WorkerConfig
{
   std::string port;
   const DeviceEntry* device = nullptr;
   DeviceOptions options;
   std::chrono::milliseconds retryInterval { 2000 };
};

// All callbacks run on the worker thread. The owner must hand data over to
// its own thread as needed (the plugin uses RunOnMainThread).
struct WorkerCallbacks
{
   std::function<void(std::string scan)> onScan;
   std::function<void(bool connected, std::string message)> onStatus; // only on change
   std::function<void(LogLevel level, std::string message)> onLog;
};

// Background thread: opens the port, reads scans through the device driver,
// and reconnects every retryInterval if the port is missing or unplugged.
class ScannerWorker
{
public:
   ScannerWorker(WorkerConfig config, WorkerCallbacks callbacks);
   ~ScannerWorker();
   ScannerWorker(const ScannerWorker&) = delete;
   ScannerWorker& operator=(const ScannerWorker&) = delete;

   void Start();
   void Stop(); // blocks until the thread has exited; no callbacks after it returns

private:
   void Run();
   void SetStatus(bool connected, const std::string& message);
   void WaitForRetry();

   WorkerConfig m_config;
   WorkerCallbacks m_callbacks;
   std::thread m_thread;
   std::atomic<bool> m_stopping { false };
   std::mutex m_mutex;
   std::condition_variable m_wake;

   bool m_hasStatus = false;
   bool m_connected = false;
   std::string m_statusMessage;
};

} // namespace QR
