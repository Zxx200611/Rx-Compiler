#include "AstBuild.hpp"

#include "RxLexer.h"
#include "antlr4-runtime.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace rxast {

TypeSyntax::~TypeSyntax() = default;
Expr::~Expr() = default;
Statement::~Statement() = default;
Item::~Item() = default;

namespace {
SourceSpan spanOfToken(const antlr4::Token* token) {
    SourceSpan span;
    if (!token) return span;
    span.begin.offset = token->getStartIndex() == INVALID_INDEX ? 0 : static_cast<std::size_t>(token->getStartIndex());
    span.begin.line = token->getLine();
    span.begin.column = token->getCharPositionInLine();
    span.end = span.begin;
    span.end.offset = token->getStopIndex() == INVALID_INDEX ? span.begin.offset : static_cast<std::size_t>(token->getStopIndex() + 1);
    return span;
}
// 得到parsetree中一个结点在原代码中的位置
SourceSpan spanOf(const antlr4::tree::ParseTree* tree) {
    SourceSpan span;
    if (const auto* ctx = dynamic_cast<const antlr4::ParserRuleContext*>(tree)) {
        if (ctx->start) {
            span.begin.offset = ctx->start->getStartIndex() == INVALID_INDEX ? 0 : static_cast<std::size_t>(ctx->start->getStartIndex());
            span.begin.line = ctx->start->getLine();
            span.begin.column = ctx->start->getCharPositionInLine();
        }
        if (ctx->stop) {
            const auto stop = ctx->stop->getStopIndex();
            span.end = span.begin;
            span.end.offset = stop == INVALID_INDEX ? span.begin.offset : static_cast<std::size_t>(stop + 1);
            span.end.line = ctx->stop->getLine();
            span.end.column = ctx->stop->getCharPositionInLine();
            if (stop != INVALID_INDEX) span.end.column += static_cast<std::size_t>(ctx->stop->getText().size());
        }
    } else if (const auto* terminal = dynamic_cast<const antlr4::tree::TerminalNode*>(tree)) {
        return spanOfToken(terminal->getSymbol());
    }
    return span;
}
// 得到前序遍历第一个terminal结点
const antlr4::tree::TerminalNode* firstTerminal(const antlr4::tree::ParseTree* tree) {
    if (!tree) return nullptr;
    if (const auto* terminal = dynamic_cast<const antlr4::tree::TerminalNode*>(tree)) return terminal;
    for (auto* child : tree->children) if (const auto* result = firstTerminal(child)) return result;
    return nullptr;
}
// 得到parsetree树中所有的terminal结点
std::vector<antlr4::tree::TerminalNode*> terminals(const antlr4::tree::ParseTree* tree) {
    std::vector<antlr4::tree::TerminalNode*> result;
    if (!tree) return result;
    if (auto* terminal = dynamic_cast<antlr4::tree::TerminalNode*>(const_cast<antlr4::tree::ParseTree*>(tree))) {
        result.push_back(terminal);
        return result;
    }
    for (auto* child : tree->children) {
        auto nested = terminals(child);
        result.insert(result.end(), nested.begin(), nested.end());
    }
    return result;
}
// 获取结点的所有儿子
std::vector<antlr4::tree::ParseTree*> ruleChildren(antlr4::tree::ParseTree* tree) {
    std::vector<antlr4::tree::ParseTree*> result;
    if (!tree) return result;
    for (auto* child : tree->children) {
        if (dynamic_cast<antlr4::tree::TerminalNode*>(child) == nullptr) result.push_back(child);
    }
    return result;
}
// 判断一个string是否是结点的儿子
bool isRule(const antlr4::tree::ParseTree* tree, const std::string& name,
            const rxantlr::RxParser& parser) {
    const auto* ctx = dynamic_cast<const antlr4::ParserRuleContext*>(tree);
    if (!ctx) return false;
    const auto index = ctx->getRuleIndex();
    const auto& names = parser.getRuleNames();
    return static_cast<std::size_t>(index) < names.size() && names[static_cast<std::size_t>(index)] == name;
}

bool isIntegerText(const std::string& value) {
    return !value.empty() && std::isdigit(static_cast<unsigned char>(value.front()));
}
// 判断代码中的字符串类型和进制，并提取出数位
IntegerLiteral integerLiteral(const std::string& raw) {
    IntegerLiteral result;
    result.raw = raw;
    std::string value;
    for (char ch : raw) if (ch != '_') value.push_back(ch);
    auto suffix = IntegerSuffix::None;
    const std::pair<std::string_view, IntegerSuffix> suffixes[] = {
        {"isize", IntegerSuffix::Isize}, {"usize", IntegerSuffix::Usize},
        {"i32", IntegerSuffix::I32}, {"u32", IntegerSuffix::U32},
    };
    for (const auto& candidate : suffixes) {
        if (value.size() >= candidate.first.size() && value.compare(value.size() - candidate.first.size(), candidate.first.size(), candidate.first) == 0) {
            suffix = candidate.second;
            value.resize(value.size() - candidate.first.size());
            break;
        }
    }
    result.suffix = suffix;
    result.base = value.rfind("0b", 0) == 0 ? 2 : value.rfind("0o", 0) == 0 ? 8 : value.rfind("0x", 0) == 0 ? 16 : 10;
    result.digits = value;
    if (result.base != 10 && result.digits.size() >= 2) result.digits.erase(0, 2);
    return result;
}

std::string operatorText(const antlr4::ParserRuleContext* context,
                         const rxantlr::RxParser& parser) {
    if (!context) return {};
    const auto index = context->getRuleIndex();
    const auto& names = parser.getRuleNames();
    const auto name = static_cast<std::size_t>(index) < names.size()
        ? names[static_cast<std::size_t>(index)] : std::string{};
    if (name == "shiftRight") return ">>";
    if (name == "additiveOperator" || name == "multiplicativeOperator" ||
        name == "comparisonExceptLt" || name == "assignmentOperator" ||
        name == "equalsSign") {
        return const_cast<antlr4::ParserRuleContext*>(context)->getText();
    }
    return {};
}

BinaryOp binaryOp(const std::string& op) {
    if (op == "+") return BinaryOp::Add;
    if (op == "-") return BinaryOp::Subtract;
    if (op == "*") return BinaryOp::Multiply;
    if (op == "/") return BinaryOp::Divide;
    if (op == "%") return BinaryOp::Remainder;
    if (op == "<<") return BinaryOp::ShiftLeft;
    if (op == ">>") return BinaryOp::ShiftRight;
    if (op == "&") return BinaryOp::BitAnd;
    if (op == "^") return BinaryOp::BitXor;
    if (op == "|") return BinaryOp::BitOr;
    if (op == "==") return BinaryOp::Equal;
    if (op == "!=") return BinaryOp::NotEqual;
    if (op == "<") return BinaryOp::Less;
    if (op == "<=") return BinaryOp::LessEqual;
    if (op == ">") return BinaryOp::Greater;
    if (op == ">=") return BinaryOp::GreaterEqual;
    if (op == "&&") return BinaryOp::LogicalAnd;
    return BinaryOp::LogicalOr;
}

bool isBinaryToken(const std::string& op) {
    static const char* values[] = {"+", "-", "*", "/", "%", "<<", ">>", "&", "^", "|", "==", "!=", "<", "<=", ">", ">=", "&&", "||"};
    for (const auto* value : values) if (op == value) return true;
    return false;
}

bool isAssignmentToken(const std::string& op) {
    static const char* values[] = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="};
    for (const auto* value : values) if (op == value) return true;
    return false;
}

AssignmentOp assignmentOp(const std::string& op) {
    if (op == "+=") return AssignmentOp::Add;
    if (op == "-=") return AssignmentOp::Subtract;
    if (op == "*=") return AssignmentOp::Multiply;
    if (op == "/=") return AssignmentOp::Divide;
    if (op == "%=") return AssignmentOp::Remainder;
    if (op == "&=") return AssignmentOp::BitAnd;
    if (op == "|=") return AssignmentOp::BitOr;
    if (op == "^=") return AssignmentOp::BitXor;
    if (op == "<<=") return AssignmentOp::ShiftLeft;
    if (op == ">>=") return AssignmentOp::ShiftRight;
    return AssignmentOp::Assign;
}

} // namespace

AstBuilder::AstBuilder(const rxantlr::RxParser& parser) : parser_(parser) {}

std::string AstBuilder::ruleName(antlr4::ParserRuleContext* tree) const {
    if (!tree) return {};
    const auto index = tree->getRuleIndex();
    const auto& names = parser_.getRuleNames();
    return static_cast<std::size_t>(index) < names.size() ? names[static_cast<std::size_t>(index)] : std::string{};
}

void AstBuilder::requireCleanTree(antlr4::tree::ParseTree* tree) const {
    if (!tree) throw std::invalid_argument("null parse tree");
}

PathSegment AstBuilder::segment(antlr4::ParserRuleContext* tree) {
    PathSegment result; result.span = spanOf(tree);
    auto leaves = terminals(tree);
    for (auto* leaf : leaves) {
        const auto value = leaf->getText();
        if (value == "::" || value == "<" || value == ">" || value == ",") continue;
        if (value == "self") result.nameKind = PathNameKind::SelfValue;
        else if (value == "Self") result.nameKind = PathNameKind::SelfType;
        if (result.name.empty()) result.name = value;
    }
    for (auto* child : ruleChildren(tree)) {
        if (ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child)) != "genericArgs") continue;
        result.hasGenericArguments = true;
        auto* args = dynamic_cast<rxantlr::RxParser::GenericArgsContext*>(child);
        for (auto* argument : args->genericArg()) if (argument->typeRef()) result.typeArguments.push_back(type(argument->typeRef()));
    }
    return result;
}

Path AstBuilder::path(antlr4::ParserRuleContext* tree) {
    Path result; result.span = spanOf(tree);
    for (auto* child : ruleChildren(tree)) {
        const auto name = ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child));
        if (name == "pathExprSegment" || name == "pathIdentSegment" || name == "typePathSegment")
            result.segments.push_back(segment(dynamic_cast<antlr4::ParserRuleContext*>(child)));
        else if (name == "pathInExpression" || name == "typePath") {
            auto nested = path(dynamic_cast<antlr4::ParserRuleContext*>(child));
            result.segments.insert(result.segments.end(),
                                   std::make_move_iterator(nested.segments.begin()),
                                   std::make_move_iterator(nested.segments.end()));
        }
    }
    if (result.segments.empty()) {
        const auto leaves = terminals(tree);
        for (auto* leaf : leaves) {
            const auto value = leaf->getText();
            if (value != "::" && value != "<" && value != ">" && value != ",") {
                PathSegment item; item.span = spanOf(leaf); item.name = value;
                item.nameKind = value == "self" ? PathNameKind::SelfValue : value == "Self" ? PathNameKind::SelfType : PathNameKind::Identifier;
                result.segments.push_back(std::move(item));
            }
        }
    }
    return result;
}

Binding AstBuilder::binding(rxantlr::RxParser::IdentifierBindingContext* tree) {
    Binding result; result.span = spanOf(tree);
    auto leaves = terminals(tree);
    for (auto* leaf : leaves) {
        if (leaf->getText() == "mut") result.isMutable = true;
        else { result.name = leaf->getText(); result.nameSpan = spanOf(leaf); }
    }
    return result;
}

TypePtr AstBuilder::type(antlr4::ParserRuleContext* tree) {
    if (!tree) return nullptr;
    const auto name = ruleName(tree);
    if (name == "typeRef" || name == "genericArg") {
        auto children = ruleChildren(tree); if (children.size() == 1) return type(dynamic_cast<antlr4::ParserRuleContext*>(children.front()));
    }
    if (name == "referenceType") {
        bool mutableInner = false;
        bool doubleReference = false;
        for (auto* child : tree->children) if (auto* leaf = dynamic_cast<antlr4::tree::TerminalNode*>(child)) {
            mutableInner |= leaf->getText() == "mut";
            doubleReference |= leaf->getText() == "&&";
        }
        TypePtr pointee;
        for (auto* child : ruleChildren(tree)) if (ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child)) == "typeRef") pointee = type(dynamic_cast<antlr4::ParserRuleContext*>(child));
        auto inner = std::make_unique<TypeSyntax>(spanOf(tree), ReferenceType{mutableInner, std::move(pointee)});
        if (doubleReference) return std::make_unique<TypeSyntax>(spanOf(tree), ReferenceType{false, std::move(inner)});
        return inner;
    }
    if (name == "arrayType") {
        auto children = ruleChildren(tree); TypePtr element; ExprPtr length;
        for (auto* child : children) {
            const auto childName = ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child));
            if (childName == "typeRef") element = type(dynamic_cast<antlr4::ParserRuleContext*>(child));
            else if (childName == "constValue") length = constant(dynamic_cast<antlr4::ParserRuleContext*>(child));
        }
        return std::make_unique<TypeSyntax>(spanOf(tree), ArrayType{std::move(element), std::move(length)});
    }
    if (name == "typePath" || name == "typePathSegment") return std::make_unique<TypeSyntax>(spanOf(tree), PathType{path(tree)});
    const auto leaves = terminals(tree);
    if (leaves.size() == 2 && leaves[0]->getText() == "(" && leaves[1]->getText() == ")") return std::make_unique<TypeSyntax>(spanOf(tree), UnitType{});
    for (auto* child : ruleChildren(tree)) if (auto* context = dynamic_cast<antlr4::ParserRuleContext*>(child)) if (auto result = type(context)) return result;
    return std::make_unique<TypeSyntax>(spanOf(tree), PathType{path(tree)});
}

TypePtr AstBuilder::buildType(rxantlr::RxParser::TypeRefContext* tree) { return type(tree); }

ExprPtr AstBuilder::constant(antlr4::ParserRuleContext* tree) {
    if (!tree) return nullptr;
    const auto leaves = terminals(tree); if (leaves.size() == 1) {
        const auto value = leaves.front()->getText();
        if (value == "true" || value == "false") return std::make_unique<Expr>(spanOf(tree), BoolLiteral{value == "true"});
        if (isIntegerText(value)) return std::make_unique<Expr>(spanOf(tree), integerLiteral(value));
    }
    if (ruleName(tree) == "constValue" || ruleName(tree) == "magnitude") {
        for (auto* leaf : leaves) if (leaf->getText() == "-") {
            for (auto* child : ruleChildren(tree)) if (auto result = constant(dynamic_cast<antlr4::ParserRuleContext*>(child))) return std::make_unique<Expr>(spanOf(tree), UnaryExpr{UnaryOp::Negate, std::move(result)});
        }
        if (ruleName(tree) == "constValue" && !leaves.empty()) {
            for (auto* child : ruleChildren(tree)) if (ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child)) == "pathInExpression") return std::make_unique<Expr>(spanOf(tree), PathExpr{path(dynamic_cast<antlr4::ParserRuleContext*>(child))});
        }
    }
    for (auto* child : ruleChildren(tree)) if (auto result = constant(dynamic_cast<antlr4::ParserRuleContext*>(child))) return result;
    return expression(tree);
}

ExprPtr AstBuilder::primary(antlr4::ParserRuleContext* tree) {
    if (!tree) return nullptr;
    const auto name = ruleName(tree);
    const auto leaves = terminals(tree);
    if (name == "literalExpression" || name == "constValue" || name == "magnitude") {
        if (leaves.size() == 1) {
            const auto value = leaves.front()->getText();
            if (value == "true" || value == "false") return std::make_unique<Expr>(spanOf(tree), BoolLiteral{value == "true"});
            if (isIntegerText(value)) return std::make_unique<Expr>(spanOf(tree), integerLiteral(value));
        }
    }
    if (name == "pathInExpression" || name == "pathExprSegment") return std::make_unique<Expr>(spanOf(tree), PathExpr{path(tree)});
    if (name == "nonBlockPrimary" || name == "conditionPrimary" || name == "conditionPrimaryWithoutBareBlock") {
        for (auto* leaf : leaves) {
            if (leaf != leaves.front()) break;
            if (leaf->getText() == "return") {
                ExprPtr value;
                for (auto* child : ruleChildren(tree)) {
                    const auto childName = ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child));
                    if (childName == "expression" || childName == "conditionExpression") value = expression(dynamic_cast<antlr4::ParserRuleContext*>(child));
                }
                return std::make_unique<Expr>(spanOf(tree), ReturnExpr{std::move(value)});
            }
            if (leaf->getText() == "break") {
                ExprPtr value;
                for (auto* child : ruleChildren(tree)) {
                    const auto childName = ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child));
                    if (childName == "expression" || childName == "conditionBreakExpression") value = expression(dynamic_cast<antlr4::ParserRuleContext*>(child));
                }
                return std::make_unique<Expr>(spanOf(tree), BreakExpr{std::move(value)});
            }
            if (leaf->getText() == "continue") return std::make_unique<Expr>(spanOf(tree), ContinueExpr{});
        }
    }
    if (name == "nonBlockPrimary") {
        Path structPath;
        std::vector<FieldInitializer> fields;
        bool hasFields = std::any_of(tree->children.begin(), tree->children.end(), [](auto* child) {
            auto* terminal = dynamic_cast<antlr4::tree::TerminalNode*>(child);
            return terminal && terminal->getText() == "{";
        });
        for (auto* child : ruleChildren(tree)) {
            const auto childName = ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child));
            if (childName == "pathInExpression") structPath = path(dynamic_cast<antlr4::ParserRuleContext*>(child));
            else if (childName == "structExprFields") {
                hasFields = true;
                for (auto* fieldTree : ruleChildren(child)) {
                    if (ruleName(dynamic_cast<antlr4::ParserRuleContext*>(fieldTree)) != "structExprField") continue;
                    auto* field = dynamic_cast<rxantlr::RxParser::StructExprFieldContext*>(fieldTree);
                    FieldInitializer value; value.span = spanOf(field); value.nameSpan = spanOf(field->identifier()); value.name = field->identifier()->getText(); value.value = expression(field->expression());
                    fields.push_back(std::move(value));
                }
            }
        }
        if (hasFields) return std::make_unique<Expr>(spanOf(tree), StructExpr{std::move(structPath), std::move(fields)});
    }
    if (name == "arrayExpression") {
        auto children = ruleChildren(tree);
        bool repeated = false; for (auto* child : tree->children) if (auto* leaf = dynamic_cast<antlr4::tree::TerminalNode*>(child)) if (leaf->getText() == ";") repeated = true;
        if (repeated) {
            ExprPtr element; ExprPtr count;
            for (auto* child : children) {
                const auto childName = ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child));
                if (childName == "expression") element = expression(dynamic_cast<antlr4::ParserRuleContext*>(child));
                else if (childName == "constValue") count = constant(dynamic_cast<antlr4::ParserRuleContext*>(child));
            }
            return std::make_unique<Expr>(spanOf(tree), RepeatArrayExpr{std::move(element), std::move(count)});
        }
        ArrayExpr value;
        for (auto* child : children) if (ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child)) == "expression") value.elements.push_back(expression(dynamic_cast<antlr4::ParserRuleContext*>(child)));
        return std::make_unique<Expr>(spanOf(tree), std::move(value));
    }
    if (name == "primaryExpression") {
        for (auto* child : ruleChildren(tree)) if (auto result = expression(dynamic_cast<antlr4::ParserRuleContext*>(child))) return result;
    }
    if (name == "nonBlockPrimary") {
        auto children = ruleChildren(tree);
        if (children.size() == 1 && ruleName(dynamic_cast<antlr4::ParserRuleContext*>(children.front())) == "pathInExpression") return std::make_unique<Expr>(spanOf(tree), PathExpr{path(dynamic_cast<antlr4::ParserRuleContext*>(children.front()))});
        for (auto* child : children) if (ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child)) == "expression") return expression(dynamic_cast<antlr4::ParserRuleContext*>(child));
    }
    if (name == "blockExpression") return block(dynamic_cast<rxantlr::RxParser::BlockExpressionContext*>(tree));
    if (name == "ifExpression") {
        IfExpr value; auto children = ruleChildren(tree); std::size_t index = 0;
        for (auto* child : children) { const auto childName = ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child)); if (childName == "conditionExpression") value.condition = expression(dynamic_cast<antlr4::ParserRuleContext*>(child)); else if (childName == "blockExpression" || childName == "ifExpression") { auto branch = expression(dynamic_cast<antlr4::ParserRuleContext*>(child)); if (index++ == 0) value.thenBranch = std::move(branch); else value.elseBranch = std::move(branch); } }
        return std::make_unique<Expr>(spanOf(tree), std::move(value));
    }
    if (name == "conditionPrimaryWithoutBareBlock" && !leaves.empty() &&
        (leaves.front()->getText() == "loop" || leaves.front()->getText() == "while")) {
        ExprPtr condition, body;
        for (auto* child : ruleChildren(tree)) {
            const auto childName = ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child));
            if (childName == "blockExpression") body = block(dynamic_cast<rxantlr::RxParser::BlockExpressionContext*>(child));
            if (childName == "conditionExpression") condition = expression(dynamic_cast<antlr4::ParserRuleContext*>(child));
        }
        if (leaves.front()->getText() == "loop") return std::make_unique<Expr>(spanOf(tree), LoopExpr{std::move(body)});
        return std::make_unique<Expr>(spanOf(tree), WhileExpr{std::move(condition), std::move(body)});
    }
    if (name == "expressionWithBlock") {
        return expression(tree);
    }
    if (name == "nonBlockPrimary" || name == "conditionPrimary" || name == "conditionPrimaryWithoutBareBlock" || name == "primaryExpression") {
        for (auto* child : ruleChildren(tree)) if (auto result = primary(dynamic_cast<antlr4::ParserRuleContext*>(child))) return result;
        if (leaves.size() == 2 && leaves[0]->getText() == "(" && leaves[1]->getText() == ")") return std::make_unique<Expr>(spanOf(tree), UnitExpr{});
    }
    for (auto* child : ruleChildren(tree)) if (auto result = expression(dynamic_cast<antlr4::ParserRuleContext*>(child))) return result;
    return nullptr;
}

ExprPtr AstBuilder::unary(antlr4::ParserRuleContext* tree) {
    auto children = ruleChildren(tree); if (children.size() == 1) return expression(dynamic_cast<antlr4::ParserRuleContext*>(children.front()));
    std::string op; for (auto* leaf : terminals(tree)) { op = leaf->getText(); if (op == "-" || op == "!" || op == "*" || op == "&" || op == "&&") break; }
    ExprPtr operand = children.empty() ? nullptr : expression(dynamic_cast<antlr4::ParserRuleContext*>(children.back()));
    bool mutableBorrow = false;
    for (auto* child : children) if (isRule(child, "unaryOperator", parser_)) {
        for (auto* leaf : terminals(child)) if (leaf->getText() == "mut") mutableBorrow = true;
    }
    if (op == "&") return std::make_unique<Expr>(spanOf(tree), BorrowExpr{mutableBorrow, std::move(operand)});
    if (op == "&&") {
        auto inner = std::make_unique<Expr>(spanOf(tree), BorrowExpr{mutableBorrow, std::move(operand)});
        return std::make_unique<Expr>(spanOf(tree), BorrowExpr{false, std::move(inner)});
    }
    return std::make_unique<Expr>(spanOf(tree), UnaryExpr{op == "-" ? UnaryOp::Negate : op == "!" ? UnaryOp::Not : UnaryOp::Dereference, std::move(operand)});
}

ExprPtr AstBuilder::cast(antlr4::ParserRuleContext* tree) {
    auto children = ruleChildren(tree); if (children.empty()) return nullptr;
    ExprPtr result = expression(dynamic_cast<antlr4::ParserRuleContext*>(children.front()));
    for (std::size_t i = 1; i < children.size(); ++i) result = std::make_unique<Expr>(spanOf(tree), CastExpr{std::move(result), type(dynamic_cast<antlr4::ParserRuleContext*>(children[i]))});
    return result;
}

ExprPtr AstBuilder::fold(antlr4::ParserRuleContext* tree) {
    auto children = tree ? tree->children : std::vector<antlr4::tree::ParseTree*>{};
    std::vector<ExprPtr> operands; std::vector<std::string> ops;
    for (auto* child : children) {
        if (auto* leaf = dynamic_cast<antlr4::tree::TerminalNode*>(child)) {
            const auto value = leaf->getText(); if (isBinaryToken(value) || isAssignmentToken(value)) ops.push_back(value);
        } else if (auto* context = dynamic_cast<antlr4::ParserRuleContext*>(child)) {
            const auto op = operatorText(context, parser_);
            if (!op.empty()) { ops.push_back(op); continue; }
            if (auto operand = expression(context)) operands.push_back(std::move(operand));
        }
    }
    if (ops.empty()) return operands.empty() ? nullptr : std::move(operands.front());
    if (isAssignmentToken(ops.front())) {
        ExprPtr rhs = operands.size() > 1 ? std::move(operands.back()) : nullptr;
        if (ops.size() > 1 && operands.size() > 2) { for (std::size_t i = ops.size() - 1; i-- > 0;) rhs = std::make_unique<Expr>(spanOf(tree), AssignmentExpr{assignmentOp(ops[i]), std::move(operands[i + 1]), std::move(rhs)}); }
        return std::make_unique<Expr>(spanOf(tree), AssignmentExpr{assignmentOp(ops.front()), std::move(operands.front()), std::move(rhs)});
    }
    ExprPtr result = std::move(operands.front());
    for (std::size_t i = 0; i < ops.size() && i + 1 < operands.size(); ++i) result = std::make_unique<Expr>(spanOf(tree), BinaryExpr{binaryOp(ops[i]), std::move(result), std::move(operands[i + 1])});
    return result;
}

ExprPtr AstBuilder::postfix(antlr4::ParserRuleContext* tree) {
    auto children = ruleChildren(tree); if (children.empty()) return nullptr;
    ExprPtr result = expression(dynamic_cast<antlr4::ParserRuleContext*>(children.front()));
    for (std::size_t i = 1; i < children.size(); ++i) result = suffix(std::move(result), dynamic_cast<antlr4::ParserRuleContext*>(children[i]));
    return result;
}

ExprPtr AstBuilder::suffix(ExprPtr base, antlr4::ParserRuleContext* tree) {
    if (!tree) return base;
    const auto name = ruleName(tree);
    if (name == "postfixSuffix") {
        for (auto* child : ruleChildren(tree)) {
            if (isRule(child, "callArguments", parser_) || isRule(child, "dotSuffix", parser_))
                return suffix(std::move(base), dynamic_cast<antlr4::ParserRuleContext*>(child));
        }
    }
    if (name == "callArguments") return std::make_unique<Expr>(spanOf(tree), CallExpr{std::move(base), arguments(dynamic_cast<rxantlr::RxParser::CallArgumentsContext*>(tree))});
    const auto leaves = terminals(tree);
    if (name == "dotSuffix") {
        std::string member; PathSegment method;
        for (auto* child : ruleChildren(tree)) {
            if (isRule(child, "pathExprSegment", parser_)) { method = segment(dynamic_cast<antlr4::ParserRuleContext*>(child)); member = method.name; }
            else if (isRule(child, "identifier", parser_)) member = child->getText();
        }
        bool call = std::any_of(tree->children.begin(), tree->children.end(), [this](auto* child) { return isRule(child, "callArguments", parser_); });
        if (call) {
            std::vector<ExprPtr> args;
            for (auto* child : ruleChildren(tree)) if (isRule(child, "callArguments", parser_)) args = arguments(dynamic_cast<rxantlr::RxParser::CallArgumentsContext*>(child));
            method.name = member;
            return std::make_unique<Expr>(spanOf(tree), MethodCallExpr{std::move(base), std::move(method), std::move(args)});
        }
        return std::make_unique<Expr>(spanOf(tree), FieldExpr{std::move(base), member, spanOf(firstTerminal(tree))});
    }
    if (!leaves.empty() && leaves.front()->getText() == "[") {
        auto children = ruleChildren(tree); ExprPtr index = children.empty() ? nullptr : expression(dynamic_cast<antlr4::ParserRuleContext*>(children.front()));
        return std::make_unique<Expr>(spanOf(tree), IndexExpr{std::move(base), std::move(index)});
    }
    return base;
}

std::vector<ExprPtr> AstBuilder::arguments(rxantlr::RxParser::CallArgumentsContext* tree) {
    std::vector<ExprPtr> result; if (!tree) return result;
    for (auto* child : ruleChildren(tree)) if (isRule(child, "expression", parser_)) result.push_back(expression(dynamic_cast<antlr4::ParserRuleContext*>(child)));
    return result;
}

ExprPtr AstBuilder::block(rxantlr::RxParser::BlockExpressionContext* tree) {
    BlockExpr value; if (!tree) return nullptr;
    for (auto* child : ruleChildren(tree)) {
        const auto name = ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child));
        if (name == "statement") value.statements.push_back(statement(dynamic_cast<rxantlr::RxParser::StatementContext*>(child)));
        else if (name == "statementExpression") value.tail = expression(dynamic_cast<antlr4::ParserRuleContext*>(child));
    }
    if (!value.tail && !value.statements.empty()) {
        auto& last = value.statements.back(); if (last && std::holds_alternative<ExprStmt>(last->data) && !std::get<ExprStmt>(last->data).hasSemicolon) { value.tail = std::move(std::get<ExprStmt>(last->data).expression); value.statements.pop_back(); }
    }
    return std::make_unique<Expr>(spanOf(tree), std::move(value));
}

StatementPtr AstBuilder::let(rxantlr::RxParser::LetStatementContext* tree) {
    if (!tree) return nullptr;
    Binding name = binding(tree->identifierBinding());
    TypePtr annotation = tree->typeRef() ? type(tree->typeRef()) : nullptr;
    ExprPtr init = expression(tree->expression());
    return std::make_unique<Statement>(spanOf(tree), LetStmt{std::move(name), std::move(annotation), std::move(init)});
}
StatementPtr AstBuilder::buildLet(rxantlr::RxParser::LetStatementContext* tree) { return let(tree); }

StatementPtr AstBuilder::statement(rxantlr::RxParser::StatementContext* tree) {
    if (!tree) return nullptr;
    if (tree->letStatement()) return let(tree->letStatement());
    for (auto* child : ruleChildren(tree)) {
        const auto name = ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child));
        if (name == "statementExpression" || name == "expressionWithBlock") {
            bool semicolon = false;
            for (auto* node : tree->children) if (auto* leaf = dynamic_cast<antlr4::tree::TerminalNode*>(node)) semicolon |= leaf->getText() == ";";
            return std::make_unique<Statement>(spanOf(tree), ExprStmt{expression(dynamic_cast<antlr4::ParserRuleContext*>(child)), semicolon});
        }
    }
    return std::make_unique<Statement>(spanOf(tree), EmptyStmt{});
}

ExprPtr AstBuilder::expression(antlr4::ParserRuleContext* tree) {
    if (!tree) return nullptr;
    const auto name = ruleName(tree);
    if (name == "expression" || name == "conditionExpression" || name == "conditionBreakExpression" || name == "statementExpression") { auto children = ruleChildren(tree); return children.empty() ? nullptr : expression(dynamic_cast<antlr4::ParserRuleContext*>(children.front())); }
    if (name == "expressionWithBlock") {
        auto* context = dynamic_cast<rxantlr::RxParser::ExpressionWithBlockContext*>(tree);
        if (context->LOOP()) return std::make_unique<Expr>(spanOf(tree), LoopExpr{block(context->blockExpression())});
        if (context->WHILE()) return std::make_unique<Expr>(spanOf(tree), WhileExpr{expression(context->conditionExpression()), block(context->blockExpression())});
        if (context->blockExpression()) return block(context->blockExpression());
        if (context->ifExpression()) return primary(context->ifExpression());
        auto children = ruleChildren(tree);
        for (auto* child : children) {
            const auto childName = ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child));
            if (childName == "conditionExpression") return expression(dynamic_cast<antlr4::ParserRuleContext*>(child));
            if (childName == "blockExpression") return block(dynamic_cast<rxantlr::RxParser::BlockExpressionContext*>(child));
        }
    }
    if (name == "postfixExpression" || name == "conditionPostfixExpression" || name == "conditionBreakPostfixExpression" || name == "statementPostfixExpression") return postfix(tree);
    if (name == "unaryExpression" || name == "conditionUnaryExpression" || name == "conditionBreakUnaryExpression" || name == "statementUnaryExpression") return unary(tree);
    if (name == "castExpression" || name == "conditionCastExpression" || name == "conditionBreakCastExpression" || name == "statementCastExpression") return cast(tree);
    if (name == "primaryExpression" || name == "nonBlockPrimary" || name == "conditionPrimary" || name == "conditionPrimaryWithoutBareBlock" || name == "literalExpression" || name == "arrayExpression" || name == "pathInExpression" || name == "blockExpression" || name == "ifExpression") return primary(tree);
    if (name == "assignmentExpression" || name.find("Expression") != std::string::npos) return fold(tree);
    for (auto* child : ruleChildren(tree)) if (auto result = expression(dynamic_cast<antlr4::ParserRuleContext*>(child))) return result;
    return nullptr;
}
ExprPtr AstBuilder::buildExpression(antlr4::ParserRuleContext* tree) { return expression(tree); }

ItemPtr AstBuilder::item(antlr4::ParserRuleContext* tree) {
    if (!tree) return nullptr;
    for (auto* child : ruleChildren(tree)) {
        const auto name = ruleName(dynamic_cast<antlr4::ParserRuleContext*>(child));
        if (name == "functionDefinition") {
            auto* fn = dynamic_cast<rxantlr::RxParser::FunctionDefinitionContext*>(child);
            FunctionDecl value;
            if (fn->identifier()) { value.name = fn->identifier()->getText(); value.nameSpan = spanOf(fn->identifier()); }
            value.body = block(fn->blockExpression());
            value.returnType = fn->typeRef() ? type(fn->typeRef()) : std::make_unique<TypeSyntax>(spanOf(fn), UnitType{});
            value.hasExplicitReturnType = fn->typeRef() != nullptr;
            value.hasLifetimeParameters = fn->genericParams() != nullptr;
            if (fn->functionParameters()) for (auto* parameterTree : ruleChildren(fn->functionParameters())) {
                auto* parameter = dynamic_cast<antlr4::ParserRuleContext*>(parameterTree);
                if (ruleName(parameter) == "functionParam") {
                    auto* ordinary = dynamic_cast<rxantlr::RxParser::FunctionParamContext*>(parameter);
                    Parameter parameterValue; parameterValue.span = spanOf(parameter);
                    parameterValue.type = type(ordinary->typeRef());
                    parameterValue.binding = binding(ordinary->identifierBinding());
                    value.parameters.push_back(std::move(parameterValue));
                } else if (ruleName(parameter) == "selfParam") {
                    auto* receiver = dynamic_cast<rxantlr::RxParser::SelfParamContext*>(parameter);
                    Receiver self; self.span = spanOf(parameter);
                    bool reference = false, mutableReceiver = false;
                    for (auto* leaf : terminals(receiver)) {
                        reference |= leaf->getText() == "&";
                        mutableReceiver |= leaf->getText() == "mut";
                    }
                    self.kind = reference ? (mutableReceiver ? ReceiverKind::MutableReference : ReceiverKind::SharedReference)
                                          : (mutableReceiver ? ReceiverKind::MutableValue : ReceiverKind::Value);
                    value.receiver = self;
                }
            }
            return std::make_unique<Item>(spanOf(tree), std::move(value));
        }
        if (name == "structDefinition") {
            auto* st = dynamic_cast<rxantlr::RxParser::StructDefinitionContext*>(child);
            StructDecl value;
            if (st->identifier()) { value.name = st->identifier()->getText(); value.nameSpan = spanOf(st->identifier()); }
            for (auto* fieldTree : st->structField()) {
                FieldDecl field; field.span = spanOf(fieldTree); field.name = fieldTree->identifier()->getText();
                field.nameSpan = spanOf(fieldTree->identifier()); field.type = type(fieldTree->typeRef());
                value.fields.push_back(std::move(field));
            }
            for (auto* attribute : st->outerAttribute()) for (auto* derive : attribute->deriveName()) {
                const auto name = derive->getText();
                DeriveTrait trait = name == "Copy" ? DeriveTrait::Copy : name == "Clone" ? DeriveTrait::Clone : name == "PartialEq" ? DeriveTrait::PartialEq : DeriveTrait::Eq;
                value.derives.push_back(DeriveRequest{spanOf(derive), trait});
            }
            return std::make_unique<Item>(spanOf(tree), std::move(value));
        }
        if (name == "constantItem") {
            auto* constantTree = dynamic_cast<rxantlr::RxParser::ConstantItemContext*>(child);
            ConstDecl value; value.name = constantTree->identifier()->getText(); value.nameSpan = spanOf(constantTree->identifier());
            value.type = type(constantTree->typeRef()); value.value = constant(constantTree->constValue());
            return std::make_unique<Item>(spanOf(tree), std::move(value));
        }
        if (name == "inherentImpl") {
            auto* implTree = dynamic_cast<rxantlr::RxParser::InherentImplContext*>(child);
            ImplDecl value; value.target = type(implTree->typeRef());
            for (auto* associated : implTree->associatedItem()) value.items.push_back(item(associated));
            return std::make_unique<Item>(spanOf(tree), std::move(value));
        }
    }
    return nullptr;
}
ItemPtr AstBuilder::buildItem(rxantlr::RxParser::ItemContext* tree) { return item(tree); }

Ast AstBuilder::build(rxantlr::RxParser::CrateContext* tree) {
    requireCleanTree(tree); auto result = std::make_unique<Program>(); result->span = spanOf(tree);
    for (auto* itemContext : tree->item()) if (auto value = item(itemContext)) result->items.push_back(std::move(value));
    return result;
}

std::any AstBuilder::visitCrate(rxantlr::RxParser::CrateContext* tree) {
    auto result = build(tree); return std::shared_ptr<Program>(result.release());
}

namespace {
class ErrorCollector final : public antlr4::BaseErrorListener {
public:
    std::vector<Diagnostic> diagnostics;
    void syntaxError(antlr4::Recognizer*, antlr4::Token* offending, std::size_t line, std::size_t column,
                     const std::string& message, std::exception_ptr) override {
        Diagnostic diagnostic; diagnostic.phase = DiagnosticPhase::Syntax; diagnostic.span.begin.line = line; diagnostic.span.begin.column = column; diagnostic.span.end = diagnostic.span.begin; if (offending) { diagnostic.span.begin.offset = offending->getStartIndex() == INVALID_INDEX ? 0 : static_cast<std::size_t>(offending->getStartIndex()); diagnostic.span.end.offset = offending->getStopIndex() == INVALID_INDEX ? diagnostic.span.begin.offset : static_cast<std::size_t>(offending->getStopIndex() + 1); } diagnostic.message = message; diagnostics.push_back(std::move(diagnostic));
    }
};
}

ParseResult parseSource(const std::string& source, std::string sourceName) {
    ParseResult result;
    for (unsigned char byte : source) if (byte > 0x7f) { result.diagnostics.push_back({DiagnosticPhase::Lexical, {}, "Rx source must contain only 7-bit ASCII"}); return result; }
    antlr4::ANTLRInputStream input(source); rxantlr::RxLexer lexer(&input); ErrorCollector lexErrors; lexer.removeErrorListeners(); lexer.addErrorListener(&lexErrors);
    antlr4::CommonTokenStream tokens(&lexer); tokens.fill();
    for (auto* token : tokens.getTokens()) if (token->getType() == rxantlr::RxLexer::INVALID_LIFETIME || token->getType() == rxantlr::RxLexer::INVALID_CHARACTER_LITERAL || token->getType() == rxantlr::RxLexer::INVALID_NUMBER || token->getType() == rxantlr::RxLexer::UNTERMINATED_BLOCK_COMMENT || token->getType() == rxantlr::RxLexer::ERROR_CHAR) result.diagnostics.push_back({DiagnosticPhase::Lexical, spanOfToken(token), "invalid lexical token: " + token->getText()});
    if (!lexErrors.diagnostics.empty() || !result.diagnostics.empty()) { result.diagnostics.insert(result.diagnostics.end(), lexErrors.diagnostics.begin(), lexErrors.diagnostics.end()); return result; }
    tokens.seek(0); rxantlr::RxParser parser(&tokens); ErrorCollector parseErrors; parser.removeErrorListeners(); parser.addErrorListener(&parseErrors); auto* tree = parser.crate();
    if (!parseErrors.diagnostics.empty() || parser.getNumberOfSyntaxErrors() != 0) { result.diagnostics = std::move(parseErrors.diagnostics); return result; }
    AstBuilder builder(parser); result.program = builder.build(tree); result.program->sourceName = std::move(sourceName); return result;
}

} // namespace rxast
