// license:GPLv3+
// qrprobe: test a scanner without VPX, using the plugin's own code
// (serial port, device driver, worker thread, duplicate filter).
//
//   qrprobe --list
//   qrprobe <port> [options]
//
// Build with -DQR_BUILD_TOOLS=ON.

#include "core/ScanFilter.h"
#include "core/ScannerWorker.h"
#include "devices/DeviceRegistry.h"
#include "devices/WaveshareProtocol.h"
#include "transport/SerialPort.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

using namespace QR;
using namespace std::chrono_literals;

namespace {

std::atomic<bool> g_quit { false };
std::mutex g_print;
const auto g_start = std::chrono::steady_clock::now();

void OnSignal(int) { g_quit = true; }

double Elapsed() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count(); }

std::string Printable(std::string_view s)
{
   std::string out;
   for (const char c : s)
   {
      const auto u = static_cast<unsigned char>(c);
      if (c == '\r') out += "\\r";
      else if (c == '\n') out += "\\n";
      else if (c == '\t') out += "\\t";
      else if (u < 0x20 || u == 0x7F) { char b[8]; std::snprintf(b, sizeof(b), "\\x%02X", u); out += b; }
      else out.push_back(c);
   }
   return out;
}

std::string HexDump(const uint8_t* data, size_t size)
{
   std::string out;
   char b[4];
   for (size_t i = 0; i < size; i++)
   {
      std::snprintf(b, sizeof(b), i ? " %02X" : "%02X", data[i]);
      out += b;
   }
   return out;
}

void Usage()
{
   std::printf(
      "Usage:\n"
      "  qrprobe --list                 list serial ports\n"
      "  qrprobe <port> [options]       read scans from <port>\n"
      "\n"
      "Options:\n"
      "  --device <id>       driver (default waveshare-d):");
   for (const DeviceEntry& d : Devices())
      std::printf(" %s", d.id);
   std::printf(
      "\n"
      "  --baud <n>          serial speed (default: the device's default)\n"
      "  --terminator <t>    auto | cr | lf | crlf | pause (default auto)\n"
      "  --duplicate <ms>    duplicate window (default 1500, 0 = off)\n"
      "  --cooldown <ms>     scan cooldown (default 0)\n"
      "  --raw               print raw bytes (hex) instead of decoded scans\n"
      "  --seconds <n>       stop after n seconds (default: run until Ctrl+C)\n"
      "\n"
      "Port examples: COM3 (Windows), /dev/serial/by-id/... or /dev/ttyUSB0 (Linux),\n"
      "               /dev/cu.usbserial-... (macOS)\n");
}

// Raw mode: send the device's identify query, then hex-dump bursts of bytes.
int RunRaw(const std::string& port, const DeviceEntry& device, const DeviceOptions& options, double seconds)
{
   auto dev = device.create(options);
   SerialPort sp;
   std::string error;
   const SerialConfig config = dev->PortConfig();
   if (!sp.Open(port, config, error))
   {
      std::fprintf(stderr, "Cannot open %s: %s\n", port.c_str(), error.c_str());
      return 1;
   }
   std::printf("Opened %s at %d baud (raw mode, %s)\n", port.c_str(), config.baud, device.id);

   if (std::strcmp(device.id, "waveshare-d") == 0)
   {
      const auto query = Waveshare::QueryBaudCommand();
      std::printf("TX: %s (query baud rate)\n", HexDump(query.data(), query.size()).c_str());
      sp.Write(query.data(), query.size());
   }

   std::string burst;
   auto lastByte = std::chrono::steady_clock::now();
   double burstStart = 0;
   while (!g_quit && (seconds <= 0 || Elapsed() < seconds))
   {
      uint8_t buf[512];
      const int n = sp.Read(buf, sizeof(buf), 10);
      const auto now = std::chrono::steady_clock::now();
      if (n < 0)
      {
         std::printf("Device disconnected\n");
         return 1;
      }
      if (n > 0)
      {
         if (burst.empty())
            burstStart = Elapsed();
         burst.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
         lastByte = now;
      }
      else if (!burst.empty() && now - lastByte > 50ms)
      {
         std::printf("[%8.3fs] RX %zu bytes: %s\n           text: \"%s\"\n", burstStart, burst.size(),
            HexDump(reinterpret_cast<const uint8_t*>(burst.data()), burst.size()).c_str(), Printable(burst).c_str());
         std::fflush(stdout);
         burst.clear();
      }
   }
   return 0;
}

int RunDecoded(const std::string& port, const DeviceEntry& device, const DeviceOptions& options, int duplicateMs, int cooldownMs, double seconds)
{
   ScanFilter filter;
   int accepted = 0;

   WorkerConfig config;
   config.port = port;
   config.device = &device;
   config.options = options;

   WorkerCallbacks callbacks;
   callbacks.onScan = [&](std::string scan) {
      const bool ok = filter.Accept(scan, Clock::now(), std::chrono::milliseconds(duplicateMs), std::chrono::milliseconds(cooldownMs));
      std::lock_guard lock(g_print);
      if (ok)
         std::printf("[%8.3fs] SCAN #%d (%zu bytes): %s\n", Elapsed(), ++accepted, scan.size(), Printable(scan).c_str());
      else
         std::printf("[%8.3fs] filtered (duplicate/cooldown): %s\n", Elapsed(), Printable(scan).c_str());
      std::fflush(stdout);
   };
   callbacks.onStatus = [](bool connected, std::string message) {
      std::lock_guard lock(g_print);
      std::printf("[%8.3fs] %s: %s\n", Elapsed(), connected ? "CONNECTED" : "NOT CONNECTED", message.c_str());
      std::fflush(stdout);
   };
   callbacks.onLog = [](LogLevel level, std::string message) {
      std::lock_guard lock(g_print);
      std::printf("[%8.3fs] %s %s\n", Elapsed(), level == LogLevel::Info ? "info:" : level == LogLevel::Warning ? "warning:" : "error:", message.c_str());
      std::fflush(stdout);
   };

   {
      std::lock_guard lock(g_print);
      std::printf("Reading %s with %s (Ctrl+C to stop)\n", port.c_str(), device.name);
   }
   ScannerWorker worker(config, callbacks);
   worker.Start();
   while (!g_quit && (seconds <= 0 || Elapsed() < seconds))
      std::this_thread::sleep_for(50ms);
   worker.Stop();
   std::printf("%d scan(s) accepted\n", accepted);
   return 0;
}

} // namespace

int main(int argc, char** argv)
{
   if (argc < 2 || !std::strcmp(argv[1], "-h") || !std::strcmp(argv[1], "--help"))
   {
      Usage();
      return argc < 2 ? 1 : 0;
   }
   if (!std::strcmp(argv[1], "--list"))
   {
      const auto ports = SerialPort::ListPorts();
      if (ports.empty())
         std::printf("No serial ports found\n");
      for (const auto& p : ports)
         std::printf("%s\n", p.c_str());
      return 0;
   }

   const std::string port = argv[1];
   const DeviceEntry* device = FindDevice("waveshare-d");
   DeviceOptions options;
   int duplicateMs = 1500, cooldownMs = 0;
   double seconds = 0;
   bool raw = false;

   for (int i = 2; i < argc; i++)
   {
      const std::string a = argv[i];
      auto next = [&]() -> const char* {
         if (i + 1 >= argc)
         {
            std::fprintf(stderr, "%s needs a value\n", a.c_str());
            std::exit(1);
         }
         return argv[++i];
      };
      if (a == "--device")
      {
         const char* id = next();
         device = FindDevice(id);
         if (!device)
         {
            std::fprintf(stderr, "Unknown device '%s'\n", id);
            return 1;
         }
      }
      else if (a == "--baud") options.baud = std::atoi(next());
      else if (a == "--duplicate") duplicateMs = std::atoi(next());
      else if (a == "--cooldown") cooldownMs = std::atoi(next());
      else if (a == "--seconds") seconds = std::atof(next());
      else if (a == "--raw") raw = true;
      else if (a == "--terminator")
      {
         const std::string t = next();
         if (t == "auto") options.terminator = Terminator::Auto;
         else if (t == "cr") options.terminator = Terminator::CR;
         else if (t == "lf") options.terminator = Terminator::LF;
         else if (t == "crlf") options.terminator = Terminator::CRLF;
         else if (t == "pause") options.terminator = Terminator::SilenceOnly;
         else
         {
            std::fprintf(stderr, "Unknown terminator '%s'\n", t.c_str());
            return 1;
         }
      }
      else
      {
         std::fprintf(stderr, "Unknown option '%s'\n", a.c_str());
         Usage();
         return 1;
      }
   }

   std::signal(SIGINT, OnSignal);
   std::signal(SIGTERM, OnSignal);
   return raw ? RunRaw(port, *device, options, seconds) : RunDecoded(port, *device, options, duplicateMs, cooldownMs, seconds);
}
