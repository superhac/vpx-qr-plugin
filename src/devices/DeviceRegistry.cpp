// license:GPLv3+
#include "devices/DeviceRegistry.h"

namespace QR {

// Each driver's factory, defined in its own .cpp.
std::unique_ptr<ScannerDevice> CreateWaveshareD(const DeviceOptions& options);
std::unique_ptr<ScannerDevice> CreateSerialLine(const DeviceOptions& options);

const std::vector<DeviceEntry>& Devices()
{
   // To add a driver: write devices/<Name>.cpp with a Create<Name>() factory,
   // declare it above, and APPEND it here. Never reorder or remove entries:
   // VPX saves the "Device" setting as a position in this list.
   static const std::vector<DeviceEntry> devices {
      { "waveshare-d", "Waveshare Barcode Scanner (D)", &CreateWaveshareD },
      { "serial-line", "Generic serial scanner (text lines)", &CreateSerialLine },
   };
   return devices;
}

const DeviceEntry* FindDevice(std::string_view id)
{
   for (const DeviceEntry& d : Devices())
      if (id == d.id)
         return &d;
   return nullptr;
}

const char** DeviceNames()
{
   static const std::vector<const char*> names = [] {
      std::vector<const char*> v;
      for (const DeviceEntry& d : Devices())
         v.push_back(d.name);
      return v;
   }();
   return const_cast<const char**>(names.data());
}

} // namespace QR
