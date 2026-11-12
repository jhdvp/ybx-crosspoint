add_executable(OtaFailureScreenTest OtaFailureScreenTest.cpp)

target_include_directories(OtaFailureScreenTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(OtaFailureScreenTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OtaFailureScreenTest)