// license:GPLv3+
// Unit tests for the parts of the QR plugin that don't need VPX or hardware.
// Build with -DQR_BUILD_TESTS=ON, run with ctest (or the qr_tests binary).

#include "core/ScanFilter.h"
#include "core/ScanFormat.h"
#include "devices/DeviceRegistry.h"
#include "devices/LineFramer.h"
#include "devices/WaveshareProtocol.h"
#include "transport/Transport.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <thread>
#include <vector>

using namespace QR;
using namespace std::chrono_literals;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                 \
   do                                                                               \
   {                                                                                \
      g_checks++;                                                                   \
      if (!(cond))                                                                  \
      {                                                                             \
         g_failures++;                                                              \
         std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      }                                                                             \
   } while (0)

static std::vector<uint8_t> Hex(const char* text)
{
   std::vector<uint8_t> out;
   unsigned int b;
   int n;
   while (std::sscanf(text, "%2x%n", &b, &n) == 1)
   {
      out.push_back(static_cast<uint8_t>(b));
      text += n;
      while (*text == ' ')
         text++;
   }
   return out;
}

static std::vector<uint8_t> Bytes(const std::string& s) { return { s.begin(), s.end() }; }

// ---------------------------------------------------------------------------
// Waveshare protocol (vectors from Appendix D of the setup manual)
// ---------------------------------------------------------------------------

static void TestWaveshareProtocol()
{
   std::printf("Waveshare protocol\n");
   CHECK(Waveshare::QueryBaudCommand() == Hex("7E 00 07 01 00 2A 02 D8 0F"));
   CHECK(Waveshare::BuildCommand(Waveshare::kTypeWrite, Waveshare::kAddrBaud, { 0x39, 0x01 }) == Hex("7E 00 08 02 00 2A 39 01 A7 EA"));
   CHECK(Waveshare::BuildCommand(Waveshare::kTypeSave, 0x0000, { 0x00 }) == Hex("7E 00 09 01 00 00 00 DE C8"));
   CHECK(Waveshare::BuildCommand(Waveshare::kTypeSave, 0x0000, { 0xFF }) == Hex("7E 00 09 01 00 00 FF C0 38"));

   {
      const auto rx = Hex("02 00 00 02 39 01 C1 4C");
      const auto r = Waveshare::ParseResponse(rx.data(), rx.size());
      CHECK(r.status == Waveshare::Response::Status::Complete);
      CHECK(r.consumed == 8);
      CHECK(r.data == Hex("39 01"));
      CHECK(Waveshare::BaudFromCode(static_cast<uint16_t>(r.data[0] | (r.data[1] << 8))) == 9600);
   }
   {
      const auto rx = Hex("02 00 00 01 00 33 31");
      const auto r = Waveshare::ParseResponse(rx.data(), rx.size());
      CHECK(r.status == Waveshare::Response::Status::Complete);
      CHECK(r.data == Hex("00"));
   }
   {
      const auto rx = Hex("02 00 00 02 39");
      CHECK(Waveshare::ParseResponse(rx.data(), rx.size()).status == Waveshare::Response::Status::Incomplete);
   }
   {
      const auto rx = Hex("02 00 00 02 39 01 C1 4D"); // bad CRC
      CHECK(Waveshare::ParseResponse(rx.data(), rx.size()).status == Waveshare::Response::Status::NotResponse);
   }
   {
      const auto rx = Bytes("hello");
      CHECK(Waveshare::ParseResponse(rx.data(), rx.size()).status == Waveshare::Response::Status::NotResponse);
   }
}

// ---------------------------------------------------------------------------
// LineFramer
// ---------------------------------------------------------------------------

struct FramerHarness
{
   std::vector<std::string> frames;
   std::vector<std::string> drops;
   LineFramer framer;
   Clock::time_point t = Clock::time_point {} + 1h;

   explicit FramerHarness(LineFramer::Config config)
      : framer(config, [this](std::string_view f) { frames.emplace_back(f); }, [this](std::string_view r) { drops.emplace_back(r); })
   {
   }
   void Feed(const std::string& s, std::chrono::milliseconds after = 0ms)
   {
      t += after;
      framer.Feed(reinterpret_cast<const uint8_t*>(s.data()), s.size(), t);
   }
   void Idle(std::chrono::milliseconds after)
   {
      t += after;
      framer.Idle(t);
   }
};

static void TestLineFramer()
{
   std::printf("LineFramer\n");
   using V = std::vector<std::string>;
   {
      FramerHarness h({ .terminator = Terminator::Auto });
      h.Feed("abc\r");
      h.Feed("def\r\n", 200ms);
      h.Feed("ghi\n", 200ms);
      CHECK((h.frames == V { "abc", "def", "ghi" }));
   }
   { // split across reads
      FramerHarness h({ .terminator = Terminator::Auto });
      h.Feed("[PLA");
      h.Feed("YER]\r", 5ms);
      CHECK((h.frames == V { "[PLAYER]" }));
   }
   { // silence ends a scan in Auto mode, but not too early
      FramerHarness h({ .terminator = Terminator::Auto, .silence = 50ms });
      h.Feed("abc");
      h.Idle(20ms);
      CHECK(h.frames.empty());
      h.Idle(40ms);
      CHECK((h.frames == V { "abc" }));
   }
   { // a gap between bytes starts a new scan
      FramerHarness h({ .terminator = Terminator::Auto, .silence = 50ms });
      h.Feed("abc");
      h.Feed("def\r", 100ms);
      CHECK((h.frames == V { "abc", "def" }));
   }
   { // CR mode keeps LF as data
      FramerHarness h({ .terminator = Terminator::CR });
      h.Feed("line1\nline2\r");
      CHECK((h.frames == V { "line1\nline2" }));
   }
   { // CRLF mode: lone CR is data
      FramerHarness h({ .terminator = Terminator::CRLF });
      h.Feed("a\rb\r\n");
      CHECK((h.frames == V { "a\rb" }));
   }
   { // LF mode
      FramerHarness h({ .terminator = Terminator::LF });
      h.Feed("x\ry\n");
      CHECK((h.frames == V { "x\ry" }));
   }
   { // silence only
      FramerHarness h({ .terminator = Terminator::SilenceOnly, .silence = 50ms });
      h.Feed("a\rb");
      h.Idle(60ms);
      CHECK((h.frames == V { "a\rb" }));
   }
   { // other control characters are removed
      FramerHarness h({ .terminator = Terminator::Auto });
      h.Feed(std::string("a\x01" "b\x7F" "c\td\r"));
      CHECK((h.frames == V { "abc\td" }));
   }
   { // oversize scans are dropped, the next one still works
      FramerHarness h({ .terminator = Terminator::Auto, .maxLength = 4 });
      h.Feed("abcdef\r");
      CHECK(h.frames.empty());
      CHECK(h.drops.size() == 1);
      h.Feed("ok\r", 200ms);
      CHECK((h.frames == V { "ok" }));
   }
   { // explicit terminator: stale partial scans are discarded, not glued on
      FramerHarness h({ .terminator = Terminator::CR, .staleTimeout = 1000ms });
      h.Feed("abc");
      h.Idle(500ms);
      CHECK(h.drops.empty());
      h.Idle(600ms);
      CHECK(h.drops.size() == 1);
      h.Feed("x\r", 10ms);
      CHECK((h.frames == V { "x" }));
   }
   { // empty lines produce nothing
      FramerHarness h({ .terminator = Terminator::Auto });
      h.Feed("\r\n\r\n");
      CHECK(h.frames.empty());
   }
}

// ---------------------------------------------------------------------------
// ScanFilter
// ---------------------------------------------------------------------------

static void TestScanFilter()
{
   std::printf("ScanFilter\n");
   const auto t0 = ScanFilter::Clock::time_point {} + 1h;
   {
      ScanFilter f;
      const auto win = 1500ms, cool = 0ms;
      CHECK(f.Accept("A", t0, win, cool));
      CHECK(!f.Accept("A", t0 + 1000ms, win, cool)); // re-sent while held
      CHECK(!f.Accept("A", t0 + 2000ms, win, cool)); // still held: window slides
      CHECK(f.Accept("A", t0 + 3600ms, win, cool));  // taken away > 1.5 s, scanned again
      CHECK(f.Accept("B", t0 + 3700ms, win, cool));  // different code
      CHECK(f.Accept("A", t0 + 3800ms, win, cool));  // A again after B
   }
   {
      ScanFilter f;
      const auto win = 1500ms, cool = 5000ms;
      CHECK(f.Accept("A", t0, win, cool));
      CHECK(!f.Accept("B", t0 + 1000ms, win, cool)); // cooldown blocks other codes too
      CHECK(f.Accept("B", t0 + 5001ms, win, cool));
   }
   {
      ScanFilter f; // both rules off
      CHECK(f.Accept("A", t0, 0ms, 0ms));
      CHECK(f.Accept("A", t0, 0ms, 0ms));
   }
}

// ---------------------------------------------------------------------------
// Drivers
// ---------------------------------------------------------------------------

class FakeTransport final : public Transport
{
public:
   std::deque<std::vector<uint8_t>> incoming;
   std::vector<uint8_t> written;

   int Read(uint8_t* buffer, size_t size, int timeoutMs) override
   {
      if (incoming.empty())
      {
         std::this_thread::sleep_for(std::chrono::milliseconds(std::min(timeoutMs, 5)));
         return 0;
      }
      auto chunk = std::move(incoming.front());
      incoming.pop_front();
      const size_t n = std::min(size, chunk.size());
      std::memcpy(buffer, chunk.data(), n);
      return static_cast<int>(n);
   }
   bool Write(const uint8_t* data, size_t size) override
   {
      written.insert(written.end(), data, data + size);
      return true;
   }
};

static void TestDrivers()
{
   std::printf("Drivers\n");
   CHECK(Devices().size() >= 2);
   CHECK(FindDevice("waveshare-d") == &Devices()[0]); // position 0 is saved in users' settings: never move it
   CHECK(FindDevice("serial-line") == &Devices()[1]);
   CHECK(std::strcmp(DeviceNames()[0], Devices()[0].name) == 0);
   CHECK(FindDevice("nope") == nullptr);

   const DeviceOptions defaults;
   {
      // The real scan captured from the Waveshare D on 2026-11-13.
      auto dev = FindDevice("waveshare-d")->create(defaults);
      CHECK(dev->PortConfig().baud == 9600);
      std::vector<std::string> scans;
      dev->SetScanHandler([&](std::string_view s) { scans.emplace_back(s); });
      const auto wire = Hex("5B 50 4C 41 59 45 52 5D 5B 46 49 45 4C 44 31 5D 5B 46 49 45 4C 44 32 5D 5B 46 49 45 4C 44 33 5D 0D");
      dev->OnBytes(wire.data(), wire.size(), Clock::now());
      CHECK(scans.size() == 1 && scans[0] == "[PLAYER][FIELD1][FIELD2][FIELD3]");
   }
   {
      // Identify: scan bytes around the response still come through.
      auto dev = FindDevice("waveshare-d")->create(defaults);
      std::vector<std::string> scans;
      dev->SetScanHandler([&](std::string_view s) { scans.emplace_back(s); });
      FakeTransport t;
      t.incoming.push_back(Bytes("AB"));
      t.incoming.push_back(Hex("02 00 00 02 39"));
      t.incoming.push_back(Hex("01 C1 4C"));
      t.incoming.push_back(Bytes("C\r"));
      CHECK(dev->Identify(t) == IdentifyResult::Confirmed);
      CHECK(t.written == Waveshare::QueryBaudCommand());
      // "C\r" may be read during or after Identify; feed what's left.
      uint8_t buf[64];
      while (!t.incoming.empty())
      {
         const int n = t.Read(buf, sizeof(buf), 10);
         dev->OnBytes(buf, static_cast<size_t>(n), Clock::now());
      }
      CHECK(scans.size() == 1 && scans[0] == "ABC");
   }
   {
      auto dev = FindDevice("waveshare-d")->create(defaults);
      FakeTransport t; // silent device
      CHECK(dev->Identify(t) == IdentifyResult::NoAnswer);
   }
   {
      DeviceOptions o;
      o.baud = 115200;
      o.terminator = Terminator::CRLF;
      auto dev = FindDevice("serial-line")->create(o);
      CHECK(dev->PortConfig().baud == 115200);
      FakeTransport t;
      CHECK(dev->Identify(t) == IdentifyResult::NotSupported);
      std::vector<std::string> scans;
      dev->SetScanHandler([&](std::string_view s) { scans.emplace_back(s); });
      const auto wire = Bytes("12345\r\n");
      dev->OnBytes(wire.data(), wire.size(), Clock::now());
      CHECK(scans.size() == 1 && scans[0] == "12345");
   }
}

// ---------------------------------------------------------------------------
// ScanFormat: ROM name added to the published text
// ---------------------------------------------------------------------------
static void TestScanFormat()
{
   // JSON object: "rom" added at the end, re-written compact, key order kept.
   CHECK(AddRomName(R"({"v":1,"player":{"name":"Jane Smith","initials":"JNS"}})", "afm_113b")
      == R"({"v":1,"player":{"name":"Jane Smith","initials":"JNS"},"rom":"afm_113b"})");
   // Pretty-printed and with spaces: same compact result.
   CHECK(AddRomName("  { \"v\" : 1,\n \"player\" : { \"name\" : \"Jane Smith\" } }  ", "afm_113b")
      == R"({"v":1,"player":{"name":"Jane Smith"},"rom":"afm_113b"})");
   // A "rom" in the code is overwritten, in place.
   CHECK(AddRomName(R"({"rom":"{QR Plugin Fills in}","v":1})", "afm_113b") == R"({"rom":"afm_113b","v":1})");
   // No ROM running: empty string.
   CHECK(AddRomName(R"({"v":1})", "") == R"({"v":1,"rom":""})");
   // Escapes and non-ASCII text survive.
   CHECK(AddRomName(R"({"player":{"name":"Zoë \"Z\" O'Neil"}})", "x") == "{\"player\":{\"name\":\"Zo\xc3\xab \\\"Z\\\" O'Neil\"},\"rom\":\"x\"}");
   CHECK(AddRomName("{\"player\":{\"name\":\"Ren\xc3\xa9\"}}", "x") == "{\"player\":{\"name\":\"Ren\xc3\xa9\"},\"rom\":\"x\"}");
   CHECK(AddRomName(R"({})", "x") == R"({"rom":"x"})");

   // Not a JSON object: ROM name appended in brackets.
   CHECK(AddRomName("[PLAYER][FIELD1]", "afm_113b") == "[PLAYER][FIELD1][afm_113b]");
   CHECK(AddRomName("[PLAYER]", "") == "[PLAYER][]");
   CHECK(AddRomName("hello", "x") == "hello[x]");
   CHECK(AddRomName("", "x") == "[x]");
   CHECK(AddRomName("[1,2]", "x") == "[1,2][x]");          // JSON array
   CHECK(AddRomName("42", "x") == "42[x]");                // JSON number
   CHECK(AddRomName(R"("text")", "x") == R"("text"[x])");  // JSON string
   CHECK(AddRomName(R"({"v":1)", "x") == R"({"v":1[x])");  // truncated
   CHECK(AddRomName(R"({"v":1}{"v":2})", "x") == R"({"v":1}{"v":2}[x])"); // trailing data
   CHECK(AddRomName("{\"n\":\"\xff\"}", "x") == "{\"n\":\"\xff\"}[x]"); // invalid UTF-8
}

int main()
{
   TestWaveshareProtocol();
   TestLineFramer();
   TestScanFilter();
   TestScanFormat();
   TestDrivers();
   std::printf("%d checks, %d failed\n", g_checks, g_failures);
   return g_failures == 0 ? 0 : 1;
}
