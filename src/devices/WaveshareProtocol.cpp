// license:GPLv3+
#include "devices/WaveshareProtocol.h"

namespace QR::Waveshare {

uint16_t Crc16Xmodem(const uint8_t* data, size_t size)
{
   uint16_t crc = 0;
   for (size_t i = 0; i < size; i++)
   {
      crc ^= static_cast<uint16_t>(data[i]) << 8;
      for (int bit = 0; bit < 8; bit++)
         crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
   }
   return crc;
}

std::vector<uint8_t> BuildCommand(uint8_t type, uint16_t address, const std::vector<uint8_t>& data)
{
   std::vector<uint8_t> cmd { 0x7E, 0x00, type, static_cast<uint8_t>(data.size()), static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address & 0xFF) };
   cmd.insert(cmd.end(), data.begin(), data.end());
   const uint16_t crc = Crc16Xmodem(cmd.data() + 2, cmd.size() - 2);
   cmd.push_back(static_cast<uint8_t>(crc >> 8));
   cmd.push_back(static_cast<uint8_t>(crc & 0xFF));
   return cmd;
}

Response ParseResponse(const uint8_t* data, size_t size)
{
   Response r;
   static constexpr uint8_t header[] = { 0x02, 0x00, 0x00 };
   for (size_t i = 0; i < 3; i++)
   {
      if (i >= size)
         return r; // Incomplete
      if (data[i] != header[i])
      {
         r.status = Response::Status::NotResponse;
         return r;
      }
   }
   if (size < 4)
      return r;
   const size_t len = data[3];
   const size_t total = 4 + len + 2;
   if (size < total)
      return r;
   const uint16_t crc = Crc16Xmodem(data + 2, 2 + len);
   if (data[4 + len] != (crc >> 8) || data[5 + len] != (crc & 0xFF))
   {
      r.status = Response::Status::NotResponse;
      return r;
   }
   r.status = Response::Status::Complete;
   r.consumed = total;
   r.data.assign(data + 4, data + 4 + len);
   return r;
}

int BaudFromCode(uint16_t code)
{
   // Appendix G of the manual
   switch (code)
   {
   case 0x09C4: return 1200;
   case 0x0271: return 4800;
   case 0x0139: return 9600;
   case 0x00D0: return 14400;
   case 0x009C: return 19200;
   case 0x004E: return 38400;
   case 0x0034: return 57600;
   case 0x001A: return 115200;
   default: return 0;
   }
}

} // namespace QR::Waveshare
