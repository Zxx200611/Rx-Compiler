cmake_minimum_required(VERSION 3.15)
project(RxAntlrSmoke LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/RxAntlr4.cmake")
rx_add_antlr4(rx_antlr4)

add_executable(rx-parse-smoke main.cpp)
target_link_libraries(rx-parse-smoke PRIVATE rx_antlr4)

enable_testing()
add_test(NAME parse_valid COMMAND rx-parse-smoke "${CMAKE_CURRENT_SOURCE_DIR}/valid.rx")
set_tests_properties(parse_valid PROPERTIES PASS_REGULAR_EXPRESSION "\\(crate")
add_test(NAME reject_syntax COMMAND rx-parse-smoke "${CMAKE_CURRENT_SOURCE_DIR}/invalid-syntax.rx")
add_test(NAME reject_lexical COMMAND rx-parse-smoke "${CMAKE_CURRENT_SOURCE_DIR}/invalid-lexical.rx")
set_tests_properties(reject_syntax reject_lexical PROPERTIES WILL_FAIL TRUE)

