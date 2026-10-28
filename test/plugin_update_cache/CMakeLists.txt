# The inline rules are header-only; the SD half compiles PluginUpdateCache.cpp
# against stubs/HalStorage.h (in-memory files) and a fake plugin-folder table.
# ArduinoJson is the version the firmware pins (platformio.ini: 7.4.2) and is
# the same declaration lib/BlePageTurner/test uses, so both share one checkout.
include(FetchContent)
FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  SOURCE_SUBDIR no-build
)
FetchContent_MakeAvailable(arduinojson)

add_executable(PluginUpdateCacheTest
  PluginUpdateCacheTest.cpp
  ${REPO_ROOT}/src/util/PluginUpdateCache.cpp
)

target_include_directories(PluginUpdateCacheTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/util
)

# ArduinoJson is third-party code: SYSTEM keeps its headers out of -Wall -Wextra.
target_include_directories(PluginUpdateCacheTest SYSTEM PRIVATE
  ${arduinojson_SOURCE_DIR}/src
)

target_link_libraries(PluginUpdateCacheTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PluginUpdateCacheTest)
