add_executable(CatalogFetchLimiterTest
  CatalogFetchLimiterTest.cpp
)

target_link_libraries(CatalogFetchLimiterTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CatalogFetchLimiterTest)
