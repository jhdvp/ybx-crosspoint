add_executable(ReaderSessionDeliveryTest
  ReaderSessionDeliveryTest.cpp
  ${REPO_ROOT}/src/activities/reader/ReaderSession.cpp
)
target_include_directories(ReaderSessionDeliveryTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/activities/reader
)
target_link_libraries(ReaderSessionDeliveryTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderSessionDeliveryTest)
