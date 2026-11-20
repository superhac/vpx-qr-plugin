// license:GPLv3+
#include "devices/Gm67Protocol.h"

namespace QR::Gm67 {

uint16_t Checksum(const uint8_t* data, size_t size)
{
   uint16_t sum = 0;
   for (size_t i = 0; i < size; i++)
      sum = static_cast<uint16_t>(sum + data[i]);
   return static_cast<uint16_t>(0x10000 - sum);
}

std::vector<uint8_t> BuildCommand(uint8_t opcode, const std::vector<uint8_t>& data)
{
   std::vector<uint8_t> cmd { static_cast<uint8_t>(4 + data.size()), opcode, kSourceHost, 0x00 };
   cmd.insert(cmd.end(), data.begin(), data.end());
   const uint16_t chk = Checksum(cmd.data(), cmd.size());
   cmd.push_back(static_cast<uint8_t>(chk >> 8));
   cmd.push_back(static_cast<uint8_t>(chk & 0xFF));
   return cmd;
}

bool FindScannerPacket(const uint8_t* data, size_t size, Packet& packet)
{
   for (size_t i = 0; i + 6 <= size; i++)
   {
      const size_t len = data[i];
      if (len < 4 || data[i + 2] != kSourceScanner || i + len + 2 > size)
         continue;
      const uint16_t chk = Checksum(data + i, len);
      if (data[i + len] != (chk >> 8) || data[i + len + 1] != (chk & 0xFF))
         continue;
      packet.offset = i;
      packet.size = len + 2;
      packet.opcode = data[i + 1];
      packet.data.assign(data + i + 4, data + i + len);
      return true;
   }
   return false;
}

} // namespace QR::Gm67
