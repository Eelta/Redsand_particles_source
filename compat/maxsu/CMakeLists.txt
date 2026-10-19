cmake_minimum_required(VERSION 3.24)
project(MaxsuBlockSpark VERSION 1.0.1 LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)

set(UPSTREAM "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/MaxsuBlockSpark-ng")
set(DKUTIL "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/DKUtil-Maxsu")
set(REDSAND_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/../../.build" CACHE PATH "Existing validated Redsand dependency build")
set(COMMONLIB "${REDSAND_BUILD}/CommonLibSSE-NG")
set(DEPS "${REDSAND_BUILD}/Precision/build-release/vcpkg_installed/x64-windows-static-md")
set(CLIB "${REDSAND_BUILD}/Precision/build-release/src/CommonLibSSE/Release/CommonLibSSE.lib")
if(NOT EXISTS "${CLIB}")
  message(FATAL_ERROR "Build Redsand's pinned SE/AE CommonLib dependency first: ${CLIB}")
endif()
list(PREPEND CMAKE_PREFIX_PATH "${DEPS}")
find_package(spdlog CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)
set(SKSEPluginAuthor "Maxsu; NG: doodlum; Redsand compatibility")
configure_file("${UPSTREAM}/cmake/Version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/include/Version.h" @ONLY)
configure_file("${UPSTREAM}/cmake/version.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/version.rc" @ONLY)
file(GLOB PLUGIN_SOURCES CONFIGURE_DEPENDS "${UPSTREAM}/src/*.cpp")
add_library(MaxsuBlockSpark SHARED ${PLUGIN_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
target_include_directories(MaxsuBlockSpark PRIVATE
  "${UPSTREAM}/src" "${CMAKE_CURRENT_BINARY_DIR}/include" "${DKUTIL}/include"
  "${COMMONLIB}/include" "${COMMONLIB}/extern/openvr/headers" "${DEPS}/include")
target_compile_definitions(MaxsuBlockSpark PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
  WINVER=0x0601 _WIN32_WINNT=0x0601 SKSE_SUPPORT_XBYAK=1 SKSE_SUPPORT_PATCH_SAFETY=1
  ENABLE_SKYRIM_SE=1 ENABLE_SKYRIM_AE=1 HAS_SKYRIM_MULTI_TARGETING=1 COMMONLIB_VERSION="7.0.0")

target_compile_options(MaxsuBlockSpark PRIVATE /EHsc /utf-8 /permissive- /Zc:preprocessor /wd4200 /wd4005 /wd4828 /Zi)
target_precompile_headers(MaxsuBlockSpark PRIVATE "${UPSTREAM}/src/PCH.h")
target_link_libraries(MaxsuBlockSpark PRIVATE "${CLIB}" spdlog::spdlog Microsoft::DirectXTK
  advapi32 bcrypt d3d11 d3dcompiler dbghelp dxgi ole32 version shell32 user32)
target_link_options(MaxsuBlockSpark PRIVATE /DEBUG:FULL /INCREMENTAL:NO /OPT:REF /OPT:ICF)

enable_testing()
add_library(mock_precision SHARED tests/mock_precision.cpp)
set_target_properties(mock_precision PROPERTIES OUTPUT_NAME Precision)
add_executable(bridge_test tests/bridge_test.cpp)
target_include_directories(bridge_test PRIVATE "${UPSTREAM}/src")
target_compile_definitions(bridge_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME bridge_optional_abi_and_live_selection COMMAND bridge_test "$<TARGET_FILE:mock_precision>")
