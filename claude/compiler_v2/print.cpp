#include "print.h"

#include <functional>
#include <ostream>
#include <string>
#include <utility>

#include "ast.h"
#include "strings.h"

namespace print {

namespace {

bool isPrintIntrinsic(std::string const& name) {
    return name == "__println" || name == "__print";
}

}  // namespace

CallStrings collect(ast::Tree const& tree, strings::Pool& pool) {
    CallStrings cs;
    std::function<void(ast::Node const&)> walk = [&](ast::Node const& n) {
        if (n.kind == ast::Kind::kCallStmt && isPrintIntrinsic(n.name)) {
            if (n.children.size() == 1
                && n.children[0]->kind == ast::Kind::kStringLiteral) {
                std::string text = n.children[0]->text;
                if (n.name == "__println") text += '\n';
                int id = strings::add(pool, std::move(text));
                cs.call_to_str_id[&n] = id;
            }
        }
        for (auto const& c : n.children) walk(*c);
    };
    for (auto const& n : tree.nodes) walk(*n);
    return cs;
}

bool tryEmitCall(ast::Node const& call_node, CallStrings const& cs, std::ostream& out) {
    if (!isPrintIntrinsic(call_node.name)) return false;
    auto it = cs.call_to_str_id.find(&call_node);
    if (it == cs.call_to_str_id.end()) return false;
    out << "  call i32 (ptr, ...) @printf(ptr @.str_" << it->second << ")\n";
    return true;
}

}  // namespace print
