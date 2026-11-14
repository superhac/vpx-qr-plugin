// license:GPLv3+
// Driver: generic serial scanner that sends each scan as a line of text.
// Covers most "serial/ASCII" scanners. Baud and terminator come from the
// plugin settings (default 9600 8N1, Auto terminator). Listen-only.
#include "devices/LineFramer.h"
#include "devices/ScannerDevice.h"

namespace QR {

namespace {

class SerialLine final : public ScannerDevice
{
public:
   explicit SerialLine(const DeviceOptions& options)
      : m_options(options)
      , m_framer({ .terminator = options.terminator },
           [this](std::string_view frame) { EmitScan(frame); },
           [this](std::string_view reason) { LogWarning(reason); })
   {
   }

   SerialConfig PortConfig() const override
   {
      SerialConfig config;
      config.baud = m_options.baud > 0 ? m_options.baud : 9600;
      return config;
   }

   void OnBytes(const uint8_t* data, size_t size, Clock::time_point now) override { m_framer.Feed(data, size, now); }
   void OnIdle(Clock::time_point now) override { m_framer.Idle(now); }

private:
   DeviceOptions m_options;
   LineFramer m_framer;
};

} // namespace

std::unique_ptr<ScannerDevice> CreateSerialLine(const DeviceOptions& options) { return std::make_unique<SerialLine>(options); }

} // namespace QR
