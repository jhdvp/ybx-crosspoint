add_executable(NativeNotesTest NativeNotesTest.cpp)
target_include_directories(NativeNotesTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(NativeNotesTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(NativeNotesTest)
