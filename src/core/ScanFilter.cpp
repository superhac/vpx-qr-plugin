// license:GPLv3+
#include "core/ScanFilter.h"

namespace QR {

void ScanFilter::Reset()
{
   m_lastSeen.clear();
   m_hasSeen = false;
   m_hasAccepted = false;
}

bool ScanFilter::Accept(std::string_view scan, Clock::time_point now, std::chrono::milliseconds duplicateWindow, std::chrono::milliseconds cooldown)
{
   const bool isDuplicate = m_hasSeen && scan == m_lastSeen && duplicateWindow.count() > 0 && now - m_lastSeenTime < duplicateWindow;

   // Every sighting counts, accepted or not, so a held code keeps extending
   // its own duplicate window.
   m_lastSeen.assign(scan);
   m_lastSeenTime = now;
   m_hasSeen = true;

   if (isDuplicate)
      return false;
   if (m_hasAccepted && cooldown.count() > 0 && now - m_lastAcceptedTime < cooldown)
      return false;

   m_lastAcceptedTime = now;
   m_hasAccepted = true;
   return true;
}

} // namespace QR
