#include <algorithm>

#include <thorin/rewrite.h>

#include "check.h"

namespace artic {

bool TypeChecker::run(const ast::ModDecl& module) {
    module.infer(*this);
    return error_count == 0;
}

std::optional<size_t> TypeChecker::find_member(const Type* type, const std::string& member) {
    auto meta = type->meta();
    assert(meta);
    auto name = world().tuple_str(member);
    for (size_t i = 0, n = meta->type()->lit_arity(); i < n; ++i) {
        if (name == meta->out(i))
            return i;
    }
    return std::nullopt;
}

bool TypeChecker::enter_decl(const ast::Decl* decl) {
    auto [_, success] = decls_.emplace(decl);
    if (!success) {
        error(decl->loc, "cannot infer type for recursive declaration");
        return false;
    }
    return true;
}

void TypeChecker::exit_decl(const ast::Decl* decl) {
    decls_.erase(decl);
}

bool TypeChecker::should_emit_error(const Type* type) {
    return !contains(type, world().type_error());
}

void TypeChecker::explain_no_ret(const Type* type, const Type* expected) {
    auto no_ret = world().type_no_ret();
    if ((type && contains(type, no_ret)) || contains(expected, no_ret)) {
        note("the type '{}' indicates a {} or {} type, used to denote the return type of functions like '{}', '{}', or '{}'",
             *no_ret,
             log::style("bottom", log::Style::Italic),
             log::style("no-return", log::Style::Italic),
             log::keyword_style("break"),
             log::keyword_style("continue"),
             log::keyword_style("return"));
        note("this error {} indicate that you forgot to add parentheses '()' in the call to one of those functions",
            log::style("may", log::Style::Italic));
    }
}

const Type* TypeChecker::expect(const Loc& loc, const std::string& msg, const Type* type, const Type* expected) {
    if (auto best = join(type, expected))
        return best;
    if (should_emit_error(type) && should_emit_error(expected)) {
        error(loc, "expected type '{}', but got {} with type '{}'", *expected, msg, *type);
        explain_no_ret(type, expected);
    }
    return world().type_error();
}

const Type* TypeChecker::expect(const Loc& loc, const std::string& msg, const Type* expected) {
    if (should_emit_error(expected)) {
        error(loc, "expected type '{}', but got {}", *expected, msg);
        explain_no_ret(nullptr, expected);
    }
    return world().type_error();
}

const Type* TypeChecker::expect(const Loc& loc, const Type* type, const Type* expected) {
    if (auto best = join(type, expected))
        return best;
    if (should_emit_error(type) && should_emit_error(expected)) {
        error(loc, "expected type '{}', but got type '{}'", *expected, *type);
        explain_no_ret(type, expected);
    }
    return world().type_error();
}

const Type* TypeChecker::struct_expected(const Loc& loc, const artic::Type* type) {
    if (should_emit_error(type))
        error(loc, "structure type expected, but got '{}'", *type);
    return world().type_error();
}

const Type* TypeChecker::unknown_member(const Loc& loc, const Type* struct_type, const std::string& field) {
    error(loc, "no field '{}' in '{}'", field, *struct_type);
    return world().type_error();
}

const Type* TypeChecker::cannot_infer(const Loc& loc, const std::string& msg) {
    error(loc, "cannot infer type for {}", msg);
    return world().type_error();
}

const Type* TypeChecker::unreachable_code(const Loc& before, const Loc& first, const Loc& last) {
    error(Loc(first, last), "unreachable code");
    note(before, "after this statement");
    return world().type_error();
}

const Type* TypeChecker::check(const ast::Node& node, const Type* type) {
    assert(!node.type); // Nodes can only be visited once
    return node.type = node.check(*this, type);
}

const Type* TypeChecker::infer(const ast::Node& node) {
    if (node.type)
        return node.type;
    return node.type = node.infer(*this);
}

const Type* TypeChecker::infer(const ast::CallExpr& call) {
    auto callee_type = infer(*call.callee);
    if (auto pi = callee_type->isa<thorin::Pi>()) {
        check(*call.arg, pi->domain());
        return pi->codomain();
    } else if (auto variadic = callee_type->isa<thorin::Variadic>()) {
        auto index_type = infer(*call.arg);
        if (!is_uint_type(index_type) && !is_sint_type(index_type)) {
            if (should_emit_error(index_type))
                error(call.arg->loc, "integer type expected as array index, but got '{}'", *index_type);
            return world().type_error();
        }
        return variadic->body();
    } else {
        if (should_emit_error(callee_type))
            error(call.callee->loc, "expected function or array type in call expression, but got '{}'", *callee_type);
        return world().type_error();
    }
}

const Type* TypeChecker::check(const ast::TypeParamList& type_params, Type* parent) {
    // If there is only one parameter, set its name
    thorin::Debug dbg { type_params.params.size() == 1 ? type_params.params[0]->id.name : "" };
    return check(type_params, parent->param(dbg));
}

const Type* TypeChecker::infer(const Loc&, const Literal& lit) {
    if (lit.is_integer())
        return world().type_sint(32);
    else if (lit.is_double())
        return world().type_real(64);
    else if (lit.is_bool())
        return world().type_bool();
    else if (lit.is_char())
        return world().type_uint(8);
    else if (lit.is_string())
        return world().variadic_unsafe(world().type_uint(8));
    else {
        assert(false);
        return world().type_error();
    }
}

const Type* TypeChecker::check(const Loc& loc, const Literal& lit, const Type* expected) {
    if (is_no_ret_type(expected))
        return infer(loc, lit);
    if (lit.is_integer()) {
        if (!is_sint_type(expected) && !is_uint_type(expected) && !is_real_type(expected))
            return expect(loc, "integer literal", expected);
        return expected;
    } else if (lit.is_double()) {
        if (!is_real_type(expected))
            return expect(loc, "floating point literal", expected);
        return expected;
    } else if (lit.is_bool()) {
        return expect(loc, "boolean literal", world().type_bool(), expected);
    } else if (lit.is_char()) {
        return expect(loc, "character literal", world().type_uint(8), expected);
    } else if (lit.is_string()) {
        return expect(loc, "string literal", world().variadic_unsafe(world().type_uint(8)), expected);
    } else {
        assert(false);
        return expected;
    }
}

bool TypeChecker::check_mut(const ast::Node& node) {
    auto* cur = &node;
    const ast::Decl* decl = nullptr;
    while (true) {
        assert(cur->type);
        if (auto path_expr = cur->isa<ast::PathExpr>()) {
            if (path_expr->path.mut)
                return true;
            if (path_expr->path.symbol && !path_expr->path.symbol->decls.empty())
                decl = path_expr->path.symbol->decls.front();
        } else if (auto proj_expr = cur->isa<ast::ProjExpr>()) {
            cur = proj_expr->expr.get();
            continue;
        } else if (auto call_expr = cur->isa<ast::CallExpr>()) {
            if (call_expr->callee->type->isa<thorin::Variadic>()) {
                cur = call_expr->callee.get();
                continue;
            }
        }
        break;
    }
    error(node.loc, "assignment to a non-mutable expression");
    if (decl)
        note(decl->loc, "this error {} be solved by adding the '{}' qualifier to this symbol", log::style("may", log::Style::Italic), log::keyword_style("mut"));
    return false;
}

template <typename Args>
const Type* TypeChecker::check_tuple(const Loc& loc, const std::string& msg, const Args& args, const Type* expected) {
    if (!expected->isa<thorin::Sigma>())
        return expect(loc, msg, expected);
    if (args.size() != expected->num_ops()) {
        error(loc, "expected {} argument(s) in {}, but got {}", expected->num_ops(), msg, args.size());
        return world().type_error();
    }
    for (size_t i = 0; i < args.size(); ++i)
        check(*args[i], expected->op(i));
    return expected;
}

template <typename Args>
const Type* TypeChecker::infer_tuple(const Args& args) {
    thorin::Array<const Type*> arg_types(args.size());
    for (size_t i = 0; i < args.size(); ++i)
        arg_types[i] = infer(*args[i]);
    return world().sigma(arg_types);
}

template <typename Fields>
const Type* TypeChecker::check_fields(const Loc& loc, const Type* struct_type, const Type* app, const Fields& fields, bool etc, const std::string& msg) {
    size_t num_fields = struct_type->meta()->type()->lit_arity();
    thorin::Array<bool> seen(num_fields, false);
    for (size_t i = 0; i < fields.size(); ++i) {
        // Skip the field if it is '...'
        if (fields[i]->is_etc())
            continue;
        auto index = find_member(struct_type, fields[i]->id.name);
        if (!index)
            return unknown_member(fields[i]->loc, struct_type, fields[i]->id.name);
        if (seen[*index]) {
            error(loc, "field '{}' specified more than once", fields[i]->id.name);
            return world().type_error();
        }
        seen[*index] = true;
        // Rewrite the field type if the structure has type arguments
        auto field_type = struct_type->op(*index);
        if (app)
            field_type = thorin::rewrite(field_type, struct_type->as_nominal()->param(), app->as<thorin::App>()->arg());
        check(*fields[i], field_type);
    }
    // Check that all fields have been specified, unless '...' was used
    if (!etc && !std::all_of(seen.begin(), seen.end(), [] (bool b) { return b; })) {
        for (size_t i = 0; i < num_fields; ++i) {
            if (!seen[i])
                error(loc, "missing field '{}' in structure {}", thorin::tuple2str(struct_type->meta()->out(i)), msg);
        }
    }
    return app ? app : struct_type;
}

namespace ast {

const artic::Type* Node::check(TypeChecker& checker, const artic::Type* expected) const {
    // By default, try to infer, and then check that types match
    return checker.expect(loc, checker.infer(*this), expected);
}

const artic::Type* Node::infer(TypeChecker& checker) const {
    return checker.cannot_infer(loc, "expression");
}

const artic::Type* Path::infer(TypeChecker& checker) const {
    if (!symbol || symbol->decls.empty())
        return checker.world().type_error();
    auto type = checker.infer(*symbol->decls.front());
    // Set the path as mutable if it refers to a mutable symbol
    if (auto ptrn_decl = symbol->decls.front()->isa<PtrnDecl>())
        mut = ptrn_decl->mut;

    // Inspect every element of the path
    for (size_t i = 0; i < elems.size(); ++i) {
        auto& elem = elems[i];

        // Apply type arguments (if any)
        bool is_poly_ctor = type->type() != checker.world().kind_star();
        bool is_poly_fn   = type->isa_nominal<thorin::Pi>();
        if (is_poly_ctor || is_poly_fn) {
            if (!elem.args.empty()) {
                thorin::Array<const artic::Type*> type_args(elem.args.size());
                for (size_t i = 0, n = type_args.size(); i < n; ++i)
                    type_args[i] = checker.infer(*elem.args[i]);
                type = is_poly_ctor
                    ? checker.world().app(type, checker.world().tuple(type_args))
                    : thorin::rewrite(type->as<thorin::Pi>()->codomain(), type->as_nominal<thorin::Pi>()->param(), checker.world().tuple(type_args));
            } else {
                checker.error(elem.loc, "missing type arguments");
                return checker.world().type_error();
            }
        } else if (!elem.args.empty()) {
            checker.error(elem.loc, "type arguments are not allowed here");
            return checker.world().type_error();
        }

        // Perform a lookup inside the current object if the path is not finished
        if (i != elems.size() - 1) {
            auto& member = elems[i + 1].id.name;
            // TODO: Modules
            if (thorin::isa<Tag::EnumType>(type)) {
                auto app = type->isa<thorin::App>();
                if (app) type = app->callee();
                
                auto index = checker.find_member(type, member);
                if (!index)
                    return checker.unknown_member(elem.loc, type, member);
                type = app ? thorin::rewrite(type->op(*index), type->as_nominal()->param(), app->arg()) : type->op(*index);
            } else {
                checker.error(elem.loc, "operator '::' not allowed on type '{}'", *type);
                return checker.world().type_error();
            }
        }
    }
    return type;
}

// Types ---------------------------------------------------------------------------

const artic::Type* PrimType::infer(TypeChecker& checker) const {
    switch (tag) {
        case Bool: return checker.world().type_bool();   break;
        case I8:   return checker.world().type_sint(8);  break;
        case I16:  return checker.world().type_sint(16); break;
        case I32:  return checker.world().type_sint(32); break;
        case I64:  return checker.world().type_sint(64); break;
        case U8:   return checker.world().type_uint(8);  break;
        case U16:  return checker.world().type_uint(16); break;
        case U32:  return checker.world().type_uint(32); break;
        case U64:  return checker.world().type_uint(64); break;
        case F32:  return checker.world().type_real(32); break;
        case F64:  return checker.world().type_real(64); break;
        default:
            // This is a parsing error and has already been reported
            return checker.world().type_error();
    }
}

const artic::Type* TupleType::infer(TypeChecker& checker) const {
    return checker.infer_tuple(args);
}

const artic::Type* ArrayType::infer(TypeChecker& checker) const {
    return checker.world().variadic_unsafe(checker.infer(*elem));
}

const artic::Type* FnType::infer(TypeChecker& checker) const {
    return checker.world().pi(checker.infer(*from), checker.infer(*to));
}

const artic::Type* PtrType::infer(TypeChecker& checker) const {
    return checker.world().type_ptr(checker.infer(*pointee));
}

const artic::Type* TypeApp::infer(TypeChecker& checker) const {
    return checker.infer(path);
}

// Statements ----------------------------------------------------------------------

const artic::Type* DeclStmt::infer(TypeChecker& checker) const {
    return checker.infer(*decl);
}

const artic::Type* DeclStmt::check(TypeChecker& checker, const artic::Type* expected) const {
    return checker.check(*decl, expected);
}

const artic::Type* ExprStmt::infer(TypeChecker& checker) const {
    return checker.infer(*expr);
}

const artic::Type* ExprStmt::check(TypeChecker& checker, const artic::Type* expected) const {
    return checker.check(*expr, expected);
}

// Expressions ---------------------------------------------------------------------

const artic::Type* TypedExpr::infer(TypeChecker& checker) const {
    return checker.check(*expr, checker.infer(*type));
}

const artic::Type* PathExpr::infer(TypeChecker& checker) const {
    return checker.infer(path);
}

const artic::Type* LiteralExpr::infer(TypeChecker& checker) const {
    return checker.infer(loc, lit);
}

const artic::Type* LiteralExpr::check(TypeChecker& checker, const artic::Type* expected) const {
    return checker.check(loc, lit, expected);
}

const artic::Type* FieldExpr::check(TypeChecker& checker, const artic::Type* expected) const {
    return checker.check(*expr, expected);
}

const artic::Type* StructExpr::infer(TypeChecker& checker) const {
    auto expr_type = checker.infer(*expr);
    auto app = expr_type->isa<thorin::App>();
    if (app) expr_type = app->callee();
    if (!is_struct_type(expr_type))
        return checker.struct_expected(loc, expr_type);
    return checker.check_fields(loc, expr_type->as_nominal(), app, fields, false, "expression");
}

const artic::Type* TupleExpr::infer(TypeChecker& checker) const {
    return checker.infer_tuple(args);
}

const artic::Type* TupleExpr::check(TypeChecker& checker, const artic::Type* expected) const {
    return checker.check_tuple(loc, "tuple expression", args, expected);
}

const artic::Type* ArrayExpr::infer(TypeChecker& checker) const {
    if (elems.empty())
        return checker.cannot_infer(loc, "array expression");
    auto elem_type = checker.infer(*elems.front());
    for (size_t i = 1; i < elems.size(); ++i)
        checker.check(*elems[i], elem_type);
    return checker.world().variadic_unsafe(elem_type);
}

const artic::Type* ArrayExpr::check(TypeChecker& checker, const artic::Type* expected) const {
    if (!expected->isa<thorin::Variadic>())
        return checker.expect(loc, "array expression", expected);
    auto elem_type = expected->as<thorin::Variadic>()->body();
    for (auto& elem : elems)
        checker.check(*elem, elem_type);
    return checker.world().variadic_unsafe(elem_type);
}

const artic::Type* FnExpr::infer(TypeChecker& checker) const {
    auto body_type = ret_type ? checker.infer(*ret_type) : nullptr;
    if (body || body_type) {
        auto param_type = checker.infer(*param);
        body_type = body_type ? checker.check(*body, body_type) : checker.infer(*body);
        return checker.world().pi(param_type, body_type);
    }
    return checker.cannot_infer(loc, "function");
}

const artic::Type* FnExpr::check(TypeChecker& checker, const artic::Type* expected) const {
    if (!expected->isa<thorin::Pi>())
        return checker.expect(loc, "anonymous function", expected);
    auto param_type = checker.check(*param, expected->as<thorin::Pi>()->domain());
    auto body_type  = checker.check(*body, expected->as<thorin::Pi>()->codomain());
    return checker.world().pi(param_type, body_type);
}

const artic::Type* BlockExpr::infer(TypeChecker& checker) const {
    if (stmts.empty())
        return checker.world().sigma();
    for (size_t i = 0; i < stmts.size() - 1; ++i) {
        auto stmt_type = checker.infer(*stmts[i]);
        if (is_no_ret_type(stmt_type))
            return checker.unreachable_code(stmts[i]->loc, stmts[i + 1]->loc, stmts.back()->loc);
    }
    auto last_type = checker.infer(*stmts.back());
    return last_semi ? checker.world().sigma() : last_type;
}

const artic::Type* BlockExpr::check(TypeChecker& checker, const artic::Type* expected) const {
    if (stmts.empty())
        return checker.expect(loc, "block expression", checker.world().sigma(), expected);
    for (size_t i = 0; i < stmts.size() - 1; ++i) {
        auto stmt_type = checker.infer(*stmts[i]);
        if (is_no_ret_type(stmt_type))
            return checker.unreachable_code(stmts[i]->loc, stmts[i + 1]->loc, stmts.back()->loc);
    }
    if (last_semi) {
        auto last_type = checker.check(*stmts.back(), checker.world().sigma());
        return checker.expect(loc, "block expression", last_type, expected);
    }
    return checker.check(*stmts.back(), expected);
}

const artic::Type* CallExpr::infer(TypeChecker& checker) const {
    return checker.infer(*this);
}

const artic::Type* ProjExpr::infer(TypeChecker& checker) const {
    auto expr_type = checker.infer(*expr);
    auto app = expr_type->isa<thorin::App>();
    if (app) expr_type = app->callee();
    if (!is_struct_type(expr_type))
        return checker.struct_expected(loc, expr_type);
    auto struct_type = expr_type->as_nominal();
    if (auto index = checker.find_member(struct_type, field.name))
        return app ? thorin::rewrite(struct_type->op(*index), struct_type->param(), app->arg()) : struct_type->op(*index);
    else
        return checker.unknown_member(loc, struct_type, field.name);
}

const artic::Type* IfExpr::infer(TypeChecker& checker) const {
    checker.check(*cond, checker.world().type_bool());
    if (if_false)
        return checker.check(*if_false, checker.infer(*if_true));
    return checker.check(*if_true, checker.world().sigma());
}

const artic::Type* IfExpr::check(TypeChecker& checker, const artic::Type* expected) const {
    checker.check(*cond, checker.world().type_bool());
    auto true_type = checker.check(*if_true, expected);
    if (if_false)
        return checker.check(*if_false, true_type);
    return true_type;
}

const artic::Type* MatchExpr::infer(TypeChecker& checker) const {
    return check(checker, nullptr);
}

const artic::Type* MatchExpr::check(TypeChecker& checker, const artic::Type* expected) const {
    auto arg_type = checker.infer(*arg);
    const artic::Type* type = expected;
    for (auto& case_ : cases) {
        checker.check(*case_->ptrn, arg_type);
        type = type ? checker.check(*case_->expr, type) : checker.infer(*case_->expr);
    }
    return type ? type : checker.cannot_infer(loc, "match expression");
}

const artic::Type* WhileExpr::infer(TypeChecker& checker) const {
    checker.check(*cond, checker.world().type_bool());
    checker.infer(*body);
    return checker.world().sigma();
}

const artic::Type* ForExpr::infer(TypeChecker& checker) const {
    return checker.infer(*body->as<CallExpr>());
}

const artic::Type* BreakExpr::infer(TypeChecker& checker) const {
    return checker.world().pi(checker.world().sigma(), checker.world().type_no_ret());
}

const artic::Type* ContinueExpr::infer(TypeChecker& checker) const {
    return checker.world().pi(checker.world().sigma(), checker.world().type_no_ret());
}

const artic::Type* ReturnExpr::infer(TypeChecker& checker) const {
    if (fn) {
        const artic::Type* arg_type = nullptr;
        if (fn->type && fn->type->isa<thorin::Pi>())
            arg_type = fn->type->as<thorin::Pi>()->codomain();
        else if (fn->ret_type && fn->ret_type->type)
            arg_type = fn->ret_type->type;
        if (arg_type)
           return checker.world().pi(arg_type, checker.world().type_no_ret());
    }
    checker.error(loc, "cannot infer the type of '{}'", log::keyword_style("return"));
    if (fn)
        checker.note(fn->loc, "try annotating the return type of this function");
    return checker.world().type_error();
}

const artic::Type* UnaryExpr::infer(TypeChecker& checker) const {
    auto arg_type = checker.infer(*arg);
    if (is_inc() || is_dec()) {
        checker.check_mut(*arg);
        return arg_type;
    }
    return arg_type;
}

const artic::Type* BinaryExpr::infer(TypeChecker& checker) const {
    auto left_type  = checker.infer(*left);
    auto right_type = checker.check(*right, left_type);
    if (has_eq()) {
        checker.check_mut(*left);
        return checker.world().sigma();
    }
    return has_cmp() ? checker.world().type_bool() : right_type;
}

// Declarations --------------------------------------------------------------------

const artic::Type* TypeParam::check(TypeChecker&, const artic::Type* expected) const {
    return expected;
}

const artic::Type* TypeParamList::check(TypeChecker& checker, const artic::Type* expected) const {
    if (params.size() == 1)
        checker.check(*params.back(), expected);
    else {
        for (size_t i = 0; i < params.size(); ++i)
            checker.check(*params[i], checker.world().extract(expected, i, { params[i]->id.name }));
    }
    return expected;
}

const artic::Type* PtrnDecl::check(TypeChecker&, const artic::Type* expected) const {
    return expected;
}

const artic::Type* LetDecl::infer(TypeChecker& checker) const {
    if (init)
        checker.check(*ptrn, checker.infer(*init));
    else
        checker.infer(*ptrn);
    return checker.world().sigma();
}

const artic::Type* FnDecl::infer(TypeChecker& checker) const {
    artic::Type* forall = nullptr;
    if (type_params) {
        forall = checker.world().type_forall(*this);
        checker.check(*type_params, forall->param());
    }
    if (fn->ret_type) {
        auto fn_type = checker.world().pi(checker.infer(*fn->param), checker.infer(*fn->ret_type));
        if (forall) {
            forall->set(1, fn_type);
            type = forall;
        } else {
            type = fn_type;
        }
    }
    if (!checker.enter_decl(this))
        return checker.world().type_error();
    auto fn_type = checker.infer(*fn);
    if (forall)
        forall->set(1, fn_type);
    checker.exit_decl(this);
    return forall ? forall : fn_type;
}

const artic::Type* FnDecl::check(TypeChecker& checker, const artic::Type* expected) const {
    // Inside a block expression, statements are expected to type as ()
    // So we ignore the expected type here
    (void)expected;
    assert(expected == checker.world().sigma());
    return infer(checker);
}

const artic::Type* FieldDecl::infer(TypeChecker& checker) const {
    return checker.infer(*type);
}

const artic::Type* StructDecl::infer(TypeChecker& checker) const {
    auto struct_type = checker.world().type_struct(*this);
    if (type_params) checker.check(*type_params, struct_type);
    // Set the type before entering the fields
    type = struct_type;
    for (size_t i = 0; i < fields.size(); ++i)
        struct_type->set(i, checker.infer(*fields[i]));
    return struct_type;
}

const artic::Type* OptionDecl::check(TypeChecker&, const artic::Type* expected) const {
    return expected;
}

const artic::Type* EnumDecl::infer(TypeChecker& checker) const {
    auto enum_type = checker.world().type_enum(*this);
    if (type_params) checker.check(*type_params, enum_type);
    // Set the type before entering the options
    type = enum_type;
    for (size_t i = 0; i < options.size(); ++i) {
        auto applied_type = type_params
            ? checker.world().app(enum_type, type_params->type)
            : enum_type;
        auto option_type = options[i]->param
            ? checker.world().pi(checker.infer(*options[i]->param), applied_type)
            : applied_type;
        enum_type->set(i, checker.check(*options[i], option_type));
    }
    return enum_type;
}

const artic::Type* ModDecl::infer(TypeChecker& checker) const {
    for (auto& decl : decls)
        checker.infer(*decl);
    // TODO: Return proper type for the module
    return nullptr;
}

// Patterns ------------------------------------------------------------------------

const artic::Type* TypedPtrn::infer(TypeChecker& checker) const {
    return checker.check(*ptrn, checker.infer(*type));
}

const artic::Type* LiteralPtrn::infer(TypeChecker& checker) const {
    return checker.infer(loc, lit);
}

const artic::Type* LiteralPtrn::check(TypeChecker& checker, const artic::Type* expected) const {
    return checker.check(loc, lit, expected);
}

const artic::Type* IdPtrn::infer(TypeChecker& checker) const {
    // Needed because the error type will be attached to the decl,
    // which is what is connected to the uses of the identifier.
    return checker.infer(*decl);
}

const artic::Type* IdPtrn::check(TypeChecker& checker, const artic::Type* expected) const {
    return checker.check(*decl, expected);
}

const artic::Type* FieldPtrn::check(TypeChecker& checker, const artic::Type* expected) const {
    return checker.check(*ptrn, expected);
}

const artic::Type* StructPtrn::infer(TypeChecker& checker) const {
    auto path_type = checker.infer(path);
    auto app = path_type->isa<thorin::App>();
    if (app) path_type = app->callee();
    if (!is_struct_type(path_type))
        return checker.struct_expected(loc, path_type);
    return checker.check_fields(loc, path_type->as_nominal(), app, fields, has_etc(), "pattern");
}

const artic::Type* TuplePtrn::infer(TypeChecker& checker) const {
    return checker.infer_tuple(args);
}

const artic::Type* TuplePtrn::check(TypeChecker& checker, const artic::Type* expected) const {
    return checker.check_tuple(loc, "tuple pattern", args, expected);
}

} // namespace ast

} // namespace artic
