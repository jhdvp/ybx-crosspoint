add_executable(YbxAchievementsTest
  YbxAchievementsTest.cpp
  ${REPO_ROOT}/src/util/YbxAchievements.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp
)

target_include_directories(YbxAchievementsTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include
)

target_link_libraries(YbxAchievementsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(YbxAchievementsTest)
