add_executable(ReadingStatsTest
  ReadingStatsTest.cpp
  ReadingStatsFilesTest.cpp
  ${REPO_ROOT}/src/util/ReadingStats.cpp
)
# The file-level tests drive ReadingStats.cpp against the in-memory Storage stub.
target_include_directories(ReadingStatsTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
)
target_link_libraries(ReadingStatsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReadingStatsTest)
