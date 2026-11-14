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

// Builds the VBScript the plugin runs in the table (VPX RunScript) for a scan.
// It calls the table's `Sub QR_OnScan(scan)`, if the table has one, with a
// Scripting.Dictionary (case-insensitive keys) of the scan's fields:
//
//  - JSON object: the object with "rom" set, flattened to dotted keys
//    ("player.name"; arrays as "list.0", "list.1", ..., "list.count"), plus
//    "type" (the first top-level key whose value is an object, "" if none)
//    and "json" (the published JSON).
//  - Anything else: "type" = "text", "text" = the scan, "rom".
//
// Strings are written as escaped VBScript literals (non-ASCII and control
// characters as ChrW), so scanned text can't break out of them.
std::string BuildTableScript(const std::string& scan, const std::string& rom);

// The `Array(key, value, ...)` expression passed to the dispatcher, alone (tests).
std::string BuildScanPairs(const std::string& scan, const std::string& rom);

// A VBScript expression for a UTF-8 string (tests).
std::string VbString(const std::string& utf8);

}
