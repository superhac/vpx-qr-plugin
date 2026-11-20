// license:GPLv3+
#pragma once

// Serial command protocol of the GM67/M3027 scanner module, from Appendix 6 of its
// manual (manual/GM67_Barcode.pdf). Commands and replies are packets:
//
//   <len> <opcode> <source> <status> <data...> <chk hi> <chk lo>
//
//   len:    bytes in the packet, excluding the 2 checksum bytes
//   source: 0x04 = host, 0x00 = scanner
//   chk:    two's complement of the 16-bit sum of all the bytes before it
//
// Scans themselves are sent as plain text, not packets (factory setting).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace QR::Gm67 {

constexpr uint8_t kSourceHost = 0x04;
constexpr uint8_t kSourceScanner = 0x00;

constexpr uint8_t kOpRequestRevision = 0xA3;
constexpr uint8_t kOpReplyRevision = 0xA4;
constexpr uint8_t kOpAck = 0xD0;
constexpr uint8_t kOpNak = 0xD1;

uint16_t Checksum(const uint8_t* data, size_t size);

// A host packet: <len> <opcode> 04 00 <data...> <chk>.
std::vector<uint8_t> BuildCommand(uint8_t opcode, const std::vector<uint8_t>& data = {});

// "Request revision" (read-only): 04 A3 04 00 FF 55
inline std::vector<uint8_t> RequestRevisionCommand() { return BuildCommand(kOpRequestRevision); }

struct Packet
{
   size_t offset = 0;         // where the packet starts in the searched bytes
   size_t size = 0;           // bytes making up the packet, checksum included
   uint8_t opcode = 0;
   std::vector<uint8_t> data; // bytes after the status byte, before the checksum
};

// Finds the first complete, valid packet sent by the scanner (source 0x00)
// anywhere in [data, data + size). Scan text around it is left alone.
bool FindScannerPacket(const uint8_t* data, size_t size, Packet& packet);

} // namespace QR::Gm67
