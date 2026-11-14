// license:GPLv3+
#include "ScanFormat.h"

#include <nlohmann/json.hpp>

namespace QR
{

std::string AddRomName(const std::string& scan, const std::string& rom)
{
   // ordered_json keeps the keys in the order they appear in the code.
   nlohmann::ordered_json json = nlohmann::ordered_json::parse(scan, nullptr, /*allow_exceptions=*/false);
   if (!json.is_object())
      return scan + "[" + rom + "]";
   json["rom"] = rom;
   // The ROM name comes from PinMAME and is plain ASCII; 'replace' only guards
   // against throwing if it ever isn't valid UTF-8.
   return json.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
}

}
