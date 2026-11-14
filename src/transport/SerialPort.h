// license:GPLv3+
#pragma once

#include "transport/Transport.h"

#include <string>
#include <vector>

namespace QR {

enum class Parity { None, Odd, Even };

struct SerialConfig
{
   int baud = 9600;
   int dataBits = 8;           // 5..8
   Parity parity = Parity::None;
   int stopBits = 1;           // 1 or 2
};

// A serial port, opened in raw mode with no flow control.
// Implementations: SerialPortPosix.cpp (Linux, macOS), SerialPortWin32.cpp.
class SerialPort final : public Transport
{
public:
   SerialPort() = default;
   ~SerialPort() override;
   SerialPort(const SerialPort&) = delete;
   SerialPort& operator=(const SerialPort&) = delete;

   // Port names:
   //   Linux:   /dev/serial/by-id/usb-... (stable), /dev/ttyUSB0, /dev/ttyACM0
   //   macOS:   /dev/cu.usbserial-...  (use cu.*, not tty.*)
   //   Windows: COM3 (the \\.\ prefix needed for COM10+ is added automatically)
   // On failure returns false and sets error to a human-readable reason.
   bool Open(const std::string& port, const SerialConfig& config, std::string& error);
   void Close();
   bool IsOpen() const;

   int Read(uint8_t* buffer, size_t size, int timeoutMs) override;
   bool Write(const uint8_t* data, size_t size) override;

   // Serial ports present on this machine, best effort (for qrprobe --list).
   static std::vector<std::string> ListPorts();

private:
#ifdef _WIN32
   void* m_handle = nullptr;
   int m_lastTimeoutMs = -1;
#else
   int m_fd = -1;
#endif
};

} // namespace QR
