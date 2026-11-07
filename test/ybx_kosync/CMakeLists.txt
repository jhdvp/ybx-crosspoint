add_executable(YbxKosyncDecisionTest YbxKosyncDecisionTest.cpp)

target_include_directories(YbxKosyncDecisionTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(YbxKosyncDecisionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(YbxKosyncDecisionTest)

add_executable(YbxDocumentIdentityTest YbxDocumentIdentityTest.cpp)
target_include_directories(YbxDocumentIdentityTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(YbxDocumentIdentityTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(YbxDocumentIdentityTest)

add_executable(KOReaderSyncClientTlsTest
  KOReaderSyncClientTlsTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderSyncClient.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderCredentialStore.cpp
)
target_include_directories(KOReaderSyncClientTlsTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/library_builder/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/HttpHeader
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Memory
)
target_include_directories(KOReaderSyncClientTlsTest SYSTEM PRIVATE
  ${arduinojson_SOURCE_DIR}/src
  ${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src
)
target_link_libraries(KOReaderSyncClientTlsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)
gtest_discover_tests(KOReaderSyncClientTlsTest)
