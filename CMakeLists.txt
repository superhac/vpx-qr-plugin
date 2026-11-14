cmake_minimum_required(VERSION 3.20)

# Match VPX's own minimum macOS version. Must be set before project().
# Override with -DCMAKE_OSX_DEPLOYMENT_TARGET=<ver> if you need to.
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

project(vpx-qr-plugin CXX)

option(QR_BUILD_TOOLS    "Build tools/qrprobe (stand-alone scanner test CLI)" OFF)
option(QR_BUILD_TESTS    "Build the unit tests (run with ctest)" OFF)
option(QR_BUILD_EXAMPLES "Build examples/consumer (a test plugin that receives scans)" OFF)

# ---------------------------------------------------------------------------
# plugin.cfg is the single source of truth for a plugin's identity:
#   [configuration] id    -> plugin id (export prefix, install folder)
#   [libraries]     <key> -> output library filename for this platform/arch
# ---------------------------------------------------------------------------

# Pick the [libraries] key VPX will look up on this platform/arch.
if(WIN32)
   if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(PLUGIN_LIB_KEY "windows.x64")
   else()
      set(PLUGIN_LIB_KEY "windows.x86")
   endif()
elseif(APPLE)
   set(PLUGIN_LIB_KEY "macos.arm64") # universal binary: macos.x64 must name the same file
else()
   if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
      set(PLUGIN_LIB_KEY "linux.aarch64")
   else()
      set(PLUGIN_LIB_KEY "linux.x64")
   endif()
endif()

# Return the quoted value of `key = "value"` from a plugin.cfg (empty if absent).
function(plugin_cfg_get cfg key out)
   file(STRINGS "${cfg}" _lines)
   set(_val "")
   string(REPLACE "." "\\." _key_re "${key}")
   foreach(_line IN LISTS _lines)
      if(_line MATCHES "^[ \t]*${_key_re}[ \t]*=[ \t]*\"([^\"]*)\"")
         set(_val "${CMAKE_MATCH_1}")
         break()
      endif()
   endforeach()
   set(${out} "${_val}" PARENT_SCOPE)
endfunction()

# Configure <target> as a VPX plugin described by <cfg>: output name, export
# id, self-contained runtime, and an install rule to <prefix>/<id>/.
# Sets <target>_PLUGIN_ID in the caller's scope.
function(vpx_plugin target cfg)
   set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${cfg}")
   plugin_cfg_get("${cfg}" id _id)
   if(NOT _id MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
      message(FATAL_ERROR "${cfg}: id \"${_id}\" must be a valid C identifier "
                          "(it becomes the <id>PluginLoad / <id>PluginUnload export prefix).")
   endif()
   plugin_cfg_get("${cfg}" ${PLUGIN_LIB_KEY} _file)
   if(NOT _file)
      message(FATAL_ERROR "${cfg}: no [libraries] entry for \"${PLUGIN_LIB_KEY}\".")
   endif()
   if(APPLE)
      plugin_cfg_get("${cfg}" macos.x64 _mac_x64)
      if(_mac_x64 AND NOT _mac_x64 STREQUAL _file)
         message(FATAL_ERROR "${cfg}: macos.x64 and macos.arm64 must name the same file (universal binary).")
      endif()
   endif()
   get_filename_component(_name "${_file}" NAME_WLE)
   get_filename_component(_ext  "${_file}" LAST_EXT)
   message(STATUS "Plugin: ${_id} -> ${_file} (${PLUGIN_LIB_KEY})")

   # PLUGIN_ID is passed as a bare token so plugin_id.h can build the export
   # names (<id>PluginLoad / <id>PluginUnload) and the id string from it.
   target_compile_definitions(${target} PRIVATE PLUGIN_ID=${_id})
   target_include_directories(${target} SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/sdk")
   target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/src")
   set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME "${_name}" SUFFIX "${_ext}")

   # Windows: export exactly the two entry points. MSGPI_EXPORT only uses
   # __declspec(dllexport) with MSVC; under MinGW it's a visibility attribute,
   # which DLLs ignore, so without a .def file MinGW exports every symbol.
   if(WIN32)
      set(_def "${CMAKE_CURRENT_BINARY_DIR}/${target}.def")
      file(WRITE "${_def}" "EXPORTS\n   ${_id}PluginLoad\n   ${_id}PluginUnload\n")
      target_sources(${target} PRIVATE "${_def}")
   endif()

   # Self-contained C++ runtime: the API boundary is C, but our code is C++,
   # so don't depend on the user having our exact libstdc++/libgcc.
   if(WIN32 AND NOT MSVC) # MinGW
      target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
   elseif(APPLE)
      set_target_properties(${target} PROPERTIES INSTALL_RPATH "@loader_path" BUILD_WITH_INSTALL_RPATH ON)
   elseif(NOT WIN32) # Linux
      # --exclude-libs,ALL keeps the static runtime's symbols private, so they
      # can't clash with the host's own C++ runtime.
      target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++ -Wl,--exclude-libs,ALL)
      set_target_properties(${target} PROPERTIES INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH ON)
   endif()

   install(TARGETS ${target} LIBRARY DESTINATION "${_id}" RUNTIME DESTINATION "${_id}")
   install(FILES "${cfg}" DESTINATION "${_id}")
   set(${target}_PLUGIN_ID "${_id}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Global build settings (apply to the plugin, the core library, tools, tests)
# ---------------------------------------------------------------------------

# macOS: universal binary so one .dylib covers macos.x64 AND macos.arm64.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
   set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
   set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# The core library is static but ends up inside a shared plugin.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Export only what's marked MSGPI_EXPORT; hide everything else.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
# MSVC: /MT, statically link the runtime (must match across all targets).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
   add_compile_options(/utf-8 /Zc:__cplusplus /W3)
   add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
   add_compile_options(-Wall -Wextra)
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# qr_core: everything except the VPX glue (transport, devices, filtering,
# worker thread). Shared by the plugin, qrprobe and the tests.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE QR_CORE_SOURCES CONFIGURE_DEPENDS
   "${CMAKE_SOURCE_DIR}/src/core/*.cpp"
   "${CMAKE_SOURCE_DIR}/src/devices/*.cpp"
   "${CMAKE_SOURCE_DIR}/src/transport/*.cpp")
add_library(qr_core STATIC ${QR_CORE_SOURCES})
target_include_directories(qr_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(qr_core PUBLIC Threads::Threads)
if(WIN32)
   target_compile_definitions(qr_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
   target_link_libraries(qr_core PUBLIC advapi32) # registry: serial port listing
endif()

# ---------------------------------------------------------------------------
# The QR plugin
# ---------------------------------------------------------------------------
add_library(plugin MODULE "${CMAKE_SOURCE_DIR}/src/QRPlugin.cpp")
target_include_directories(plugin PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(plugin PRIVATE qr_core)
vpx_plugin(plugin "${CMAKE_SOURCE_DIR}/plugin.cfg")

# Ship the consumer API header with the plugin, for other plugin authors.
install(FILES "${CMAKE_SOURCE_DIR}/include/QRPlugin.h" DESTINATION "${plugin_PLUGIN_ID}/include")

# ---------------------------------------------------------------------------
# Optional: tools, tests, examples (off by default; CI builds only the plugin)
# ---------------------------------------------------------------------------
if(QR_BUILD_TOOLS)
   add_executable(qrprobe "${CMAKE_SOURCE_DIR}/tools/qrprobe.cpp")
   target_link_libraries(qrprobe PRIVATE qr_core)
endif()

if(QR_BUILD_TESTS)
   enable_testing()
   add_executable(qr_tests "${CMAKE_SOURCE_DIR}/tests/qr_tests.cpp")
   target_link_libraries(qr_tests PRIVATE qr_core)
   add_test(NAME qr_tests COMMAND qr_tests)

   # Loads the built plugin into a fake VPX host and checks load/unload.
   add_executable(qr_fake_host "${CMAKE_SOURCE_DIR}/tests/fake_host.cpp")
   target_include_directories(qr_fake_host SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/sdk")
   target_include_directories(qr_fake_host PRIVATE "${CMAKE_SOURCE_DIR}/include")
   target_link_libraries(qr_fake_host PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
   add_dependencies(qr_fake_host plugin)
   add_test(NAME plugin_lifecycle COMMAND qr_fake_host "$<TARGET_FILE:plugin>")
endif()

if(QR_BUILD_EXAMPLES)
   add_library(qrconsumer MODULE "${CMAKE_SOURCE_DIR}/examples/consumer/consumer.cpp")
   target_include_directories(qrconsumer PRIVATE "${CMAKE_SOURCE_DIR}/include")
   vpx_plugin(qrconsumer "${CMAKE_SOURCE_DIR}/examples/consumer/plugin.cfg")
endif()
