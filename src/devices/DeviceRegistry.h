// license:GPLv3+
#pragma once

#include "devices/ScannerDevice.h"

#include <memory>
#include <string_view>
#include <vector>

namespace QR {

struct DeviceEntry
{
   const char* id;   // stable id, e.g. "waveshare-d" (used in events and qrprobe)
   const char* name; // shown in VPX's plugin settings
   std::unique_ptr<ScannerDevice> (*create)(const DeviceOptions& options);
};

// All drivers. The "Device" setting stores the position in this list, so new
// drivers must be APPENDED (see DeviceRegistry.cpp).
const std::vector<DeviceEntry>& Devices();

const DeviceEntry* FindDevice(std::string_view id);

// Driver names in registry order, for the settings enum.
const char** DeviceNames();

} // namespace QR
