// Dependency smoke check only: prints a parse tree, not the compiler's AST.
#include "antlr4-runtime.h"
#include "RxLexer.h"
#include "RxParser.h"

#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

class ErrorCollector final : public antlr4::BaseErrorListener {
public:
    bool failed = false;

    void syntaxError(antlr4::Recognizer*, antlr4::Token*,
                     std::size_t line, std::size_t column,
                     const std::string& message, std::exception_ptr) override {
        failed = true;
        std::cerr << line << ':' << column + 1 << ": " << message << '\n';
    }
};

bool isInvalidToken(std::size_t type) {
    return type == rxantlr::RxLexer::INVALID_NUMBER
        || type == rxantlr::RxLexer::INVALID_LIFETIME
        || type == rxantlr::RxLexer::INVALID_CHARACTER_LITERAL
        || type == rxantlr::RxLexer::UNTERMINATED_BLOCK_COMMENT
        || type == rxantlr::RxLexer::ERROR_CHAR;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: rx-parse-smoke source.rx\n";
        return 1;
    }
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open source file.\n";
        return 1;
    }
    const std::string source((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    for (unsigned char byte : source) {
        if (byte > 0x7f) {
            std::cerr << "Source must be ASCII.\n";
            return 1;
        }
    }

    ErrorCollector lexErrors;
    antlr4::ANTLRInputStream input(source);
    rxantlr::RxLexer lexer(&input);
    lexer.removeErrorListeners();
    lexer.addErrorListener(&lexErrors);
    antlr4::CommonTokenStream tokens(&lexer);
    tokens.fill();
    for (antlr4::Token* token : tokens.getTokens()) {
        if (isInvalidToken(token->getType())) {
            std::cerr << token->getLine() << ':' << token->getCharPositionInLine() + 1
                      << ": Invalid token: " << token->getText() << '\n';
            return 1;
        }
    }
    if (lexErrors.failed) return 1;
    tokens.seek(0);

    ErrorCollector parseErrors;
    rxantlr::RxParser parser(&tokens);
    parser.removeErrorListeners();
    parser.addErrorListener(&parseErrors);
    auto* tree = parser.crate();
    if (parseErrors.failed || parser.getNumberOfSyntaxErrors() != 0) return 1;
    std::cout << tree->toStringTree(&parser) << '\n';
    return 0;
}
