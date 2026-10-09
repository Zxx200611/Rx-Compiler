include_guard(GLOBAL)

get_filename_component(RX_ANTLR4_TOOLS_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(RX_ANTLR4_REPO_DIR "${RX_ANTLR4_TOOLS_DIR}/.." ABSOLUTE)
set(RX_ANTLR4_PREFIX "${RX_ANTLR4_TOOLS_DIR}/installed/antlr4-4.13.2")
set(RX_ANTLR4_JAR "${RX_ANTLR4_TOOLS_DIR}/antlr-4.13.2-complete.jar")

if(NOT EXISTS "${RX_ANTLR4_PREFIX}/lib/cmake/antlr4-runtime/antlr4-runtime-config.cmake")
    message(FATAL_ERROR "Run bash tools/setup-antlr4.sh in WSL Ubuntu first.")
endif()
find_package(antlr4-runtime 4.13.2 EXACT CONFIG REQUIRED
    PATHS "${RX_ANTLR4_PREFIX}/lib/cmake/antlr4-runtime" NO_DEFAULT_PATH)
find_package(Java 11 REQUIRED COMPONENTS Runtime)
find_program(RX_ANTLR4_BASH bash)
if(NOT RX_ANTLR4_BASH)
    message(FATAL_ERROR "Bash is required. Configure this project inside WSL Ubuntu.")
endif()

function(rx_add_antlr4 target)
    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/${target}-generated")
    add_custom_command(
        OUTPUT "${generated_dir}/RxLexer.cpp" "${generated_dir}/RxLexer.h"
               "${generated_dir}/RxParser.cpp" "${generated_dir}/RxParser.h"
               "${generated_dir}/RxParserVisitor.cpp" "${generated_dir}/RxParserVisitor.h"
               "${generated_dir}/RxParserBaseVisitor.cpp" "${generated_dir}/RxParserBaseVisitor.h"
        BYPRODUCTS "${generated_dir}/RxLexer.tokens" "${generated_dir}/RxLexer.interp"
                   "${generated_dir}/RxParser.tokens" "${generated_dir}/RxParser.interp"
                   "${generated_dir}/grammar/RxLexer.g4" "${generated_dir}/grammar/RxParser.g4"
        COMMAND "${RX_ANTLR4_BASH}" "${RX_ANTLR4_TOOLS_DIR}/generate-antlr4.sh" "${generated_dir}"
        DEPENDS "${RX_ANTLR4_REPO_DIR}/template/grammar/Lexer.g4"
                "${RX_ANTLR4_REPO_DIR}/template/grammar/Parser.g4"
                "${RX_ANTLR4_JAR}" "${RX_ANTLR4_TOOLS_DIR}/generate-antlr4.sh"
        VERBATIM)

    add_library(${target} STATIC
        "${generated_dir}/RxLexer.cpp"
        "${generated_dir}/RxParser.cpp"
        "${generated_dir}/RxParserVisitor.cpp"
        "${generated_dir}/RxParserBaseVisitor.cpp")
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_include_directories(${target} PUBLIC "${generated_dir}")
    target_link_libraries(${target} PUBLIC antlr4_static)
endfunction()
