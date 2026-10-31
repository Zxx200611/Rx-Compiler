#include "Semantic.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>

namespace rxast {
namespace {

enum class Kind { Unit, Never, I32, U32, Isize, Usize, Bool, Struct, Ref, Array, Box, Vec };
struct Type;
using Ty = std::shared_ptr<const Type>;
struct Type { Kind kind; std::string name; Ty element; bool mut = false; std::uint64_t length = 0; };
Ty make(Kind k, Ty e = {}, bool m = false, std::uint64_t n = 0, std::string name = {}) {
    return std::make_shared<Type>(Type{k, std::move(name), std::move(e), m, n});
}
bool same(Ty a, Ty b) {
    if (!a || !b) return !a && !b;
    return a->kind == b->kind && a->name == b->name && a->mut == b->mut &&
           a->length == b->length && same(a->element, b->element);
}
bool integer(Ty t) { return t && t->kind >= Kind::I32 && t->kind <= Kind::Usize; }
bool scalar(Ty t) { return integer(t) || (t && t->kind == Kind::Bool); }
bool signedInteger(Ty t) { return t && (t->kind == Kind::I32 || t->kind == Kind::Isize); }
bool indirect(Ty t) { return t && (t->kind == Kind::Ref || t->kind == Kind::Box); }
std::string describe(Ty t) {
    if (!t) return "unknown";
    switch (t->kind) {
    case Kind::Unit: return "()"; case Kind::Never: return "!";
    case Kind::I32: return "i32"; case Kind::U32: return "u32";
    case Kind::Isize: return "isize"; case Kind::Usize: return "usize";
    case Kind::Bool: return "bool"; case Kind::Struct: return t->name;
    case Kind::Ref: return std::string(t->mut ? "&mut " : "&") + describe(t->element);
    case Kind::Array: return "[" + describe(t->element) + "; " + std::to_string(t->length) + "]";
    case Kind::Box: return "Box<" + describe(t->element) + ">";
    case Kind::Vec: return "Vec<" + describe(t->element) + ">";
    }
    return "unknown";
}
bool coercible(Ty from, Ty to) {
    if (same(from, to) || from->kind == Kind::Never) return true;
    if (from->kind != Kind::Ref || to->kind != Kind::Ref) return false;
    bool mutableAccess = from->mut;
    Ty cur = from->element;
    for (;;) {
        if (same(cur, to->element) && (!to->mut || mutableAccess)) return true;
        if (!indirect(cur)) return false;
        if (cur->kind == Kind::Ref && !cur->mut) mutableAccess = false;
        cur = cur->element;
    }
}
struct Value {
    Ty type;
    bool place = false;
    bool mutablePlace = true;
    // Shared access and immutable Vec indexing restrict subsequent projections,
    // even when the projected value contains a mutable reference.
    bool restricted = false;
};
struct Failure : std::runtime_error { SourceSpan span; Failure(SourceSpan s, std::string m) : std::runtime_error(std::move(m)), span(s) {} };
struct Function { const FunctionDecl* decl = nullptr; std::string owner; std::vector<Ty> params; Ty result; Ty receiver; };
struct Constant { const ConstDecl* decl; std::string owner; Ty type; std::uint64_t value = 0; int state = 0; };
struct Structure { const StructDecl* decl; std::map<std::string, Ty> fields; std::set<DeriveTrait> derives; };
struct Loop { bool isWhile; Ty expected; Ty result; };

class Checker {
    const Program& program;
    std::map<std::string, Structure> structs;
    std::map<std::string, Function> functions;
    std::map<std::string, Constant> constants;
    std::vector<std::map<std::string, Value>> scopes;
    std::vector<Loop> loops;
    std::string self;
    Ty returnType;
    Ty unit = make(Kind::Unit), never = make(Kind::Never), boolean = make(Kind::Bool), usize = make(Kind::Usize);

    [[noreturn]] void fail(SourceSpan s, std::string message) { throw Failure(s, std::move(message)); }
    void require(bool ok, SourceSpan s, const std::string& m) { if (!ok) fail(s, m); }
    void match(Ty from, Ty to, SourceSpan s) {
        require(coercible(from, to), s, "expected " + describe(to) + ", found " + describe(from));
    }
    Value* local(const std::string& name) {
        for (auto i = scopes.rbegin(); i != scopes.rend(); ++i) {
            auto f = i->find(name); if (f != i->end()) return &f->second;
        }
        return nullptr;
    }
    std::string key(const std::string& owner, const std::string& name) { return owner.empty() ? name : owner + "::" + name; }
    Ty literalType(const IntegerLiteral& l, Ty expected = {}) {
        switch (l.suffix) {
        case IntegerSuffix::I32: return make(Kind::I32); case IntegerSuffix::U32: return make(Kind::U32);
        case IntegerSuffix::Isize: return make(Kind::Isize); case IntegerSuffix::Usize: return usize;
        case IntegerSuffix::None: return integer(expected) ? expected : make(Kind::I32);
        }
        return make(Kind::I32);
    }
    std::uint64_t magnitude(const IntegerLiteral& l) {
        std::uint64_t n = 0;
        for (char c : l.digits) {
            if (c == '_') continue;
            unsigned d = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' ? c - 'a' + 10 : c - 'A' + 10);
            n = n * l.base + d;
        }
        return n;
    }
    Ty segmentType(const PathSegment& seg) {
        const auto& n = seg.name;
        if (n == "Box" || n == "Vec") {
            require(seg.typeArguments.size() == 1, seg.span, n + " requires one concrete type argument");
            return make(n == "Box" ? Kind::Box : Kind::Vec, type(seg.typeArguments[0].get()));
        }
        require(seg.typeArguments.empty(), seg.span, "type arguments are not allowed here");
        if (n == "i32" || n == "u32" || n == "isize" || n == "usize" || n == "bool")
            require(!seg.hasGenericArguments, seg.span, "primitive types do not accept generic arguments");
        if (n == "i32") return make(Kind::I32);
        if (n == "u32") return make(Kind::U32);
        if (n == "isize") return make(Kind::Isize);
        if (n == "usize") return usize;
        if (n == "bool") return boolean;
        if (n == "Self") {
            require(!self.empty(), seg.span, "Self outside a struct or impl");
            return make(Kind::Struct, {}, false, 0, self);
        }
        require(structs.count(n), seg.span, "unknown type " + n);
        return make(Kind::Struct, {}, false, 0, n);
    }
    Ty pathType(const Path& p) {
        require(p.segments.size() == 1, p.span, "invalid type path");
        return segmentType(p.segments[0]);
    }
    Ty type(const TypeSyntax* syntax) {
        require(syntax != nullptr, {}, "missing type");
        if (std::holds_alternative<UnitType>(syntax->data)) return unit;
        if (auto p = std::get_if<PathType>(&syntax->data)) return pathType(p->path);
        if (auto r = std::get_if<ReferenceType>(&syntax->data)) return make(Kind::Ref, type(r->pointee.get()), r->isMutable);
        auto& a = std::get<ArrayType>(syntax->data);
        auto length = constantExpr(a.length.get(), usize);
        match(length.first, usize, syntax->span);
        return make(Kind::Array, type(a.element.get()), false, length.second);
    }
    std::string valuePath(const Path& p) {
        require(!p.segments.empty() && p.segments.size() <= 2, p.span, "invalid value path");
        for (auto& s : p.segments) require(s.typeArguments.empty(), s.span, "value has no type parameters");
        if (p.segments.size() == 1) return p.segments[0].name;
        auto owner = segmentType(p.segments[0]);
        require(owner->kind == Kind::Struct, p.span, "invalid associated constant owner");
        return key(owner->name, p.segments[1].name);
    }
    std::pair<Ty, std::uint64_t> constantExpr(const Expr* e, Ty expected = {}) {
        require(e != nullptr, {}, "missing constant expression");
        if (auto l = std::get_if<IntegerLiteral>(&e->data)) return {literalType(*l, expected), magnitude(*l)};
        if (auto b = std::get_if<BoolLiteral>(&e->data)) return {boolean, b->value};
        if (auto p = std::get_if<PathExpr>(&e->data)) {
            std::string name = valuePath(p->path);
            auto f = constants.find(name);
            require(f != constants.end(), e->span, "constant path must name a constant");
            return evalConstant(f->second);
        }
        if (auto u = std::get_if<UnaryExpr>(&e->data)) {
            auto v = constantExpr(u->operand.get());
            require(u->op == UnaryOp::Negate && signedInteger(v.first), e->span, "constant negation requires a signed integer");
            return {v.first, static_cast<std::uint32_t>(-v.second)};
        }
        fail(e->span, "not a constant expression");
    }
    std::pair<Ty, std::uint64_t> evalConstant(Constant& c) {
        require(c.state != 1, c.decl->nameSpan, "constant dependency cycle");
        if (c.state == 2) return {c.type, c.value};
        c.state = 1;
        auto previous = self; self = c.owner;
        c.type = type(c.decl->type.get());
        require(scalar(c.type), c.decl->nameSpan, "constants must be scalar");
        auto value = constantExpr(c.decl->value.get(), c.type);
        match(value.first, c.type, c.decl->nameSpan);
        c.value = value.second; c.state = 2; self = previous;
        return {c.type, c.value};
    }
    bool trait(Ty t, DeriveTrait tr, std::set<std::string>& visiting) {
        if (scalar(t) || t->kind == Kind::Unit || t->kind == Kind::Never) return true;
        if (t->kind == Kind::Ref) {
            if (tr == DeriveTrait::Copy || tr == DeriveTrait::Clone) return !t->mut;
            return trait(t->element, tr, visiting);
        }
        if (t->kind == Kind::Array) return trait(t->element, tr, visiting);
        if (t->kind == Kind::Box || t->kind == Kind::Vec)
            return tr != DeriveTrait::Copy && trait(t->element, tr, visiting);
        auto& s = structs.at(t->name);
        if (!s.derives.count(tr)) return false;
        auto token = t->name + ":" + std::to_string(static_cast<int>(tr));
        if (!visiting.insert(token).second) return true;
        for (auto& f : s.fields) if (!trait(f.second, tr, visiting)) { visiting.erase(token); return false; }
        visiting.erase(token); return true;
    }
    bool trait(Ty t, DeriveTrait tr) { std::set<std::string> visiting; return trait(t, tr, visiting); }
    void layout(Ty t, std::set<std::string>& stack, std::set<std::string>& done, SourceSpan span) {
        if (t->kind == Kind::Array) { layout(t->element, stack, done, span); return; }
        if (t->kind != Kind::Struct || done.count(t->name)) return;
        require(stack.insert(t->name).second, span, "infinite inline struct layout");
        for (auto& f : structs.at(t->name).fields) layout(f.second, stack, done, span);
        stack.erase(t->name); done.insert(t->name);
    }
    void addItems(const std::vector<ItemPtr>& items, const std::string& owner = {}) {
        for (auto& i : items) {
            if (auto s = std::get_if<StructDecl>(&i->data)) {
                require(owner.empty(), i->span, "nested struct");
                const std::set<std::string> protectedNames{"i32","u32","isize","usize","bool","Box","Vec","Copy","Clone","PartialEq","Eq"};
                require(!protectedNames.count(s->name), s->nameSpan, "protected type name");
                require(!structs.count(s->name), s->nameSpan, "duplicate struct");
                structs.emplace(s->name, Structure{s, {}, {}});
            }
        }
        for (auto& i : items) {
            if (auto f = std::get_if<FunctionDecl>(&i->data)) {
                require(!owner.empty() || (f->name != "get_i32" && f->name != "print_i32" && f->name != "println_i32"), f->nameSpan, "protected builtin value name");
                auto name = key(owner, f->name);
                require(!functions.count(name) && !constants.count(name), f->nameSpan, "duplicate value " + name);
                require(!owner.empty() || !f->receiver, f->nameSpan, "top-level function cannot have a receiver");
                functions.emplace(name, Function{f, owner, {}, {}, {}});
            } else if (auto c = std::get_if<ConstDecl>(&i->data)) {
                require(!owner.empty() || (c->name != "get_i32" && c->name != "print_i32" && c->name != "println_i32"), c->nameSpan, "protected builtin value name");
                auto name = key(owner, c->name);
                require(!functions.count(name) && !constants.count(name), c->nameSpan, "duplicate value " + name);
                constants.emplace(name, Constant{c, owner, {}});
            }
        }
    }
    void declarations() {
        addItems(program.items);
        for (auto& i : program.items) if (auto impl = std::get_if<ImplDecl>(&i->data)) {
            self.clear(); auto target = type(impl->target.get());
            require(target->kind == Kind::Struct, i->span, "impl target must be a user struct");
            addItems(impl->items, target->name);
        }
        for (auto& c : constants) evalConstant(c.second);
        for (auto& entry : structs) {
            self = entry.first; auto& s = entry.second;
            for (auto& f : s.decl->fields) {
                require(!s.fields.count(f.name), f.nameSpan, "duplicate field");
                s.fields.emplace(f.name, type(f.type.get()));
            }
            for (auto& d : s.decl->derives)
                require(s.derives.insert(d.trait).second, d.span, "duplicate derive");
            require(!s.derives.count(DeriveTrait::Copy) || s.derives.count(DeriveTrait::Clone), s.decl->nameSpan, "Copy requires explicit Clone");
            require(!s.derives.count(DeriveTrait::Eq) || s.derives.count(DeriveTrait::PartialEq), s.decl->nameSpan, "Eq requires explicit PartialEq");
        }
        std::set<std::string> stack, done;
        for (auto& s : structs) {
            auto ty = make(Kind::Struct, {}, false, 0, s.first);
            layout(ty, stack, done, s.second.decl->nameSpan);
            for (auto tr : s.second.derives) require(trait(ty, tr), s.second.decl->nameSpan, "field does not support requested derive");
        }
        for (auto& entry : functions) {
            auto& f = entry.second; self = f.owner;
            std::set<std::string> names;
            if (f.decl->receiver) {
                auto k = f.decl->receiver->kind;
                auto s = make(Kind::Struct, {}, false, 0, self);
                f.receiver = (k == ReceiverKind::SharedReference || k == ReceiverKind::MutableReference) ? make(Kind::Ref, s, k == ReceiverKind::MutableReference) : s;
                names.insert("self");
            }
            for (auto& p : f.decl->parameters) {
                require(names.insert(p.binding.name).second, p.binding.nameSpan, "duplicate parameter");
                f.params.push_back(type(p.type.get()));
            }
            f.result = type(f.decl->returnType.get());
        }
        self.clear();
        require(functions.count("main"), program.span, "missing top-level main");
        auto& main = functions.at("main");
        require(main.params.empty() && !main.decl->hasLifetimeParameters && same(main.result, unit), main.decl->nameSpan, "main must have no parameters or generics and return unit");
    }
    Value deref(Value v, SourceSpan span) {
        require(indirect(v.type), span, "type cannot be dereferenced");
        if (v.type->kind == Kind::Ref) {
            v.restricted = v.restricted || !v.type->mut;
            v.mutablePlace = v.type->mut && !v.restricted;
        } else {
            v.mutablePlace = (!v.place || v.mutablePlace) && !v.restricted;
        }
        v.type = v.type->element; v.place = true; return v;
    }
    void mutableAccess(const Value& v, SourceSpan span) {
        require((!v.place || v.mutablePlace) && !v.restricted, span, "mutable access requires a mutable place");
    }
    Ty lub(Ty a, Ty b, SourceSpan span) {
        if (!a || a->kind == Kind::Never) return b;
        if (b->kind == Kind::Never || coercible(b, a)) return a;
        if (coercible(a, b)) return b;
        fail(span, "incompatible result types");
    }
    Ty arithmeticType(Ty t) {
        if (t->kind == Kind::Ref && !t->mut && scalar(t->element)) return t->element;
        return t;
    }
    Ty binary(BinaryOp op, Ty left, Ty right, SourceSpan span) {
        if (op == BinaryOp::LogicalAnd || op == BinaryOp::LogicalOr) {
            require(same(left, boolean) && same(right, boolean), span, "logical operands must be bool"); return boolean;
        }
        if (op == BinaryOp::Equal || op == BinaryOp::NotEqual) {
            require(same(left, right) && trait(left, DeriveTrait::PartialEq), span, "equality requires matching PartialEq types"); return boolean;
        }
        if (op >= BinaryOp::Less && op <= BinaryOp::GreaterEqual) {
            if (left->kind == Kind::Ref && !left->mut && right->kind == Kind::Ref && right->mut && same(left->element, right->element)) right = make(Kind::Ref, right->element);
            require(same(left, right), span, "ordering operands must match");
            while (left->kind == Kind::Ref) left = left->element;
            require(scalar(left), span, "ordering requires scalars"); return boolean;
        }
        left = arithmeticType(left); right = arithmeticType(right);
        if (op == BinaryOp::ShiftLeft || op == BinaryOp::ShiftRight) {
            require(integer(left) && integer(right), span, "shift operands must be integers"); return left;
        }
        bool bit = op == BinaryOp::BitAnd || op == BinaryOp::BitOr || op == BinaryOp::BitXor;
        require(same(left, right) && (integer(left) || (bit && same(left, boolean))), span, "invalid binary operand types");
        return left;
    }
    Value callFunction(Function& f, const std::vector<ExprPtr>& args, SourceSpan span, bool explicitReceiver = true) {
        std::vector<Ty> params;
        if (explicitReceiver && f.receiver) params.push_back(f.receiver);
        params.insert(params.end(), f.params.begin(), f.params.end());
        require(args.size() == params.size(), span, "wrong argument count");
        for (std::size_t i = 0; i < args.size(); ++i) expr(args[i].get(), params[i]);
        return {f.result};
    }
    struct Builtin { Ty receiver; std::vector<Ty> params; Ty result; };
    std::optional<Builtin> builtin(Ty owner, const std::string& name) {
        if (name == "clone" && trait(owner, DeriveTrait::Clone)) return Builtin{make(Kind::Ref, owner), {}, owner};
        if (owner->kind == Kind::Array && name == "len") return Builtin{make(Kind::Ref, owner), {}, usize};
        if (owner->kind == Kind::Vec) {
            if (name == "len") return Builtin{make(Kind::Ref, owner), {}, usize};
            if (name == "is_empty") return Builtin{make(Kind::Ref, owner), {}, boolean};
            if (name == "push") return Builtin{make(Kind::Ref, owner, true), {owner->element}, unit};
            if (name == "remove") return Builtin{make(Kind::Ref, owner, true), {usize}, owner->element};
        }
        return {};
    }
    Value call(const CallExpr& c, SourceSpan span) {
        auto p = c.callee ? std::get_if<PathExpr>(&c.callee->data) : nullptr;
        require(p != nullptr, span, "callee is not a function path");
        auto& path = p->path;
        require(!path.segments.empty() && path.segments.size() <= 2, span, "invalid call path");
        auto& last = path.segments.back();
        require(last.typeArguments.empty(), last.span, "function has no type parameters");
        if (path.segments.size() == 1) {
            require(!local(last.name), span, "local value is not callable");
            if (last.name == "get_i32" || last.name == "print_i32" || last.name == "println_i32") {
                bool get = last.name == "get_i32";
                require(c.arguments.size() == (get ? 0u : 1u), span, "wrong builtin argument count");
                if (!get) expr(c.arguments[0].get(), make(Kind::I32));
                return {get ? make(Kind::I32) : unit};
            }
            auto f = functions.find(last.name);
            require(f != functions.end(), span, "unknown function " + last.name);
            return callFunction(f->second, c.arguments, span);
        }
        auto owner = segmentType(path.segments[0]);
        if ((owner->kind == Kind::Box || owner->kind == Kind::Vec) && last.name == "new") {
            require(c.arguments.size() == (owner->kind == Kind::Box ? 1u : 0u), span, "wrong constructor argument count");
            if (owner->kind == Kind::Box) expr(c.arguments[0].get(), owner->element);
            return {owner};
        }
        if (owner->kind == Kind::Struct) {
            auto f = functions.find(key(owner->name, last.name));
            if (f != functions.end()) return callFunction(f->second, c.arguments, span);
        }
        auto b = builtin(owner, last.name);
        require(b.has_value(), span, "unknown associated function");
        require(c.arguments.size() == b->params.size() + 1, span, "wrong argument count");
        expr(c.arguments[0].get(), b->receiver);
        for (std::size_t i = 0; i < b->params.size(); ++i) expr(c.arguments[i + 1].get(), b->params[i]);
        return {b->result};
    }
    Value method(const MethodCallExpr& m, SourceSpan span) {
        require(m.method.typeArguments.empty(), m.method.span, "method has no type parameters");
        Value receiver = expr(m.receiver.get());
        for (;;) {
            auto owner = receiver.type;
            std::vector<Ty> candidates{owner, make(Kind::Ref, owner), make(Kind::Ref, owner, true)};
            for (std::size_t index = 0; index < candidates.size(); ++index) {
                Ty candidate = candidates[index];
                Function* chosen = nullptr;
                for (auto& entry : functions) {
                    auto& f = entry.second;
                    if (!f.owner.empty() && f.decl->name == m.method.name && f.receiver && same(f.receiver, candidate)) { chosen = &f; break; }
                }
                std::optional<Builtin> b;
                // Each builtin belongs to its exact owner type. A shared-reference
                // clone therefore appears at &&T, after T's own clone candidate.
                if (candidate->kind == Kind::Ref) b = builtin(candidate->element, m.method.name);
                if (b && !same(b->receiver, candidate)) b.reset();
                if (!chosen && !b) continue;
                if (index == 2) mutableAccess(receiver, span);
                if (index == 0 && candidate->kind == Kind::Ref && candidate->mut)
                    require(!receiver.restricted, span, "mutable receiver crosses restricted access");
                if (chosen) return callFunction(*chosen, m.arguments, span, false);
                require(m.arguments.size() == b->params.size(), span, "wrong method argument count");
                for (std::size_t i = 0; i < m.arguments.size(); ++i) expr(m.arguments[i].get(), b->params[i]);
                return {b->result};
            }
            if (!indirect(receiver.type)) break;
            receiver = deref(receiver, span);
        }
        fail(span, "no matching method " + m.method.name);
    }
    Value expr(const Expr* e, Ty expected = {}) {
        require(e != nullptr, {}, "missing AST expression");
        Value result = rawExpr(e, expected);
        if (expected) {
            match(result.type, expected, e->span);
            if (expected->kind == Kind::Ref && expected->mut && result.type->kind != Kind::Never)
                require(!result.restricted, e->span, "mutable reborrow crosses shared or immutable vector access");
        }
        return result;
    }
    Value rawExpr(const Expr* e, Ty expected) {
        const auto& data = e->data;
        if (auto l = std::get_if<IntegerLiteral>(&data)) return {literalType(*l, expected)};
        if (std::holds_alternative<BoolLiteral>(data)) return {boolean};
        if (std::holds_alternative<UnitExpr>(data)) return {unit};
        if (auto p = std::get_if<PathExpr>(&data)) {
            auto name = valuePath(p->path);
            if (p->path.segments.size() == 1) if (auto v = local(name)) return *v;
            auto c = constants.find(name);
            require(c != constants.end(), e->span, "unknown value " + name);
            return {evalConstant(c->second).first};
        }
        if (auto u = std::get_if<UnaryExpr>(&data)) {
            Value v = expr(u->operand.get());
            if (u->op == UnaryOp::Dereference) return deref(v, e->span);
            auto t = arithmeticType(v.type);
            require(u->op == UnaryOp::Negate ? signedInteger(t) : scalar(t), e->span, "invalid unary operand");
            return {t};
        }
        if (auto b = std::get_if<BorrowExpr>(&data)) {
            Value v = expr(b->operand.get());
            if (b->isMutable) mutableAccess(v, e->span);
            return {make(Kind::Ref, v.type, b->isMutable)};
        }
        if (auto b = std::get_if<BinaryExpr>(&data)) {
            auto l = expr(b->left.get()), r = expr(b->right.get());
            return {binary(b->op, l.type, r.type, e->span)};
        }
        if (auto a = std::get_if<AssignmentExpr>(&data)) {
            auto target = expr(a->target.get());
            require(target.place, e->span, "assignment target is not a place"); mutableAccess(target, e->span);
            if (a->op == AssignmentOp::Assign) expr(a->value.get(), target.type);
            else {
                require(scalar(target.type), e->span, "compound assignment target must be scalar");
                bool shift = a->op == AssignmentOp::ShiftLeft || a->op == AssignmentOp::ShiftRight;
                auto rhs = expr(a->value.get(), !shift && std::holds_alternative<IntegerLiteral>(a->value->data) ? target.type : Ty{});
                BinaryOp op = BinaryOp::Add;
                switch (a->op) {
                case AssignmentOp::Add: op = BinaryOp::Add; break; case AssignmentOp::Subtract: op = BinaryOp::Subtract; break;
                case AssignmentOp::Multiply: op = BinaryOp::Multiply; break; case AssignmentOp::Divide: op = BinaryOp::Divide; break;
                case AssignmentOp::Remainder: op = BinaryOp::Remainder; break; case AssignmentOp::BitAnd: op = BinaryOp::BitAnd; break;
                case AssignmentOp::BitOr: op = BinaryOp::BitOr; break; case AssignmentOp::BitXor: op = BinaryOp::BitXor; break;
                case AssignmentOp::ShiftLeft: op = BinaryOp::ShiftLeft; break; case AssignmentOp::ShiftRight: op = BinaryOp::ShiftRight; break;
                default: break;
                }
                binary(op, target.type, rhs.type, e->span);
            }
            return {unit};
        }
        if (auto c = std::get_if<CastExpr>(&data)) {
            auto source = expr(c->operand.get()); auto target = type(c->target.get());
            require(scalar(source.type) && integer(target), e->span, "unsupported cast"); return {target};
        }
        if (auto a = std::get_if<ArrayExpr>(&data)) {
            Ty element = expected && expected->kind == Kind::Array ? expected->element : Ty{};
            Ty inferred;
            for (auto& item : a->elements) {
                auto v = expr(item.get(), element); inferred = lub(inferred, v.type, item->span);
            }
            return {make(Kind::Array, element ? element : (inferred ? inferred : never), false, a->elements.size())};
        }
        if (auto a = std::get_if<RepeatArrayExpr>(&data)) {
            auto count = constantExpr(a->count.get(), usize); match(count.first, usize, e->span);
            auto element = expr(a->element.get(), expected && expected->kind == Kind::Array ? expected->element : Ty{});
            require(count.second <= 1 || trait(element.type, DeriveTrait::Copy), e->span, "array repetition requires Copy");
            return {make(Kind::Array, element.type, false, count.second)};
        }
        if (auto s = std::get_if<StructExpr>(&data)) {
            auto t = pathType(s->path); require(t->kind == Kind::Struct, e->span, "struct expression requires a struct");
            auto& fields = structs.at(t->name).fields; std::set<std::string> initialized;
            for (auto& f : s->fields) {
                require(initialized.insert(f.name).second, f.nameSpan, "duplicate initializer field");
                auto field = fields.find(f.name); require(field != fields.end(), f.nameSpan, "unknown initializer field");
                expr(f.value.get(), field->second);
            }
            require(initialized.size() == fields.size(), e->span, "missing initializer field"); return {t};
        }
        if (auto c = std::get_if<CallExpr>(&data)) return call(*c, e->span);
        if (auto m = std::get_if<MethodCallExpr>(&data)) return method(*m, e->span);
        if (auto f = std::get_if<FieldExpr>(&data)) {
            auto base = expr(f->base.get());
            while (indirect(base.type)) base = deref(base, e->span);
            require(base.type->kind == Kind::Struct, e->span, "field access requires a struct");
            auto& fields = structs.at(base.type->name).fields; auto found = fields.find(f->name);
            require(found != fields.end(), f->nameSpan, "unknown field");
            base.mutablePlace = (!base.place || base.mutablePlace) && !base.restricted;
            base.type = found->second; base.place = true; return base;
        }
        if (auto i = std::get_if<IndexExpr>(&data)) {
            auto base = expr(i->base.get()); expr(i->index.get(), usize);
            while (indirect(base.type)) base = deref(base, e->span);
            require(base.type->kind == Kind::Array || base.type->kind == Kind::Vec, e->span, "value is not indexable");
            base.mutablePlace = (!base.place || base.mutablePlace) && !base.restricted;
            if (base.type->kind == Kind::Vec && !base.mutablePlace) base.restricted = true;
            base.type = base.type->element; base.place = true; return base;
        }
        if (auto b = std::get_if<BlockExpr>(&data)) {
            scopes.emplace_back(); bool diverges = false;
            for (auto& s : b->statements) {
                if (auto l = std::get_if<LetStmt>(&s->data)) {
                    Ty annotation = l->annotation ? type(l->annotation.get()) : Ty{};
                    auto v = expr(l->initializer.get(), annotation);
                    if (v.type->kind == Kind::Never) diverges = true;
                    scopes.back()[l->binding.name] = {annotation ? annotation : v.type, true, l->binding.isMutable, false};
                } else if (auto x = std::get_if<ExprStmt>(&s->data)) {
                    auto v = expr(x->expression.get(), x->hasSemicolon ? Ty{} : unit);
                    if (v.type->kind == Kind::Never) diverges = true;
                }
            }
            Ty tail = b->tail ? expr(b->tail.get(), expected).type : unit;
            scopes.pop_back(); return {diverges ? never : tail};
        }
        if (auto i = std::get_if<IfExpr>(&data)) {
            auto cond = expr(i->condition.get(), boolean);
            auto yes = expr(i->thenBranch.get(), i->elseBranch ? expected : unit);
            if (!i->elseBranch) return {cond.type->kind == Kind::Never ? never : unit};
            auto no = expr(i->elseBranch.get(), expected);
            return {cond.type->kind == Kind::Never ? never : lub(yes.type, no.type, e->span)};
        }
        if (auto l = std::get_if<LoopExpr>(&data)) {
            loops.push_back({false, expected, never});
            expr(l->body.get(), unit);
            Ty result = loops.back().result; loops.pop_back(); return {result};
        }
        if (auto w = std::get_if<WhileExpr>(&data)) {
            auto saved = std::move(loops); loops.clear(); expr(w->condition.get(), boolean); loops = std::move(saved);
            loops.push_back({true, unit, unit}); expr(w->body.get(), unit); loops.pop_back(); return {unit};
        }
        if (auto r = std::get_if<ReturnExpr>(&data)) {
            require(returnType != nullptr, e->span, "return outside a function");
            if (r->value) expr(r->value.get(), returnType); else match(unit, returnType, e->span);
            return {never};
        }
        if (auto b = std::get_if<BreakExpr>(&data)) {
            require(!loops.empty(), e->span, "break outside a loop");
            const auto index = loops.size() - 1;
            require(!loops[index].isWhile || !b->value, e->span, "while cannot accept a break value");
            Ty expectedBreak = loops[index].expected;
            auto v = b->value ? expr(b->value.get(), expectedBreak).type : unit;
            if (expectedBreak) match(v, expectedBreak, e->span);
            loops[index].result = lub(loops[index].result, v, e->span); return {never};
        }
        if (std::holds_alternative<ContinueExpr>(data)) {
            require(!loops.empty(), e->span, "continue outside a loop"); return {never};
        }
        fail(e->span, "unsupported expression");
    }
public:
    explicit Checker(const Program& p) : program(p) {}
    void run() {
        declarations();
        for (auto& entry : functions) {
            auto& f = entry.second; self = f.owner; returnType = f.result;
            scopes.clear(); scopes.emplace_back(); loops.clear();
            if (f.receiver) {
                auto kind = f.decl->receiver->kind;
                scopes.back()["self"] = {f.receiver, true, kind == ReceiverKind::MutableValue, false};
            }
            for (std::size_t i = 0; i < f.params.size(); ++i) scopes.back()[f.decl->parameters[i].binding.name] = {f.params[i], true, f.decl->parameters[i].binding.isMutable, false};
            expr(f.decl->body.get(), f.result);
        }
    }
};

} // namespace

SemanticResult checkProgram(const Program& program) {
    SemanticResult result;
    try { Checker(program).run(); }
    catch (const Failure& f) { result.diagnostics.push_back({DiagnosticPhase::Semantic, f.span, f.what()}); }
    return result;
}
SemanticResult checkSource(const std::string& source, std::string sourceName) {
    auto parsed = parseSource(source, std::move(sourceName));
    if (!parsed) return {std::move(parsed.diagnostics)};
    return checkProgram(*parsed.program);
}

} // namespace rxast
