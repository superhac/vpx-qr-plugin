// license:GPLv3+
// Serial port for Linux and macOS (POSIX termios).
#if !defined(_WIN32)

#include "transport/SerialPort.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <system_error>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace QR {

namespace {

bool BaudToSpeed(int baud, speed_t& speed)
{
#if defined(__APPLE__)
   // On macOS speed_t is the numeric rate, and the B constants equal it.
   if (baud <= 0)
      return false;
   speed = static_cast<speed_t>(baud);
   return true;
#else
   switch (baud)
   {
   case 1200: speed = B1200; return true;
   case 2400: speed = B2400; return true;
   case 4800: speed = B4800; return true;
   case 9600: speed = B9600; return true;
   case 19200: speed = B19200; return true;
   case 38400: speed = B38400; return true;
   case 57600: speed = B57600; return true;
   case 115200: speed = B115200; return true;
#ifdef B230400
   case 230400: speed = B230400; return true;
#endif
#ifdef B460800
   case 460800: speed = B460800; return true;
#endif
#ifdef B921600
   case 921600: speed = B921600; return true;
#endif
   default: return false; // e.g. 14400 has no termios constant on Linux
   }
#endif
}

std::string OpenErrorMessage(int err)
{
   std::string msg = std::strerror(err);
   switch (err)
   {
   case ENOENT: return "port not found";
   case EBUSY: return "port is in use by another program";
   case EACCES:
#if defined(__linux__)
      return msg + " (add your user to the 'dialout' group, then log out and back in)";
#else
      return msg;
#endif
   default: return msg;
   }
}

} // namespace

SerialPort::~SerialPort() { Close(); }

bool SerialPort::IsOpen() const { return m_fd >= 0; }

bool SerialPort::Open(const std::string& port, const SerialConfig& config, std::string& error)
{
   Close();
   if (port.empty())
   {
      error = "no port configured";
      return false;
   }

   speed_t speed;
   if (!BaudToSpeed(config.baud, speed))
   {
      error = "baud rate " + std::to_string(config.baud) + " is not supported on this platform";
      return false;
   }

   // O_NONBLOCK: don't wait for carrier detect on open (macOS tty.* devices
   // would block forever). We keep the fd non-blocking and use poll().
   const int fd = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
   if (fd < 0)
   {
      error = OpenErrorMessage(errno);
      return false;
   }

   // Exclusive access, so two programs don't steal each other's bytes.
   if (::ioctl(fd, TIOCEXCL) != 0 && errno == EBUSY)
   {
      ::close(fd);
      error = OpenErrorMessage(EBUSY);
      return false;
   }

   termios tio {};
   if (::tcgetattr(fd, &tio) != 0)
   {
      error = std::string("not a serial port: ") + std::strerror(errno);
      ::close(fd);
      return false;
   }

   ::cfmakeraw(&tio);
   tio.c_cflag |= CLOCAL | CREAD;
   tio.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
#ifdef CRTSCTS
   tio.c_cflag &= ~CRTSCTS;
#endif
   switch (config.dataBits)
   {
   case 5: tio.c_cflag |= CS5; break;
   case 6: tio.c_cflag |= CS6; break;
   case 7: tio.c_cflag |= CS7; break;
   default: tio.c_cflag |= CS8; break;
   }
   if (config.parity != Parity::None)
      tio.c_cflag |= PARENB | (config.parity == Parity::Odd ? PARODD : 0);
   if (config.stopBits == 2)
      tio.c_cflag |= CSTOPB;
   tio.c_iflag &= ~(IXON | IXOFF | IXANY);
   tio.c_cc[VMIN] = 0;
   tio.c_cc[VTIME] = 0;

   if (::cfsetispeed(&tio, speed) != 0 || ::cfsetospeed(&tio, speed) != 0 || ::tcsetattr(fd, TCSANOW, &tio) != 0)
   {
      error = std::string("failed to configure port: ") + std::strerror(errno);
      ::close(fd);
      return false;
   }
   ::tcflush(fd, TCIOFLUSH); // drop anything buffered before we opened it

   m_fd = fd;
   return true;
}

void SerialPort::Close()
{
   if (m_fd >= 0)
   {
      ::ioctl(m_fd, TIOCNXCL);
      ::close(m_fd);
      m_fd = -1;
   }
}

int SerialPort::Read(uint8_t* buffer, size_t size, int timeoutMs)
{
   if (m_fd < 0)
      return -1;

   pollfd pfd { m_fd, POLLIN, 0 };
   const int ready = ::poll(&pfd, 1, timeoutMs);
   if (ready < 0)
      return errno == EINTR ? 0 : -1;
   if (ready == 0)
      return 0;

   if (pfd.revents & POLLIN)
   {
      const ssize_t n = ::read(m_fd, buffer, size);
      if (n > 0)
         return static_cast<int>(n);
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
         return 0;
      return -1; // n == 0 after POLLIN: hang-up (device unplugged)
   }
   if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
      return -1;
   return 0;
}

bool SerialPort::Write(const uint8_t* data, size_t size)
{
   if (m_fd < 0)
      return false;
   size_t done = 0;
   while (done < size)
   {
      const ssize_t n = ::write(m_fd, data + done, size - done);
      if (n > 0)
      {
         done += static_cast<size_t>(n);
         continue;
      }
      if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
         return false;
      pollfd pfd { m_fd, POLLOUT, 0 };
      if (::poll(&pfd, 1, 1000) <= 0)
         return false;
   }
   return true;
}

std::vector<std::string> SerialPort::ListPorts()
{
   namespace fs = std::filesystem;
   std::vector<std::string> ports;
   std::error_code ec;

   auto addMatching = [&](const fs::path& dir, auto&& match) {
      for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
      {
         const std::string name = it->path().filename().string();
         if (match(name))
            ports.push_back(it->path().string());
      }
      ec.clear();
   };
   auto startsWith = [](const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; };

#if defined(__APPLE__)
   addMatching("/dev", [&](const std::string& n) { return startsWith(n, "cu.") && n != "cu.Bluetooth-Incoming-Port" && n != "cu.debug-console"; });
#else
   // Stable names first, then the kernel names they point to.
   addMatching("/dev/serial/by-id", [](const std::string&) { return true; });
   std::vector<std::string> kernel;
   std::swap(kernel, ports);
   addMatching("/dev", [&](const std::string& n) { return startsWith(n, "ttyUSB") || startsWith(n, "ttyACM"); });
   std::sort(kernel.begin(), kernel.end());
   std::sort(ports.begin(), ports.end());
   kernel.insert(kernel.end(), ports.begin(), ports.end());
   return kernel;
#endif
   std::sort(ports.begin(), ports.end());
   return ports;
}

} // namespace QR

#endif // !_WIN32
