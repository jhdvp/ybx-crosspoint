add_executable(ClippingsManagerTest ClippingsManagerTest.cpp ${REPO_ROOT}/src/clippings/ClippingsManager.cpp
                                    ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_link_libraries(ClippingsManagerTest PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(ClippingsManagerTest PRIVATE stubs ../library_builder/stubs ${REPO_ROOT}/src
                                                        ${REPO_ROOT}/lib/Utf8)
gtest_discover_tests(ClippingsManagerTest TEST_PREFIX "ClippingsManagerTest.")
