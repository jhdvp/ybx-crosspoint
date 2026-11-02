cmake_minimum_required(VERSION 3.16)
project(OtaReleasePolicyStandaloneTest CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(OtaReleasePolicyStandaloneTest
  ../ota_release_policy/OtaReleasePolicyTest.cpp
  ${REPO_ROOT}/src/network/OtaReleasePolicy.cpp
)

target_include_directories(OtaReleasePolicyStandaloneTest PRIVATE
  ${REPO_ROOT}
)

target_link_libraries(OtaReleasePolicyStandaloneTest PRIVATE
  GTest::gtest_main
)

gtest_discover_tests(OtaReleasePolicyStandaloneTest)