add_executable(NativeNotesTest NativeNotesTest.cpp)
target_include_directories(NativeNotesTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(NativeNotesTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(NativeNotesTest)


add_executable(NativeNotesStorageTest
  NativeNotesStorageTest.cpp
  ${REPO_ROOT}/src/nativeapps/notes/NotesStorage.cpp
)
target_include_directories(NativeNotesStorageTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
)
target_link_libraries(NativeNotesStorageTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(NativeNotesStorageTest)
