# vip_sd — Plugify C++ plugin: port of the Pisex VIP module "[VIP] Show Damage".
#
# Pure s2sdk: no s2guard and no pisexcompat. All safety rules (the map gate, the slot/controller
# checks, the controller's m_iTeamNum read, the guarded centre print, the "player_hurt" hook) live
# in src/plugin.cpp.
#
#   plugify-vip_sd  the plugin shared library (entry "bin/vip_sd" -> bin/libvip_sd.so)
#   vip_sd_tests    engine-free unit tests: the map gate, HasClient/HasController, GetPlayerTeam,
#                   the guarded PrintToCenter and the player_hurt filter, driven through fake
#                   s2sdk/vip bindings (build/tests/vip_sd_tests)

set(VIP_SD_VERSION "1.1.0")

# Binding globals the plugify module fills from the plugins declared as dependencies:
#   __s2sdk_*   (s2sdk_bindings)
#   __vip_*     (vip consumer bindings, generated into plugins/vip/include/external/plugify/include)
# In the monorepo plugins/vip is added first ("vip" < "vip_sd"), so vip_bindings already exists;
# a standalone exported repo only carries plugins/vip/include/ headers + vip.cpp and has no
# plugins/vip/CMakeLists.txt - define vip_bindings here when absent.
if(NOT TARGET vip_bindings)
    add_library(vip_bindings OBJECT
            "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include/vip/vip.cpp")
    target_include_directories(vip_bindings SYSTEM PUBLIC
            "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
            "${CMAKE_SOURCE_DIR}/sdk/include")
    target_compile_definitions(vip_bindings PUBLIC _GLIBCXX_USE_CXX11_ABI=1)
endif()

add_library(plugify-vip_sd SHARED
        src/plugin.cpp
        # The __s2sdk_* / __vip_* binding globals the plugify module fills from the manifest's
        # dependencies; compiled in, never linked as a library.
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
        plugify_sdk
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

# --- tests --------------------------------------------------------------------------------------
# vip_sd_tests: no engine. The plugin TU is compiled into the test (the map gate and the client
# checks live in its anonymous namespace) and the __s2sdk_* / __vip_* binding globals are
# recording fakes: the map gate (B1/B2), HasClient/HasController and GetPlayerTeam (C2/C8), the
# guarded PrintToCenter and the player_hurt filter. No pisexcompat.
add_executable(vip_sd_tests
        tests/vip_sd_tests.cpp
        $<TARGET_OBJECTS:s2sdk_bindings>
        $<TARGET_OBJECTS:vip_bindings>
)
target_include_directories(vip_sd_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(vip_sd_tests SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/plugins/vip/include"
        "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
        "${CMAKE_SOURCE_DIR}/sdk/include")
target_link_libraries(vip_sd_tests PRIVATE
        plugify_sdk
        -static-libstdc++ -static-libgcc
)
target_compile_options(vip_sd_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(vip_sd_tests PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        PLUGIFY_MODULE="vip_sd"
        PLUGIFY_IS_DEBUG=0
        PLUGIFY_IS_RELEASE=1
)
set_target_properties(vip_sd_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
