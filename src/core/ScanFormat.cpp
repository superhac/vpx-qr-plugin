// license:GPLv3+
#include "ScanFormat.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace QR
{

namespace
{

using Json = nlohmann::ordered_json; // keeps the keys in the order they appear in the code

// The scan as a JSON object with "rom" set, or nothing if it isn't a JSON object.
std::optional<Json> ParseWithRom(const std::string& scan, const std::string& rom)
{
   Json json = Json::parse(scan, nullptr, /*allow_exceptions=*/false);
   if (!json.is_object())
      return std::nullopt;
   json["rom"] = rom;
   return json;
}

std::string Dump(const Json& json)
{
   // 'replace' only guards against throwing on invalid UTF-8 (the parser
   // already rejects it in scans; the ROM name comes from PinMAME).
   return json.dump(-1, ' ', false, Json::error_handler_t::replace);
}

// Next code point of a UTF-8 string, U+FFFD for an invalid sequence.
uint32_t NextCodePoint(const std::string& s, size_t& i)
{
   const auto byte = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
   const unsigned char b0 = byte(i);
   int extra;
   uint32_t cp;
   if (b0 < 0x80) { i++; return b0; }
   else if ((b0 & 0xE0) == 0xC0) { extra = 1; cp = b0 & 0x1F; }
   else if ((b0 & 0xF0) == 0xE0) { extra = 2; cp = b0 & 0x0F; }
   else if ((b0 & 0xF8) == 0xF0) { extra = 3; cp = b0 & 0x07; }
   else { i++; return 0xFFFD; }
   for (int k = 1; k <= extra; k++)
   {
      if (i + k >= s.size() || (byte(i + k) & 0xC0) != 0x80) { i++; return 0xFFFD; }
      cp = (cp << 6) | (byte(i + k) & 0x3F);
   }
   static constexpr uint32_t kMin[] = { 0, 0x80, 0x800, 0x10000 };
   if (cp < kMin[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { i++; return 0xFFFD; }
   i += extra + 1;
   return cp;
}

std::string VbNumber(const Json& value)
{
   if (value.is_number_integer() || value.is_number_unsigned())
   {
      // Long range, minus -2^31 (which VBScript reads as -(2^31), a Double).
      const bool fitsLong = value.is_number_unsigned()
         ? value.get<uint64_t>() <= static_cast<uint64_t>(std::numeric_limits<int32_t>::max())
         : (value.get<int64_t>() > std::numeric_limits<int32_t>::min() && value.get<int64_t>() <= std::numeric_limits<int32_t>::max());
      if (fitsLong)
         return value.dump();
      return Json(value.get<double>()).dump();
   }
   return value.dump(); // float: locale-independent, e.g. 1.5 or 1e+20
}

std::string VbValue(const Json& value)
{
   switch (value.type())
   {
   case Json::value_t::string: return VbString(value.get_ref<const std::string&>());
   case Json::value_t::boolean: return value.get<bool>() ? "True" : "False";
   case Json::value_t::null: return "Null";
   default: return VbNumber(value);
   }
}

using Pairs = std::vector<std::pair<std::string, std::string>>; // key, VBScript expression

void Flatten(const Json& value, const std::string& path, Pairs& out)
{
   if (value.is_object())
   {
      for (const auto& [key, item] : value.items())
         Flatten(item, path.empty() ? key : path + "." + key, out);
   }
   else if (value.is_array())
   {
      for (size_t i = 0; i < value.size(); i++)
         Flatten(value[i], path + "." + std::to_string(i), out);
      out.emplace_back(path + ".count", std::to_string(value.size()));
   }
   else
      out.emplace_back(path, VbValue(value));
}

} // namespace

std::string AddRomName(const std::string& scan, const std::string& rom)
{
   const auto json = ParseWithRom(scan, rom);
   return json ? Dump(*json) : scan + "[" + rom + "]";
}

std::string VbString(const std::string& utf8)
{
   std::string out, literal;
   bool hasLiteral = false;
   const auto append = [&](const std::string& part) { out += out.empty() ? part : " & " + part; };
   const auto flushLiteral = [&]() {
      if (hasLiteral)
         append("\"" + literal + "\"");
      literal.clear();
      hasLiteral = false;
   };
   const auto chrW = [&](uint32_t unit) { flushLiteral(); append("ChrW(" + std::to_string(unit) + ")"); };
   for (size_t i = 0; i < utf8.size();)
   {
      const uint32_t cp = NextCodePoint(utf8, i);
      if (cp >= 0x20 && cp < 0x7F)
      {
         literal += static_cast<char>(cp);
         if (cp == '"')
            literal += '"';
         hasLiteral = true;
      }
      else if (cp > 0xFFFF) // UTF-16 surrogate pair, as VBScript strings are UTF-16
      {
         chrW(0xD800 + ((cp - 0x10000) >> 10));
         chrW(0xDC00 + ((cp - 0x10000) & 0x3FF));
      }
      else
         chrW(cp);
   }
   flushLiteral();
   return out.empty() ? "\"\"" : out;
}

std::string BuildScanPairs(const std::string& scan, const std::string& rom)
{
   Pairs pairs;
   if (const auto json = ParseWithRom(scan, rom))
   {
      Flatten(*json, "", pairs);
      std::string type;
      for (const auto& [key, item] : json->items())
         if (item.is_object())
         {
            type = key;
            break;
         }
      // Added last so they win over same-named keys from the code.
      pairs.emplace_back("type", VbString(type));
      pairs.emplace_back("json", VbString(Dump(*json)));
   }
   else
   {
      pairs.emplace_back("type", "\"text\"");
      pairs.emplace_back("text", VbString(scan));
      pairs.emplace_back("rom", VbString(rom));
   }
   std::string out = "Array(";
   for (size_t i = 0; i < pairs.size(); i++)
      out += (i ? ", " : "") + VbString(pairs[i].first) + ", " + pairs[i].second;
   return out + ")";
}

std::string BuildTableScript(const std::string& scan, const std::string& rom)
{
   // The dispatcher is (re)defined with every scan: VBScript lets a later
   // script block redefine a Sub, and this needs no state across games.
   // Errors are trapped only while looking up QR_OnScan, so a table without
   // it is skipped silently, but errors inside the table's Sub still show.
   // CreateObject stays alone on its line: VPX rewrites CreateObject("...")
   // calls in RunScript code with a per-line regex.
   return
      "Sub QRPlugin_Dispatch(pairs)\n"
      "   Dim handler, scan, i\n"
      "   On Error Resume Next\n"
      "   Set handler = GetRef(\"QR_OnScan\")\n"
      "   If Err.Number <> 0 Then Exit Sub\n"
      "   On Error GoTo 0\n"
      "   Set scan = CreateObject(\"Scripting.Dictionary\")\n"
      "   scan.CompareMode = 1\n"
      "   For i = 0 To UBound(pairs) - 1 Step 2\n"
      "      scan(pairs(i)) = pairs(i + 1)\n"
      "   Next\n"
      "   handler scan\n"
      "End Sub\n"
      "QRPlugin_Dispatch " + BuildScanPairs(scan, rom) + "\n";
}

}
