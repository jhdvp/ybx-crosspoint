add_executable(OtaFailureScreenTest OtaFailureScreenTest.cpp)
target_include_directories(OtaFailureScreenTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)
target_link_libraries(OtaFailureScreenTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(OtaFailureScreenTest)
