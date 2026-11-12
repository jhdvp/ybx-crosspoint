# PersistableStore's atomic save (temp file + replaceFile) is verified against
# an in-memory HalStorage stub. ArduinoJson is the version the firmware pins
# (platformio.ini: 7.4.2); the same declaration lib/BlePageTurner/test and
# test/plugin_update_cache use, so all three share one checkout.
include(FetchContent)
FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  SOURCE_SUBDIR no-build
)
FetchContent_MakeAvailable(arduinojson)

add_executable(PersistableStoreTest
  PersistableStoreTest.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
)

target_include_directories(PersistableStoreTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Serialization
)

# The host build defines no ARDUINO macro, so String support must be enabled
# explicitly for serializeJson(doc, String&) / deserializeJson(doc, String&).
target_compile_definitions(PersistableStoreTest PRIVATE ARDUINOJSON_ENABLE_ARDUINO_STRING=1)

# ArduinoJson is third-party code: SYSTEM keeps its headers out of -Wall -Wextra.
target_include_directories(PersistableStoreTest SYSTEM PRIVATE
  ${arduinojson_SOURCE_DIR}/src
)

target_link_libraries(PersistableStoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PersistableStoreTest)
