// license:GPLv3+
#pragma once

#include "devices/ScannerDevice.h"

#include <chrono>
#include <functional>
#include <string>
#include <string_view>

namespace QR {

// Splits a byte stream into scans, for devices that send text followed by a
// terminator and/or a pause.
//
// - The terminator itself is never part of a scan, and empty scans are
//   dropped (so CRLF in Auto mode gives one scan, not two).
// - In Auto and SilenceOnly modes, `silence` with no new bytes ends a scan.
// - In CR/LF/CRLF modes, an unterminated partial scan is discarded after
//   `staleTimeout` of silence, so it can't get glued to the next scan.
// - Control characters other than TAB, CR and LF are removed (unless they are
//   the terminator, which is removed anyway).
// - A scan longer than maxLength is dropped entirely.
class LineFramer
{
public:
   struct Config
   {
      Terminator terminator = Terminator::Auto;
      std::chrono::milliseconds silence { 50 };
      std::chrono::milliseconds staleTimeout { 1000 };
      size_t maxLength = 4096;
   };
   using FrameHandler = std::function<void(std::string_view frame)>;
   using DropHandler = std::function<void(std::string_view reason)>;

   LineFramer(Config config, FrameHandler onFrame, DropHandler onDrop = {});

   void Feed(const uint8_t* data, size_t size, Clock::time_point now);
   void Idle(Clock::time_point now);
   void Reset();

private:
   void EndFrame();
   void Append(uint8_t c);

   Config m_config;
   FrameHandler m_onFrame;
   DropHandler m_onDrop;
   std::string m_buffer;
   bool m_overflow = false;
   bool m_pendingCR = false; // CRLF mode: saw CR, waiting to see if LF follows
   Clock::time_point m_lastByte {};
};

} // namespace QR
