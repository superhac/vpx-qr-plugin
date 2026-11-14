// license:GPLv3+
#pragma once

#include "transport/SerialPort.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace QR {

using Clock = std::chrono::steady_clock;

// How a scan ends on the wire.
enum class Terminator
{
   Auto,        // CR or LF (so CRLF works too), or a short silence
   CR,          // CR only; LF is part of the data
   LF,          // LF only; CR is part of the data
   CRLF,        // the CR LF pair only
   SilenceOnly, // a short silence on the line (devices with no terminator)
};

// User options that apply to any driver (from the plugin settings).
struct DeviceOptions
{
   int baud = 0;                          // 0 = the device's default
   Terminator terminator = Terminator::Auto;
};

// Result of checking that the expected device is on the port.
enum class IdentifyResult
{
   NotSupported, // this driver has no way to check
   Confirmed,    // the device answered as expected
   NoAnswer,     // no (valid) answer; it may be another device, or a wiring issue
};

// A scanner driver: turns the bytes a device sends into scans.
// Drivers are created per connection and used from the worker thread only.
class ScannerDevice
{
public:
   using ScanHandler = std::function<void(std::string_view scan)>;
   using LogHandler = std::function<void(std::string_view warning)>;

   virtual ~ScannerDevice() = default;

   // Serial settings to open the port with.
   virtual SerialConfig PortConfig() const = 0;

   // Called right after the port is opened. A driver may send a harmless
   // query here. Scan bytes that arrive meanwhile must still be processed.
   virtual IdentifyResult Identify(Transport& transport) { (void)transport; return IdentifyResult::NotSupported; }

   // Bytes received from the device.
   virtual void OnBytes(const uint8_t* data, size_t size, Clock::time_point now) = 0;

   // Called when a read timed out with no data, so silence-based framing can
   // finish a scan.
   virtual void OnIdle(Clock::time_point now) = 0;

   void SetScanHandler(ScanHandler handler) { m_onScan = std::move(handler); }
   void SetLogHandler(LogHandler handler) { m_onLog = std::move(handler); }

protected:
   void EmitScan(std::string_view scan) const { if (m_onScan) m_onScan(scan); }
   void LogWarning(std::string_view warning) const { if (m_onLog) m_onLog(warning); }

private:
   ScanHandler m_onScan;
   LogHandler m_onLog;
};

} // namespace QR
