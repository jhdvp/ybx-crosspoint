# The SDK's Atom parser is the firmware's only reader for the YBX library tree.
# Compile it for the host against the vendored expat, with the two Arduino
# headers it includes stubbed to what it uses.
add_library(OpdsParserHost STATIC
  ${REPO_ROOT}/freeink-sdk/libs/network/Opds/src/OpdsParser.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(OpdsParserHost PUBLIC
  ${REPO_ROOT}/freeink-sdk/libs/network/Opds/include
  ${REPO_ROOT}/lib/expat
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

# Same limits the firmware builds with (platformio.ini).
target_compile_definitions(OpdsParserHost PUBLIC XML_GE=0 XML_CONTEXT_BYTES=0)

add_executable(OpdsLibraryTreeTest
  OpdsLibraryTreeTest.cpp
)

target_link_libraries(OpdsLibraryTreeTest PRIVATE
  OpdsParserHost
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OpdsLibraryTreeTest)
