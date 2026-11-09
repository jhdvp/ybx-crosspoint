add_executable(PowerSettingsTest PowerSettingsTest.cpp ${REPO_ROOT}/lib/hal/HalPowerSettings.cpp)
target_include_directories(PowerSettingsTest PRIVATE stubs ${REPO_ROOT}/lib/hal)
target_link_libraries(PowerSettingsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(PowerSettingsTest)
