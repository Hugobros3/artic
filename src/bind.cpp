#include "artic/bind.h"
#include "artic/ast.h"

namespace artic {

bool NameBinder::run(ast::ModDecl& mod) {
    bind(mod);
    return errors == 0;
}

void NameBinder::bind_head(ast::Decl& decl) {
    decl.bind_head(*this);
}

void NameBinder::bind(ast::Node& node) {
    if (node.attrs)
        node.attrs->bind(*this);
    node.bind(*this);
}

void NameBinder::pop_scope() {
    for (auto& pair : scopes_.back().symbols) {
        auto decl = pair.second->decls.front();
        if (pair.second.use_count() <= 1 &&
            !scopes_.back().top_level &&
            !decl->isa<ast::FieldDecl>() &&
            !decl->isa<ast::OptionDecl>()) {
            warn(decl->loc, "unused identifier '{}'", pair.first);
            note("prefix unused identifiers with '_'");
        }
    }
    scopes_.pop_back();
}

void NameBinder::insert_symbol(ast::NamedDecl& decl) {
    assert(!scopes_.empty());
    auto& name = decl.id.name;
    assert(!name.empty());

    // Do not bind anonymous variables
    if (name[0] == '_') return;

    auto shadow_symbol = find_symbol(name);
    if (!scopes_.back().insert(name, Symbol(&decl))) {
        error(decl.loc, "identifier '{}' already declared", name);
        for (auto other : shadow_symbol->decls) {
            if (other != &decl) note(other->loc, "previously declared here");
        }
    } else if (
        warn_on_shadowing && shadow_symbol &&
        decl.isa<ast::PtrnDecl>() &&
        !shadow_symbol->decls[0]->is_top_level) {
        warn(decl.loc, "declaration shadows identifier '{}'", name);
        note(shadow_symbol->decls[0]->loc, "previously declared here");
    }
}

namespace ast {

// Path ----------------------------------------------------------------------------

void Path::bind(NameBinder& binder) {
    // Bind the first element of the path
    auto& first = elems.front();
    if (first.id.name[0] == '_')
        binder.error(first.id.loc, "identifiers beginning with '_' cannot be referenced");
    else {
        // TODO this assumes symbols are always the first element of a path
        // question: can paths of length > 2 even exist currently? afaik the only case of length 1 paths currently is enums...
        symbol = binder.find_symbol(first.id.name);
        if (!symbol) {
            binder.error(first.id.loc, "unknown identifier '{}'", first.id.name);
            if (auto similar = binder.find_similar_symbol(first.id.name)) {
                auto decl = similar->decls.front();
                binder.note("did you mean '{}'?", decl->id.name);
            }
        }
    }
    // Bind the type arguments of each element
    for (auto& elem : elems) {
        for (auto& arg : elem.args)
            binder.bind(*arg);
    }
}

// Filter --------------------------------------------------------------------------

void Filter::bind(NameBinder& binder) {
    if (expr) binder.bind(*expr);
}

// Attributes ----------------------------------------------------------------------

void Attr::bind(NameBinder&) {
    // Do nothing
}

void PathAttr::bind(NameBinder& binder) {
    binder.bind(path);
}

void NamedAttr::bind(NameBinder& binder) {
    for (auto& arg : args)
        binder.bind(*arg);
}

// Types ---------------------------------------------------------------------------

void PrimType::bind(NameBinder&) {}

void TupleType::bind(NameBinder& binder) {
    for (auto& arg : args) binder.bind(*arg);
}

void ArrayType::bind(NameBinder& binder) {
    binder.bind(*elem);
}

void FnType::bind(NameBinder& binder) {
    binder.bind(*from);
    if (to) binder.bind(*to);
}

void PtrType::bind(NameBinder& binder) {
    binder.bind(*pointee);
}

void TypeApp::bind(NameBinder& binder) {
    binder.bind(path);
}

void ErrorType::bind(NameBinder&) {}

// Statements ----------------------------------------------------------------------

void DeclStmt::bind(NameBinder& binder) {
    binder.bind(*decl);
}

void ExprStmt::bind(NameBinder& binder) {
    binder.bind(*expr);
}

// Expressions ---------------------------------------------------------------------

void TypedExpr::bind(NameBinder& binder) {
    binder.bind(*expr);
    binder.bind(*type);
}

void PathExpr::bind(NameBinder& binder) {
    binder.bind(path);
}

void LiteralExpr::bind(NameBinder&) {}

void FieldExpr::bind(NameBinder& binder) {
    binder.bind(*expr);
}

void RecordExpr::bind(NameBinder& binder) {
    if (expr)
        binder.bind(*expr);
    else
        binder.bind(*type);
    for (auto& field : fields) binder.bind(*field);
}

void TupleExpr::bind(NameBinder& binder) {
    for (auto& arg : args) binder.bind(*arg);
}

void ArrayExpr::bind(NameBinder& binder) {
    for (auto& elem : elems) binder.bind(*elem);
}

void RepeatArrayExpr::bind(NameBinder& binder) {
    binder.bind(*elem);
}

void FnExpr::bind(NameBinder& binder, bool in_for_loop) {
    binder.push_scope();
    if (param)    binder.bind(*param);
    if (ret_type) binder.bind(*ret_type);
    if (filter)   binder.bind(*filter);
    binder.push_scope();
    // Do not rebind the current `return` to this function
    // for anonymous functions introduced as for loop bodies.
    ast::FnExpr* old = nullptr;
    if (!in_for_loop) old = binder.push_fn(this);
    binder.bind(*body);
    if (!in_for_loop) binder.pop_fn(old);
    binder.pop_scope();
    binder.pop_scope();
}

void FnExpr::bind(NameBinder& binder) {
    bind(binder, false);
}

void BlockExpr::bind(NameBinder& binder) {
    binder.push_scope();
    for (auto& stmt : stmts) {
        if (auto decl_stmt = stmt->isa<DeclStmt>())
            binder.bind_head(*decl_stmt->decl);
    }
    for (auto& stmt : stmts) binder.bind(*stmt);
    binder.pop_scope();
}

void CallExpr::bind(NameBinder& binder) {
    binder.bind(*callee);
    binder.bind(*arg);
}

void UnaryExpr::bind(NameBinder& binder) {
    binder.bind(*arg);
}

void BinaryExpr::bind(NameBinder& binder) {
    binder.bind(*left);
    binder.bind(*right);
}

void ProjExpr::bind(NameBinder& binder) {
    binder.bind(*expr);
    // Cannot bind field yet, need type inference
}

void IfExpr::bind(NameBinder& binder) {
    binder.push_scope();
    if (cond)
        binder.bind(*cond);
    else {
        binder.bind(*ptrn);
        binder.bind(*expr);
    }
    binder.bind(*if_true);
    binder.pop_scope();
    if (if_false) binder.bind(*if_false);
}

void CaseExpr::bind(NameBinder& binder) {
    binder.push_scope();
    binder.bind(*ptrn);
    binder.bind(*expr);
    binder.pop_scope();
}

void MatchExpr::bind(NameBinder& binder) {
    binder.bind(*arg);
    for (auto& case_ : cases)
        binder.bind(*case_);
}

void WhileExpr::bind(NameBinder& binder) {
    binder.push_scope();
    if (cond)
        binder.bind(*cond);
    else {
        binder.bind(*ptrn);
        binder.bind(*expr);
    }
    auto old = binder.push_loop(this);
    binder.bind(*body);
    binder.pop_loop(old);
    binder.pop_scope();
}

void ForExpr::bind(NameBinder& binder) {
    // The call expression looks like:
    // iterate(|i| { ... })(...)
    // continue() and break() should only be available to the lambda
    binder.bind(*call->callee->as<CallExpr>()->callee);
    auto old = binder.push_loop(this);
    auto loop_body = call->callee->as<CallExpr>()->arg->as<FnExpr>();
    if (loop_body->attrs)
        loop_body->attrs->bind(binder);
    loop_body->bind(binder, true);
    binder.pop_loop(old);
    binder.bind(*call->arg);
}

void BreakExpr::bind(NameBinder& binder) {
    loop = binder.cur_loop();
    if (!loop)
        binder.error(loc, "use of '{}' outside of a loop", *this->as<Node>());
}

void ContinueExpr::bind(NameBinder& binder) {
    loop = binder.cur_loop();
    if (!loop)
        binder.error(loc, "use of '{}' outside of a loop", *this->as<Node>());
}

void ReturnExpr::bind(NameBinder& binder) {
    fn = binder.cur_fn();
    if (!fn)
        binder.error(loc, "use of '{}' outside of a function", *this->as<Node>());
}

void FilterExpr::bind(NameBinder& binder) {
    binder.bind(*filter);
    binder.bind(*expr);
}

void CastExpr::bind(NameBinder& binder) {
    binder.bind(*expr);
    binder.bind(*type);
}

void ImplicitCastExpr::bind(NameBinder&) {}

void AsmExpr::bind(NameBinder& binder) {
    for (auto& in : ins)
        binder.bind(*in.expr);
    for (auto& out : outs)
        binder.bind(*out.expr);
}

void ErrorExpr::bind(NameBinder&) {}

// Patterns ------------------------------------------------------------------------

void TypedPtrn::bind(NameBinder& binder) {
    if (ptrn) binder.bind(*ptrn);
    binder.bind(*type);
}

void IdPtrn::bind(NameBinder& binder) {
    binder.bind(*decl);
    if (sub_ptrn)
        binder.bind(*sub_ptrn);
}

void LiteralPtrn::bind(NameBinder&) {}

void FieldPtrn::bind(NameBinder& binder) {
    if (ptrn) binder.bind(*ptrn);
}

void RecordPtrn::bind(NameBinder& binder) {
    binder.bind(path);
    for (auto& field : fields) binder.bind(*field);
}

void CtorPtrn::bind(NameBinder& binder) {
    binder.bind(path);
    if (arg) binder.bind(*arg);
}

void TuplePtrn::bind(NameBinder& binder) {
    for (auto& arg : args) binder.bind(*arg);
}

void ArrayPtrn::bind(NameBinder& binder) {
    for (auto& elem : elems) binder.bind(*elem);
}

void ErrorPtrn::bind(NameBinder&) {}

// Declarations --------------------------------------------------------------------

void TypeParam::bind(NameBinder& binder) {
    binder.insert_symbol(*this);
}

void TypeParamList::bind(NameBinder& binder) {
    for (auto& param : params) binder.bind(*param);
}

void PtrnDecl::bind(NameBinder& binder) {
    binder.insert_symbol(*this);
}

void LetDecl::bind(NameBinder& binder) {
    if (init) binder.bind(*init);
    binder.bind(*ptrn);
}

void StaticDecl::bind_head(NameBinder& binder) {
    binder.insert_symbol(*this);
}

void StaticDecl::bind(NameBinder& binder) {
    if (type) binder.bind(*type);
    if (init) binder.bind(*init);
}

void FnDecl::bind_head(NameBinder& binder) {
    binder.insert_symbol(*this);
}

void FnDecl::bind(NameBinder& binder) {
    binder.push_scope();
    if (type_params)
        binder.bind(*type_params);

    if (fn->body)
        binder.bind(*fn);
    else {
        binder.bind(*fn->param);
        if (fn->ret_type)
            binder.bind(*fn->ret_type);
    }
    binder.pop_scope();
}

void FieldDecl::bind(NameBinder& binder) {
    binder.bind(*type);
    if (init)
        binder.bind(*init);
}

void StructDecl::bind_head(NameBinder& binder) {
    binder.insert_symbol(*this);
}

void StructDecl::bind(NameBinder& binder) {
    binder.push_scope();
    if (type_params) binder.bind(*type_params);
    for (auto& field : fields) binder.bind(*field);
    binder.pop_scope();
}

void OptionDecl::bind(NameBinder& binder) {
    if (param) binder.bind(*param);
    else {
        for (auto& field : fields)
            binder.bind(*field);
    }
    binder.insert_symbol(*this);
}

void EnumDecl::bind_head(NameBinder& binder) {
    binder.insert_symbol(*this);
}

void EnumDecl::bind(NameBinder& binder) {
    binder.push_scope();
    if (type_params) binder.bind(*type_params);
    for (auto& option : options) {
        option->parent = this;
        binder.bind(*option);
    }
    binder.pop_scope();
}

void TypeDecl::bind_head(NameBinder& binder) {
    binder.insert_symbol(*this);
}

void TypeDecl::bind(NameBinder& binder) {
    binder.push_scope();
    if (type_params) binder.bind(*type_params);
    binder.bind(*aliased_type);
    binder.pop_scope();
}

void ModDecl::bind_head(NameBinder& binder) {
    if (id.name != "")
        binder.insert_symbol(*this);
}

void ModDecl::bind(NameBinder& binder) {
    // Symbols defined outside the module are not visible inside it.
    std::vector<SymbolTable> old;
    std::swap(binder.scopes_, old);
    binder.push_scope();
    for (auto& decl : decls) binder.bind_head(*decl);
    for (auto& decl : decls) binder.bind(*decl);
    std::swap(binder.scopes_, old);
}

void ErrorDecl::bind(NameBinder&) {}

} // namespace ast

} // namespace artic
