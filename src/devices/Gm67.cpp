// license:GPLv3+
// Driver: GM67/M3027 scanner module (manual/GM67_Barcode.pdf).
//
// Must be in "USB COM" (virtual serial port) or TTL serial mode; the factory
// interface is USB keyboard. Serial defaults (manual 3.1): 9600 8N1. The
// manual lists no terminator as the default (7.2), but a unit tested on
// 2026-11-20 (software 1.9.30) sent CR after each scan. Auto
// framing handles both: CR/LF, or a short silence.
// Identify() sends the read-only "request revision" command (Appendix 6); the
// tested unit answers it in USB COM mode with no wake-up needed.
#include "devices/Gm67Protocol.h"
#include "devices/LineFramer.h"
#include "devices/ScannerDevice.h"

#include <vector>

namespace QR {

namespace {

class Gm67Device final : public ScannerDevice
{
public:
   explicit Gm67Device(const DeviceOptions& options)
      : m_options(options)
      , m_framer({ .terminator = options.terminator },
           [this](std::string_view frame) { EmitScan(frame); },
           [this](std::string_view reason) { LogWarning(reason); })
   {
   }

   SerialConfig PortConfig() const override
   {
      SerialConfig config; // 8N1
      config.baud = m_options.baud > 0 ? m_options.baud : 9600;
      return config;
   }

   IdentifyResult Identify(Transport& transport) override
   {
      const std::vector<uint8_t> query = Gm67::RequestRevisionCommand();
      if (!transport.Write(query.data(), query.size()))
         return IdentifyResult::NoAnswer;

      // Collect everything for a moment, then pass whatever isn't the reply
      // on as scan data.
      std::vector<uint8_t> rx;
      const auto deadline = Clock::now() + std::chrono::milliseconds(500);
      while (Clock::now() < deadline)
      {
         uint8_t buf[256];
         const int n = transport.Read(buf, sizeof(buf), 20);
         if (n < 0)
            break;
         if (n == 0)
            continue;
         rx.insert(rx.end(), buf, buf + n);

         Gm67::Packet packet;
         if (Gm67::FindScannerPacket(rx.data(), rx.size(), packet))
         {
            rx.erase(rx.begin() + static_cast<std::ptrdiff_t>(packet.offset), rx.begin() + static_cast<std::ptrdiff_t>(packet.offset + packet.size));
            if (!rx.empty())
               OnBytes(rx.data(), rx.size(), Clock::now());
            return IdentifyResult::Confirmed;
         }
      }
      if (!rx.empty())
         OnBytes(rx.data(), rx.size(), Clock::now());
      return IdentifyResult::NoAnswer;
   }

   void OnBytes(const uint8_t* data, size_t size, Clock::time_point now) override { m_framer.Feed(data, size, now); }
   void OnIdle(Clock::time_point now) override { m_framer.Idle(now); }

private:
   DeviceOptions m_options;
   LineFramer m_framer;
};

} // namespace

std::unique_ptr<ScannerDevice> CreateGm67(const DeviceOptions& options) { return std::make_unique<Gm67Device>(options); }

} // namespace QR
