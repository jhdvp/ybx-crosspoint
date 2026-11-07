add_executable(ReadingStatsTest ReadingStatsTest.cpp ReadingStatsSnapshotTest.cpp ReadingStatsArchiveTest.cpp)
target_include_directories(ReadingStatsTest PRIVATE ${REPO_ROOT}/src stubs)
target_link_libraries(ReadingStatsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReadingStatsTest)

add_executable(ReadingStatsStorageTest ReadingStatsStorageTest.cpp ../../src/util/ReadingStats.cpp)
target_include_directories(ReadingStatsStorageTest PRIVATE stubs ../../src)
target_link_libraries(ReadingStatsStorageTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReadingStatsStorageTest)
