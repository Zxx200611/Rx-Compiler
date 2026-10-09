cmake_minimum_required(VERSION 3.15)
project(RxCompiler LANGUAGES CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/RxAntlr4.cmake")
rx_add_antlr4(rx_antlr4)
add_library(rx_frontend src/AstBuild.cpp src/AstPrint.cpp src/Semantic.cpp)
target_include_directories(rx_frontend PUBLIC include)
target_link_libraries(rx_frontend PUBLIC rx_antlr4)
target_compile_options(rx_frontend PRIVATE -Wall -Wextra -Wpedantic)
add_executable(rx-compiler src/main.cpp)
target_link_libraries(rx-compiler PRIVATE rx_frontend)

