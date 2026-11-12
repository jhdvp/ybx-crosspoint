add_executable(YbxServerMirrorTest YbxServerMirrorTest.cpp)

target_include_directories(YbxServerMirrorTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(YbxServerMirrorTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(YbxServerMirrorTest)