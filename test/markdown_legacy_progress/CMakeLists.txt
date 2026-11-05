add_executable(MarkdownLegacyProgressTest MarkdownLegacyProgressTest.cpp)
target_include_directories(MarkdownLegacyProgressTest PRIVATE
  ${REPO_ROOT}/test/notes/stubs
  ${REPO_ROOT}/src
)
target_link_libraries(MarkdownLegacyProgressTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(MarkdownLegacyProgressTest)
