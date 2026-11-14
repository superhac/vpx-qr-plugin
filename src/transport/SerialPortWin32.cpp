// license:GPLv3+
// Serial port for Windows (Win32 COMM API).
#if defined(_WIN32)

#include "transport/SerialPort.h"

#include <algorithm>
#include <cstring>

#include <windows.h>

namespace QR {

namespace {

std::string LastErrorMessage(DWORD err)
{
   switch (err)
   {
   case ERROR_FILE_NOT_FOUND:
   case ERROR_PATH_NOT_FOUND: return "port not found";
   case ERROR_ACCESS_DENIED: return "port is in use by another program";
   default: break;
   }
   char* text = nullptr;
   FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err, 0,
      reinterpret_cast<char*>(&text), 0, nullptr);
   std::string msg = text ? text : ("error " + std::to_string(err));
   if (text)
      LocalFree(text);
   while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == ' ' || msg.back() == '.'))
      msg.pop_back();
   return msg;
}

// "COM3" -> "\\.\COM3" (required for COM10 and above, harmless below).
std::string DevicePath(const std::string& port)
{
   if (port.rfind("\\\\.\\", 0) == 0)
      return port;
   return "\\\\.\\" + port;
}

} // namespace

SerialPort::~SerialPort() { Close(); }

bool SerialPort::IsOpen() const { return m_handle != nullptr; }

bool SerialPort::Open(const std::string& port, const SerialConfig& config, std::string& error)
{
   Close();
   if (port.empty())
   {
      error = "no port configured";
      return false;
   }

   // dwShareMode = 0: exclusive access.
   HANDLE h = CreateFileA(DevicePath(port).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
   if (h == INVALID_HANDLE_VALUE)
   {
      error = LastErrorMessage(GetLastError());
      return false;
   }

   DCB dcb {};
   dcb.DCBlength = sizeof(dcb);
   if (!GetCommState(h, &dcb))
   {
      error = "not a serial port: " + LastErrorMessage(GetLastError());
      CloseHandle(h);
      return false;
   }
   dcb.BaudRate = static_cast<DWORD>(config.baud);
   dcb.ByteSize = static_cast<BYTE>(config.dataBits);
   dcb.Parity = config.parity == Parity::Odd ? ODDPARITY : config.parity == Parity::Even ? EVENPARITY : NOPARITY;
   dcb.StopBits = config.stopBits == 2 ? TWOSTOPBITS : ONESTOPBIT;
   dcb.fBinary = TRUE;
   dcb.fParity = config.parity != Parity::None;
   dcb.fOutxCtsFlow = FALSE;
   dcb.fOutxDsrFlow = FALSE;
   dcb.fDtrControl = DTR_CONTROL_ENABLE;
   dcb.fRtsControl = RTS_CONTROL_ENABLE;
   dcb.fDsrSensitivity = FALSE;
   dcb.fOutX = FALSE;
   dcb.fInX = FALSE;
   dcb.fNull = FALSE;
   dcb.fAbortOnError = FALSE;
   if (!SetCommState(h, &dcb))
   {
      error = "failed to configure port (baud " + std::to_string(config.baud) + "): " + LastErrorMessage(GetLastError());
      CloseHandle(h);
      return false;
   }
   PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);

   m_handle = h;
   m_lastTimeoutMs = -1;
   return true;
}

void SerialPort::Close()
{
   if (m_handle)
   {
      CloseHandle(static_cast<HANDLE>(m_handle));
      m_handle = nullptr;
   }
}

int SerialPort::Read(uint8_t* buffer, size_t size, int timeoutMs)
{
   if (!m_handle)
      return -1;
   HANDLE h = static_cast<HANDLE>(m_handle);

   if (timeoutMs != m_lastTimeoutMs)
   {
      // Return as soon as any byte is available; otherwise wait up to
      // timeoutMs for the first one (documented COMMTIMEOUTS combination).
      COMMTIMEOUTS to {};
      to.ReadIntervalTimeout = MAXDWORD;
      to.ReadTotalTimeoutMultiplier = MAXDWORD;
      to.ReadTotalTimeoutConstant = static_cast<DWORD>(std::max(timeoutMs, 1));
      to.WriteTotalTimeoutConstant = 1000;
      if (!SetCommTimeouts(h, &to))
         return -1;
      m_lastTimeoutMs = timeoutMs;
   }

   DWORD read = 0;
   if (!ReadFile(h, buffer, static_cast<DWORD>(size), &read, nullptr))
   {
      // Unplugging a USB adapter fails reads with e.g. ERROR_ACCESS_DENIED,
      // ERROR_BAD_COMMAND or ERROR_OPERATION_ABORTED.
      DWORD errors = 0;
      if (GetLastError() == ERROR_OPERATION_ABORTED && ClearCommError(h, &errors, nullptr))
         return 0; // line error cleared, port still alive
      return -1;
   }
   return static_cast<int>(read);
}

bool SerialPort::Write(const uint8_t* data, size_t size)
{
   if (!m_handle)
      return false;
   DWORD written = 0;
   return WriteFile(static_cast<HANDLE>(m_handle), data, static_cast<DWORD>(size), &written, nullptr) && written == size;
}

std::vector<std::string> SerialPort::ListPorts()
{
   std::vector<std::string> ports;
   HKEY key;
   if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) != ERROR_SUCCESS)
      return ports;
   for (DWORD i = 0;; i++)
   {
      char name[256], value[256];
      DWORD nameLen = sizeof(name), valueLen = sizeof(value), type = 0;
      if (RegEnumValueA(key, i, name, &nameLen, nullptr, &type, reinterpret_cast<BYTE*>(value), &valueLen) != ERROR_SUCCESS)
         break;
      if (type == REG_SZ && valueLen > 0)
         ports.emplace_back(value, strnlen(value, valueLen));
   }
   RegCloseKey(key);
   // COM2 before COM10
   std::sort(ports.begin(), ports.end(), [](const std::string& a, const std::string& b) {
      return a.size() != b.size() ? a.size() < b.size() : a < b;
   });
   return ports;
}

} // namespace QR

#endif // _WIN32
