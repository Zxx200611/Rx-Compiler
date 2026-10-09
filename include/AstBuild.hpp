#pragma once

#include "RxParser.h"
#include "RxParserBaseVisitor.h"

#include <any>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rxast {

struct SourcePosition {
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 0;
};
struct SourceSpan { SourcePosition begin; SourcePosition end; };

struct Expr;
struct TypeSyntax;
struct Statement;
struct Item;
using ExprPtr = std::unique_ptr<Expr>;
using TypePtr = std::unique_ptr<TypeSyntax>;
using StatementPtr = std::unique_ptr<Statement>;
using ItemPtr = std::unique_ptr<Item>;

enum class IntegerSuffix { None, I32, U32, Isize, Usize };
struct IntegerLiteral {
    std::string raw, digits;
    unsigned base = 10;
    IntegerSuffix suffix = IntegerSuffix::None;
};
enum class PathNameKind { Identifier, SelfValue, SelfType };
struct PathSegment {
    SourceSpan span;
    std::string name;
    PathNameKind nameKind = PathNameKind::Identifier;
    bool hasGenericArguments = false;
    std::vector<TypePtr> typeArguments;
};
struct Path { SourceSpan span; std::vector<PathSegment> segments; };

struct UnitType {};
struct PathType { Path path; };
struct ReferenceType { bool isMutable = false; TypePtr pointee; };
struct ArrayType { TypePtr element; ExprPtr length; };
using TypeData = std::variant<UnitType, PathType, ReferenceType, ArrayType>;
struct TypeSyntax {
    SourceSpan span; TypeData data;
    TypeSyntax(SourceSpan s, TypeData d) : span(s), data(std::move(d)) {}
    ~TypeSyntax();
    TypeSyntax(const TypeSyntax&) = delete; TypeSyntax& operator=(const TypeSyntax&) = delete;
};

enum class UnaryOp { Negate, Not, Dereference };
enum class BinaryOp { Add, Subtract, Multiply, Divide, Remainder, ShiftLeft, ShiftRight,
    BitAnd, BitXor, BitOr, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    LogicalAnd, LogicalOr };
enum class AssignmentOp { Assign, Add, Subtract, Multiply, Divide, Remainder,
    BitAnd, BitOr, BitXor, ShiftLeft, ShiftRight };
struct BoolLiteral { bool value = false; };
struct UnitExpr {};
struct PathExpr { Path path; };
struct UnaryExpr { UnaryOp op; ExprPtr operand; };
struct BorrowExpr { bool isMutable = false; ExprPtr operand; };
struct BinaryExpr { BinaryOp op; ExprPtr left; ExprPtr right; };
struct AssignmentExpr { AssignmentOp op; ExprPtr target; ExprPtr value; };
struct CastExpr { ExprPtr operand; TypePtr target; };
struct ArrayExpr { std::vector<ExprPtr> elements; };
struct RepeatArrayExpr { ExprPtr element; ExprPtr count; };
struct FieldInitializer { SourceSpan span, nameSpan; std::string name; ExprPtr value; };
struct StructExpr { Path path; std::vector<FieldInitializer> fields; };
struct CallExpr { ExprPtr callee; std::vector<ExprPtr> arguments; };
struct MethodCallExpr { ExprPtr receiver; PathSegment method; std::vector<ExprPtr> arguments; };
struct FieldExpr { ExprPtr base; std::string name; SourceSpan nameSpan; };
struct IndexExpr { ExprPtr base; ExprPtr index; };
struct BlockExpr { std::vector<StatementPtr> statements; ExprPtr tail; };
struct IfExpr { ExprPtr condition; ExprPtr thenBranch; ExprPtr elseBranch; };
struct LoopExpr { ExprPtr body; };
struct WhileExpr { ExprPtr condition; ExprPtr body; };
struct ReturnExpr { ExprPtr value; };
struct BreakExpr { ExprPtr value; };
struct ContinueExpr {};
using ExprData = std::variant<IntegerLiteral, BoolLiteral, UnitExpr, PathExpr, UnaryExpr,
    BorrowExpr, BinaryExpr, AssignmentExpr, CastExpr, ArrayExpr, RepeatArrayExpr,
    StructExpr, CallExpr, MethodCallExpr, FieldExpr, IndexExpr, BlockExpr, IfExpr,
    LoopExpr, WhileExpr, ReturnExpr, BreakExpr, ContinueExpr>;
struct Expr {
    SourceSpan span; ExprData data;
    Expr(SourceSpan s, ExprData d) : span(s), data(std::move(d)) {}
    ~Expr();
    Expr(const Expr&) = delete; Expr& operator=(const Expr&) = delete;
};

struct Binding { SourceSpan span, nameSpan; std::string name; bool isMutable = false; };
struct EmptyStmt {};
struct LetStmt { Binding binding; TypePtr annotation; ExprPtr initializer; };
struct ExprStmt { ExprPtr expression; bool hasSemicolon = false; };
using StatementData = std::variant<EmptyStmt, LetStmt, ExprStmt>;
struct Statement {
    SourceSpan span; StatementData data;
    Statement(SourceSpan s, StatementData d) : span(s), data(std::move(d)) {}
    ~Statement();
    Statement(const Statement&) = delete; Statement& operator=(const Statement&) = delete;
};

enum class ReceiverKind { Value, MutableValue, SharedReference, MutableReference };
struct Receiver { SourceSpan span; ReceiverKind kind = ReceiverKind::Value; };
struct Parameter { SourceSpan span; Binding binding; TypePtr type; };
struct FunctionDecl {
    std::string name; SourceSpan nameSpan; std::optional<Receiver> receiver;
    std::vector<Parameter> parameters; TypePtr returnType;
    bool hasExplicitReturnType = false, hasLifetimeParameters = false; ExprPtr body;
};
enum class DeriveTrait { Copy, Clone, PartialEq, Eq };
struct DeriveRequest { SourceSpan span; DeriveTrait trait; };
struct FieldDecl { SourceSpan span, nameSpan; std::string name; TypePtr type; };
struct StructDecl { std::string name; SourceSpan nameSpan; std::vector<DeriveRequest> derives; std::vector<FieldDecl> fields; };
struct ConstDecl { std::string name; SourceSpan nameSpan; TypePtr type; ExprPtr value; };
struct ImplDecl { TypePtr target; std::vector<ItemPtr> items; };
using ItemData = std::variant<FunctionDecl, StructDecl, ConstDecl, ImplDecl>;
struct Item {
    SourceSpan span; ItemData data;
    Item(SourceSpan s, ItemData d) : span(s), data(std::move(d)) {}
    ~Item();
    Item(const Item&) = delete; Item& operator=(const Item&) = delete;
};
struct Program { SourceSpan span; std::string sourceName; std::vector<ItemPtr> items; };
using Ast = std::unique_ptr<Program>;

class AstBuilder final : public rxantlr::RxParserBaseVisitor {
public:
    explicit AstBuilder(const rxantlr::RxParser& parser);
    Ast build(rxantlr::RxParser::CrateContext* tree);
    ItemPtr buildItem(rxantlr::RxParser::ItemContext* tree);
    ExprPtr buildExpression(antlr4::ParserRuleContext* tree);
    TypePtr buildType(rxantlr::RxParser::TypeRefContext* tree);
    StatementPtr buildLet(rxantlr::RxParser::LetStatementContext* tree);
    std::any visitCrate(rxantlr::RxParser::CrateContext* tree) override;
private:
    const rxantlr::RxParser& parser_;
    std::string ruleName(antlr4::ParserRuleContext* tree) const;
    void requireCleanTree(antlr4::tree::ParseTree* tree) const;
    ItemPtr item(antlr4::ParserRuleContext* tree);
    ExprPtr expression(antlr4::ParserRuleContext* tree);
    ExprPtr primary(antlr4::ParserRuleContext* tree);
    ExprPtr fold(antlr4::ParserRuleContext* tree);
    ExprPtr unary(antlr4::ParserRuleContext* tree);
    ExprPtr cast(antlr4::ParserRuleContext* tree);
    ExprPtr postfix(antlr4::ParserRuleContext* tree);
    ExprPtr suffix(ExprPtr base, antlr4::ParserRuleContext* tree);
    ExprPtr block(rxantlr::RxParser::BlockExpressionContext* tree);
    StatementPtr statement(rxantlr::RxParser::StatementContext* tree);
    StatementPtr let(rxantlr::RxParser::LetStatementContext* tree);
    ExprPtr constant(antlr4::ParserRuleContext* tree);
    TypePtr type(antlr4::ParserRuleContext* tree);
    Binding binding(rxantlr::RxParser::IdentifierBindingContext* tree);
    Path path(antlr4::ParserRuleContext* tree);
    PathSegment segment(antlr4::ParserRuleContext* tree);
    std::vector<ExprPtr> arguments(rxantlr::RxParser::CallArgumentsContext* tree);
};

enum class DiagnosticPhase { Lexical, Syntax, Ast, Semantic };
struct Diagnostic { DiagnosticPhase phase; SourceSpan span; std::string message; };
struct ParseResult { Ast program; std::vector<Diagnostic> diagnostics; explicit operator bool() const noexcept { return !!program; } };
ParseResult parseSource(const std::string& source, std::string sourceName = {});

} // namespace rxast
