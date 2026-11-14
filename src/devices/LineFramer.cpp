// license:GPLv3+
#include "devices/LineFramer.h"

namespace QR {

LineFramer::LineFramer(Config config, FrameHandler onFrame, DropHandler onDrop)
   : m_config(config)
   , m_onFrame(std::move(onFrame))
   , m_onDrop(std::move(onDrop))
{
   m_buffer.reserve(256);
}

void LineFramer::Reset()
{
   m_buffer.clear();
   m_overflow = false;
   m_pendingCR = false;
}

void LineFramer::EndFrame()
{
   if (m_overflow)
   {
      if (m_onDrop)
         m_onDrop("scan longer than " + std::to_string(m_config.maxLength) + " bytes dropped");
   }
   else if (!m_buffer.empty() && m_onFrame)
      m_onFrame(m_buffer);
   Reset();
}

void LineFramer::Append(uint8_t c)
{
   const bool isControl = c < 0x20 || c == 0x7F;
   if (isControl && c != '\t' && c != '\r' && c != '\n')
      return;
   if (m_overflow)
      return;
   if (m_buffer.size() >= m_config.maxLength)
   {
      m_overflow = true;
      m_buffer.clear();
      return;
   }
   m_buffer.push_back(static_cast<char>(c));
}

void LineFramer::Feed(const uint8_t* data, size_t size, Clock::time_point now)
{
   if (size == 0)
      return;

   // Bytes after a long gap start a new scan.
   if (!m_buffer.empty() || m_overflow || m_pendingCR)
      Idle(now);
   m_lastByte = now;

   for (size_t i = 0; i < size; i++)
   {
      const uint8_t c = data[i];
      switch (m_config.terminator)
      {
      case Terminator::Auto:
         if (c == '\r' || c == '\n')
            EndFrame();
         else
            Append(c);
         break;
      case Terminator::CR:
         if (c == '\r')
            EndFrame();
         else
            Append(c);
         break;
      case Terminator::LF:
         if (c == '\n')
            EndFrame();
         else
            Append(c);
         break;
      case Terminator::CRLF:
         if (m_pendingCR)
         {
            m_pendingCR = false;
            if (c == '\n')
            {
               EndFrame();
               break;
            }
            Append('\r'); // a lone CR is data
         }
         if (c == '\r')
            m_pendingCR = true;
         else
            Append(c);
         break;
      case Terminator::SilenceOnly:
         Append(c);
         break;
      }
   }
}

void LineFramer::Idle(Clock::time_point now)
{
   if (m_buffer.empty() && !m_overflow && !m_pendingCR)
      return;
   const auto quiet = now - m_lastByte;
   const bool silenceEnds = m_config.terminator == Terminator::Auto || m_config.terminator == Terminator::SilenceOnly;
   if (silenceEnds)
   {
      if (quiet >= m_config.silence)
         EndFrame();
   }
   else if (quiet >= m_config.staleTimeout)
   {
      if (m_overflow)
         EndFrame(); // reports the oversize drop
      else
      {
         if (m_onDrop)
            m_onDrop("incomplete scan discarded (no terminator received)");
         Reset();
      }
   }
}

} // namespace QR
