#include "Semantic.hpp"
#include "AstPrint.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <exception>
#include <string>

int main(int argc, char** argv) {
    bool dumpAst = false;
    const char* path = nullptr;
    if (argc == 2) path = argv[1];
    else if (argc == 3 && std::string(argv[1]) == "--ast") { dumpAst = true; path = argv[2]; }
    else return 2;
    std::ifstream in(path, std::ios::binary);
    if (!in) return 2;
    std::string source((std::istreambuf_iterator<char>(in)), {});
    try {
        auto parsed = rxast::parseSource(source, path);
        // if (dumpAst && parsed) rxast::printAst(*parsed.program, std::cout);
        auto checked = parsed ? rxast::checkProgram(*parsed.program)
                              : rxast::SemanticResult{std::move(parsed.diagnostics)};
        for (const auto& d : checked.diagnostics)
            std::cerr << path << ':' << d.span.begin.line << ':' << d.span.begin.column + 1 << ": " << d.message << '\n';
        return checked ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "compiler internal error: " << e.what() << '\n';
        return 2;
    }
}
