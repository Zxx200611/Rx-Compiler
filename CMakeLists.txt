cmake_minimum_required(VERSION 3.15)
project(RxCompiler LANGUAGES CXX)

set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(WITH_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(tools/antlr4-cpp-runtime-4.13.2
                 "${CMAKE_CURRENT_BINARY_DIR}/antlr4-runtime")

# These sources are generated from grammar/{Lexer,Parser}.g4 with ANTLR 4.13.2.
# Keeping them in Git makes the judge build independent of Java and network access.
add_library(rx_antlr4 STATIC
    generated/RxLexer.cpp
    generated/RxParser.cpp
    generated/RxParserVisitor.cpp
    generated/RxParserBaseVisitor.cpp)
target_compile_features(rx_antlr4 PUBLIC cxx_std_17)
target_include_directories(rx_antlr4 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/generated")
target_link_libraries(rx_antlr4 PUBLIC antlr4_static)

add_library(rx_frontend src/AstBuild.cpp src/Semantic.cpp)
target_include_directories(rx_frontend PUBLIC include)
target_link_libraries(rx_frontend PUBLIC rx_antlr4)
target_compile_options(rx_frontend PRIVATE -Wall -Wextra -Wpedantic)
add_executable(rx-compiler src/main.cpp)
target_link_libraries(rx-compiler PRIVATE rx_frontend)

