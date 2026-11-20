// license:GPLv3+
// Unit tests for the parts of the QR plugin that don't need VPX or hardware.
// Build with -DQR_BUILD_TESTS=ON, run with ctest (or the qr_tests binary).

#include "core/ScanFilter.h"
#include "core/ScanFormat.h"
#include "devices/DeviceRegistry.h"
#include "devices/Gm67Protocol.h"
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
// GM67/M3027 protocol (vectors from Appendix 6 of the manual)
// ---------------------------------------------------------------------------
static void TestGm67Protocol()
{
   std::printf("GM67/M3027 protocol\n");
   CHECK(Gm67::RequestRevisionCommand() == Hex("04 A3 04 00 FF 55"));
   CHECK(Gm67::BuildCommand(0xE4) == Hex("04 E4 04 00 FF 14"));             // START_DECODE
   CHECK(Gm67::BuildCommand(0xE8, { 0x01 }) == Hex("05 E8 04 00 01 FF 0E")); // LED_OFF
   {
      const auto param = Hex("08 C6 04 08 00 F2 05 02"); // PARAM_SEND "terminator CR"
      CHECK(Gm67::Checksum(param.data(), param.size()) == 0xFE2D);
   }
   {
      // A reply from the scanner, with scan text before and after it.
      std::vector<uint8_t> rx = Bytes("AB");
      const auto reply = Hex("07 A4 00 00 31 2E 30");
      rx.insert(rx.end(), reply.begin(), reply.end());
      const uint16_t chk = Gm67::Checksum(reply.data(), reply.size());
      rx.push_back(static_cast<uint8_t>(chk >> 8));
      rx.push_back(static_cast<uint8_t>(chk & 0xFF));
      rx.push_back('C');
      Gm67::Packet p;
      CHECK(Gm67::FindScannerPacket(rx.data(), rx.size(), p));
      CHECK(p.offset == 2 && p.size == 9 && p.opcode == Gm67::kOpReplyRevision);
      CHECK(p.data == Bytes("1.0"));
   }
   {
      // The real revision reply captured from a GM67/M3027 on 2026-11-20 (its text contains CR LF).
      const auto rx = Hex("5E A4 00 00 4D 6F 64 65 6C 3A 4D 33 30 32 37 0D 0A 53 2F 4E 3A 32 30 32 36 30 39 30 38 30 32 32 32 0D 0A 44 61 74 65 3A 32 30 32 36 30 39 30 38 0D 0A 48 61 72 64 77 61 72 65 20 76 65 72 73 69 6F 6E 3A 0D 0A 53 6F 66 74 77 61 72 65 20 76 65 72 73 69 6F 6E 3A 31 2E 39 2E 33 30 0D 0A E6 27");
      Gm67::Packet p;
      CHECK(Gm67::FindScannerPacket(rx.data(), rx.size(), p));
      CHECK(p.offset == 0 && p.size == 96 && p.opcode == Gm67::kOpReplyRevision);
   }
   {
      const auto ack = Hex("04 D0 00 00 FF 2C");
      Gm67::Packet p;
      CHECK(Gm67::FindScannerPacket(ack.data(), ack.size(), p) && p.opcode == Gm67::kOpAck);
   }
   {
      Gm67::Packet p;
      const auto host = Hex("04 A3 04 00 FF 55"); // our own command echoed: not from the scanner
      CHECK(!Gm67::FindScannerPacket(host.data(), host.size(), p));
      const auto bad = Hex("04 D0 00 00 FF 2D"); // wrong checksum
      CHECK(!Gm67::FindScannerPacket(bad.data(), bad.size(), p));
      const auto partial = Hex("04 D0 00 00 FF");
      CHECK(!Gm67::FindScannerPacket(partial.data(), partial.size(), p));
      const auto text = Bytes("{\"v\":1,\"player\":{\"name\":\"Jane Smith\",\"initials\":\"JNS\"}}");
      CHECK(!Gm67::FindScannerPacket(text.data(), text.size(), p));
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
   CHECK(Devices().size() >= 3);
   CHECK(FindDevice("waveshare-d") == &Devices()[0]); // position 0 is saved in users' settings: never move it
   CHECK(FindDevice("serial-line") == &Devices()[1]);
   CHECK(FindDevice("gm67") == &Devices()[2]);
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
      auto dev = FindDevice("gm67")->create(defaults);
      // Real GM67/M3027 capture from 2026-11-20: revision reply, then a player code ending in CR.
      std::vector<std::string> scans;
      dev->SetScanHandler([&](std::string_view s) { scans.emplace_back(s); });
      FakeTransport t;
      t.incoming.push_back(Hex("5E A4 00 00 4D 6F 64 65 6C 3A 4D 33 30 32 37 0D 0A 53 2F 4E 3A 32 30 32 36 30 39 30 38 30 32 32 32 0D 0A 44 61 74 65 3A 32 30 32 36 30 39 30 38 0D 0A 48 61 72 64 77 61 72 65 20 76 65 72 73 69 6F 6E 3A 0D 0A 53 6F 66 74 77 61 72 65 20 76 65 72 73 69 6F 6E 3A 31 2E 39 2E 33 30 0D 0A E6 27"));
      CHECK(dev->Identify(t) == IdentifyResult::Confirmed);
      CHECK(scans.empty()); // the reply's text must not leak out as scans
      const auto wire = Bytes("{\"v\":1,\"player\":{\"name\":\"Superhac\",\"initials\":\"SUP\",\"passcode\":\"secret\"}}\r");
      dev->OnBytes(wire.data(), wire.size(), Clock::now());
      CHECK(scans.size() == 1 && scans[0] == "{\"v\":1,\"player\":{\"name\":\"Superhac\",\"initials\":\"SUP\",\"passcode\":\"secret\"}}");
   }
   {
      auto dev = FindDevice("waveshare-d")->create(defaults);
      FakeTransport t; // silent device
      CHECK(dev->Identify(t) == IdentifyResult::NoAnswer);
   }
   {
      // GM67/M3027 factory output: data with no terminator, ended by silence.
      auto dev = FindDevice("gm67")->create(defaults);
      CHECK(dev->PortConfig().baud == 9600);
      std::vector<std::string> scans;
      dev->SetScanHandler([&](std::string_view s) { scans.emplace_back(s); });
      const auto t0 = Clock::now();
      const auto wire = Bytes("{\"v\":1,\"player\":{\"name\":\"Jane Smith\",\"initials\":\"JNS\"}}");
      dev->OnBytes(wire.data(), wire.size(), t0);
      dev->OnIdle(t0 + 20ms);
      CHECK(scans.empty());
      dev->OnIdle(t0 + 100ms);
      CHECK(scans.size() == 1 && scans[0] == std::string(wire.begin(), wire.end()));
   }
   {
      // GM67/M3027 identify: the revision reply is recognised, scan text around it passes through.
      auto dev = FindDevice("gm67")->create(defaults);
      std::vector<std::string> scans;
      dev->SetScanHandler([&](std::string_view s) { scans.emplace_back(s); });
      FakeTransport t;
      t.incoming.push_back(Bytes("AB"));
      auto reply = Hex("07 A4 00 00 31 2E 30");
      const uint16_t chk = Gm67::Checksum(reply.data(), reply.size());
      reply.push_back(static_cast<uint8_t>(chk >> 8));
      t.incoming.push_back(reply);
      t.incoming.push_back({ static_cast<uint8_t>(chk & 0xFF), 'C', '\r' });
      CHECK(dev->Identify(t) == IdentifyResult::Confirmed);
      CHECK(t.written == Gm67::RequestRevisionCommand());
      CHECK(scans.size() == 1 && scans[0] == "ABC");
   }
   {
      auto dev = FindDevice("gm67")->create(defaults);
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

// ---------------------------------------------------------------------------
// ScanFormat: VBScript for the table's QR_OnScan
// ---------------------------------------------------------------------------
static void TestTableScript()
{
   // String literals: quotes doubled, non-ASCII / control characters as ChrW.
   CHECK(VbString("") == R"("")");
   CHECK(VbString("Jane Smith") == R"("Jane Smith")");
   CHECK(VbString(R"(say "hi")") == R"("say ""hi""")");
   CHECK(VbString("Ren\xc3\xa9") == R"("Ren" & ChrW(233))");
   CHECK(VbString("\xc3\xa9t\xc3\xa9") == R"(ChrW(233) & "t" & ChrW(233))");
   CHECK(VbString("a\r\nb") == R"("a" & ChrW(13) & ChrW(10) & "b")");
   CHECK(VbString("\xf0\x9f\x98\x80") == "ChrW(55357) & ChrW(56832)");   // U+1F600: surrogate pair
   CHECK(VbString("x\xffy") == R"("x" & ChrW(65533) & "y")");            // invalid UTF-8
   CHECK(VbString("\xe2\x82") == "ChrW(65533) & ChrW(65533)");            // truncated sequence
   // Scanned text can't end the literal and inject code.
   CHECK(VbString("\" : ExecuteGlobal \"x") == R"(""" : ExecuteGlobal ""x")");

   // Player code: flattened keys, then type and json.
   CHECK(BuildScanPairs(R"({"v":1,"player":{"name":"Jane Smith","initials":"JNS"}})", "afm_113b")
      == R"(Array("v", 1, "player.name", "Jane Smith", "player.initials", "JNS", "rom", "afm_113b", "type", "player", )"
         R"("json", "{""v"":1,""player"":{""name"":""Jane Smith"",""initials"":""JNS""},""rom"":""afm_113b""}"))");
   // Value types, arrays, nesting.
   CHECK(BuildScanPairs(R"({"a":{"b":{"c":true}},"n":null,"f":1.5,"big":3000000000,"neg":-7,"list":["x",2]})", "")
      == R"(Array("a.b.c", True, "n", Null, "f", 1.5, "big", 3000000000.0, "neg", -7, "list.0", "x", "list.1", 2, "list.count", 2, )"
         R"("rom", "", "type", "a", "json", "{""a"":{""b"":{""c"":true}},""n"":null,""f"":1.5,""big"":3000000000,""neg"":-7,""list"":[""x"",2],""rom"":""""}"))");
   // No object-valued key: empty type. A "type" in the code is overridden (added last).
   CHECK(BuildScanPairs(R"({"type":"x"})", "r") == R"(Array("type", "x", "rom", "r", "type", "", "json", "{""type"":""x"",""rom"":""r""}"))");
   // Not JSON: text.
   CHECK(BuildScanPairs("[PLAYER][FIELD1]", "afm_113b") == R"(Array("type", "text", "text", "[PLAYER][FIELD1]", "rom", "afm_113b"))");
   CHECK(BuildScanPairs("[1,2]", "") == R"(Array("type", "text", "text", "[1,2]", "rom", ""))");

   // Full script: dispatcher then one call line; no line breaks from the scan.
   const std::string script = BuildTableScript("a\rb\nc", "");
   CHECK(script.find("Sub QRPlugin_Dispatch(pairs)\n") == 0);
   CHECK(script.find("GetRef(\"QR_OnScan\")") != std::string::npos);
   CHECK(script.ends_with("\nQRPlugin_Dispatch Array(\"type\", \"text\", \"text\", \"a\" & ChrW(13) & \"b\" & ChrW(10) & \"c\", \"rom\", \"\")\n"));
}

int main()
{
   TestWaveshareProtocol();
   TestGm67Protocol();
   TestLineFramer();
   TestScanFilter();
   TestScanFormat();
   TestTableScript();
   TestDrivers();
   std::printf("%d checks, %d failed\n", g_checks, g_failures);
   return g_failures == 0 ? 0 : 1;
}
