// license:GPLv3+
#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace QR {

// Decides which scans are passed on.
//
// - Duplicate window: a scan identical to the previous one is dropped if it
//   arrives within `duplicateWindow` of the previous *sighting* of that code.
//   Sightings extend the window, so a code held in front of a continuous-mode
//   scanner (re-sent every ~1 s) counts once, until it's taken away for longer
//   than the window.
// - Cooldown: after a scan is accepted, every scan (same or different) is
//   dropped for `cooldown`.
//
// A duration of zero disables that rule.
class ScanFilter
{
public:
   using Clock = std::chrono::steady_clock;

   bool Accept(std::string_view scan, Clock::time_point now, std::chrono::milliseconds duplicateWindow, std::chrono::milliseconds cooldown);
   void Reset();

private:
   std::string m_lastSeen;
   Clock::time_point m_lastSeenTime {};
   bool m_hasSeen = false;
   Clock::time_point m_lastAcceptedTime {};
   bool m_hasAccepted = false;
};

} // namespace QR
