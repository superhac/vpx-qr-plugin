// license:GPLv3+
#pragma once

#include <cstddef>
#include <cstdint>

namespace QR {

// A byte stream to a scanner. Today that's a serial port; a HID transport
// could implement the same interface later.
class Transport
{
public:
   virtual ~Transport() = default;

   // Waits up to timeoutMs for data. Returns the number of bytes read (0 on
   // timeout), or -1 if the device is gone (unplugged, I/O error).
   virtual int Read(uint8_t* buffer, size_t size, int timeoutMs) = 0;

   // Writes all bytes. Returns false on error.
   virtual bool Write(const uint8_t* data, size_t size) = 0;
};

} // namespace QR
