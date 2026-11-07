add_executable(OtaReleasePolicyTest
  OtaReleasePolicyTest.cpp
  ${REPO_ROOT}/src/network/OtaReleasePolicy.cpp
)

target_link_libraries(OtaReleasePolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OtaReleasePolicyTest)
