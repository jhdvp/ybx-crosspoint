add_executable(YbxAccountModelTest YbxAccountModelTest.cpp)
target_include_directories(YbxAccountModelTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)
target_link_libraries(YbxAccountModelTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(YbxAccountModelTest)
