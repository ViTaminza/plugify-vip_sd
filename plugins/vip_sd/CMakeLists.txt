# vip_sd — Plugify C++ plugin: port of the Pisex VIP module "[VIP] Show Damage".
#
#   plugify-vip_sd  the plugin shared library (entry "bin/vip_sd" -> bin/libvip_sd.so)

set(VIP_SD_VERSION "1.0.1")

# Binding globals the plugify module fills from the plugins declared as dependencies:
#   __s2sdk_*   (s2sdk_bindings)
#   __vip_*     (vip consumer bindings, generated into plugins/vip/include/external/plugify/include)
if(NOT TARGET vip_bindings)
    message(FATAL_ERROR "vip_sd needs the generated vip consumer bindings (plugins/vip/CMakeLists.txt, target vip_bindings)")
endif()

add_library(plugify-vip_sd SHARED
        src/plugin.cpp
        $<TARGET_OBJECTS:s2sdk_bindings>
        $<TARGET_OBJECTS:vip_bindings>
)
target_include_directories(plugify-vip_sd PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# SYSTEM: warnings from the generated vip/s2sdk/plg headers must not fail -Werror on our sources.
target_include_directories(plugify-vip_sd SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/plugins/vip/include"
        "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
        "${CMAKE_SOURCE_DIR}/sdk/include")
target_link_libraries(plugify-vip_sd PRIVATE
        pisexcompat
        -static-libstdc++ -static-libgcc
)
target_compile_options(plugify-vip_sd PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(plugify-vip_sd PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        PLUGIFY_MODULE="vip_sd"
        PLUGIFY_IS_DEBUG=0
        PLUGIFY_IS_RELEASE=1
)
set_target_properties(plugify-vip_sd PROPERTIES OUTPUT_NAME vip_sd)

if(UNIX AND NOT APPLE)
    target_link_options(plugify-vip_sd PRIVATE
            "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/sym/version_script.lds")
endif()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/vip_sd.pplugin.in"
        "${CMAKE_CURRENT_BINARY_DIR}/vip_sd.pplugin"
)
