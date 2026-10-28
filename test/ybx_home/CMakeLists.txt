add_executable(YbxHomeLayoutTest YbxHomeLayoutTest.cpp)
target_include_directories(YbxHomeLayoutTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)
target_link_libraries(YbxHomeLayoutTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(YbxHomeLayoutTest)
