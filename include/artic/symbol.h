#ifndef ARTIC_SYMBOL_H
#define ARTIC_SYMBOL_H

#include <unordered_map>
#include <type_traits>
#include <memory>
#include <vector>
#include <string>

namespace artic {

namespace ast {
    struct NamedDecl;
}

/// Declaration site of a symbol.
struct Symbol {
    Symbol(ast::NamedDecl* decl) : decls{ decl } {}
    // TODO decls seems to be only used as an option type (0 or 1 element), is there a reason to use a vec instead ? Also this ctor is dead code
    // Symbol(std::vector<ast::NamedDecl*>&& decls) : decls(std::move(decls)) {}
    virtual ~Symbol() {}

    std::vector<ast::NamedDecl*> decls;
};

/// Table containing a map from symbol name to declaration site.
struct SymbolTable {
    bool top_level;
    std::unordered_map<std::string, std::shared_ptr<Symbol>> symbols;

    SymbolTable(bool top_level = false)
        : top_level(top_level)
    {}

    std::shared_ptr<Symbol> find(const std::string& name) {
        auto it = symbols.find(name);
        if (it != symbols.end()) return it->second;
        return nullptr;
    }

    template <typename T, typename DistanceFn>
    std::pair<T, std::shared_ptr<Symbol>> find_similar(const std::string& name, T min, DistanceFn distance) {
        std::shared_ptr<Symbol> best;
        for (auto& symbol : symbols) {
            auto d = distance(symbol.first, name, min);
            if (d < min) {
                best = symbol.second;
                min  = d;
            }
        }
        return std::make_pair(min, best);
    }

    bool insert(const std::string& name, Symbol&& symbol) {
        auto it = symbols.find(name);
        if (it != symbols.end()) {
            auto& exprs = it->second->decls;
            exprs.insert(exprs.end(), symbol.decls.begin(), symbol.decls.end());
            return false;
        }

        symbols.emplace(name, std::make_shared<Symbol>(std::move(symbol)));
        return true;
    }
};

} // namespace artic

#endif // ARTIC_SYMBOL_H
