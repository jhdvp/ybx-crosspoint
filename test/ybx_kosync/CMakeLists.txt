add_executable(YbxKosyncDecisionTest YbxKosyncDecisionTest.cpp)

target_include_directories(YbxKosyncDecisionTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(YbxKosyncDecisionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(YbxKosyncDecisionTest)
