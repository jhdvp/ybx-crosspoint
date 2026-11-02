add_executable(FirmwareBoardTagScannerTest
  FirmwareBoardTagScannerTest.cpp
  ${REPO_ROOT}/src/network/FirmwareBoardTag.cpp
)
target_include_directories(FirmwareBoardTagScannerTest PRIVATE
  ${REPO_ROOT}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/network
)
target_link_libraries(FirmwareBoardTagScannerTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(FirmwareBoardTagScannerTest)
