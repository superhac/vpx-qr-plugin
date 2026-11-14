// license:GPLv3+
#pragma once

#include <string>

namespace QR
{

// Builds the text published for a scan, adding the running PinMAME ROM name
// (empty if none):
//
//  - The scan is a JSON object: "rom" is set to the ROM name (added, or
//    overwritten if the code has one) and the object is re-written as compact
//    JSON, keeping the code's key order. E.g.
//       {"v":1,"player":{"name":"Jane Smith","initials":"JNS"}}
//    -> {"v":1,"player":{"name":"Jane Smith","initials":"JNS"},"rom":"afm_113b"}
//
//  - Anything else (plain text, invalid JSON, a JSON array/number/string):
//    the ROM name is appended in brackets, e.g. "[PLAYER][FIELD1][afm_113b]".
std::string AddRomName(const std::string& scan, const std::string& rom);

}
