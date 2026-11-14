// license:GPLv3+
// Driver: Waveshare Barcode Scanner Module (D).
//
// Defaults (setup manual, Appendix C): 9600 8N1, continuous mode, output is
// [Data] followed by a CR tail. The tail can be changed to none/TAB/CRLF with
// setup barcodes, so the default framing is Auto (CR or LF or silence).
// Identify() sends the read-only "query baud rate" command (Appendix D).
#include "devices/LineFramer.h"
#include "devices/ScannerDevice.h"
#include "devices/WaveshareProtocol.h"

#include <algorithm>
#include <vector>

namespace QR {

namespace {

class WaveshareD final : public ScannerDevice
{
public:
   explicit WaveshareD(const DeviceOptions& options)
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
      const std::vector<uint8_t> query = Waveshare::QueryBaudCommand();
      if (!transport.Write(query.data(), query.size()))
         return IdentifyResult::NoAnswer;

      std::vector<uint8_t> rx;
      const auto deadline = Clock::now() + std::chrono::milliseconds(500);
      while (Clock::now() < deadline)
      {
         uint8_t buf[256];
         const int n = transport.Read(buf, sizeof(buf), 20);
         if (n < 0)
            return IdentifyResult::NoAnswer;
         if (n == 0)
            continue;
         rx.insert(rx.end(), buf, buf + n);

         // Anything before a 0x02 is scan data (scans are text, they never
         // contain 0x02), so pass it on to the framer.
         const auto start = std::find(rx.begin(), rx.end(), uint8_t { 0x02 });
         if (start != rx.begin())
         {
            OnBytes(rx.data(), static_cast<size_t>(start - rx.begin()), Clock::now());
            rx.erase(rx.begin(), start);
         }
         if (rx.empty())
            continue;

         const Waveshare::Response r = Waveshare::ParseResponse(rx.data(), rx.size());
         if (r.status == Waveshare::Response::Status::Incomplete)
            continue;
         if (r.status == Waveshare::Response::Status::NotResponse)
         {
            rx.erase(rx.begin()); // not a response after all: drop the 0x02 and keep looking
            continue;
         }
         if (r.consumed < rx.size())
            OnBytes(rx.data() + r.consumed, rx.size() - r.consumed, Clock::now());
         return IdentifyResult::Confirmed;
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

std::unique_ptr<ScannerDevice> CreateWaveshareD(const DeviceOptions& options) { return std::make_unique<WaveshareD>(options); }

} // namespace QR
