add_executable(NativeNoteContextStoreTest
  NativeNoteContextStoreTest.cpp
  ${REPO_ROOT}/src/util/YbxNoteContextStore.cpp
)
target_include_directories(NativeNoteContextStoreTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Txt
  ${REPO_ROOT}/test/notes/stubs
)
target_link_libraries(NativeNoteContextStoreTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(NativeNoteContextStoreTest)
