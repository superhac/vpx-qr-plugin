// license:GPLv3+
#pragma once

// Serial command protocol of the Waveshare Barcode Scanner Module (D),
// from Appendix D of its setup manual (manual/ folder):
//
//   request:  7E 00 <type> <len> <addr hi> <addr lo> <data[len]> <crc hi> <crc lo>
//   response: 02 00 00 <len> <data[len]> <crc hi> <crc lo>
//   crc:      CRC-16/XMODEM over everything after "7E 00" (request) or after
//             "02 00" (response), excluding the CRC itself.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace QR::Waveshare {

constexpr uint8_t kTypeRead = 0x07;
constexpr uint8_t kTypeWrite = 0x08;
constexpr uint8_t kTypeSave = 0x09;

constexpr uint16_t kAddrBaud = 0x002A; // 2 bytes, little-endian baud code

uint16_t Crc16Xmodem(const uint8_t* data, size_t size);

std::vector<uint8_t> BuildCommand(uint8_t type, uint16_t address, const std::vector<uint8_t>& data);

// "Query baud rate": 7E 00 07 01 00 2A 02 D8 0F
inline std::vector<uint8_t> QueryBaudCommand() { return BuildCommand(kTypeRead, kAddrBaud, { 0x02 }); }

struct Response
{
   enum class Status
   {
      Incomplete, // starts like a response, need more bytes
      NotResponse, // first byte isn't 0x02, or header/CRC is wrong
      Complete,
   };
   Status status = Status::Incomplete;
   size_t consumed = 0;       // bytes making up the response (when Complete)
   std::vector<uint8_t> data; // payload (when Complete)
};

// Parses a response at the start of [data, data + size).
Response ParseResponse(const uint8_t* data, size_t size);

// Baud rate from the device's baud code (e.g. 0x0139 -> 9600), or 0 if unknown.
int BaudFromCode(uint16_t code);

} // namespace QR::Waveshare
