add_executable(YbxReadingSummaryTest YbxReadingSummaryTest.cpp ${REPO_ROOT}/src/util/YbxReadingSummary.cpp)
target_include_directories(YbxReadingSummaryTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(YbxReadingSummaryTest PRIVATE crosspoint_test_common bleturner_arduinojson GTest::gtest_main)
gtest_discover_tests(YbxReadingSummaryTest)

add_executable(YbxAwardsTest YbxAwardsTest.cpp ${REPO_ROOT}/src/util/YbxAwards.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp)
target_include_directories(YbxAwardsTest PRIVATE ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include)
target_link_libraries(YbxAwardsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(YbxAwardsTest)
