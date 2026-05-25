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
        if (n.kind == ast::Kind::kCallStmt && isPrintIntrinsic(n.name)
            && n.children.size() == 1) {
            ast::Node const& arg = *n.children[0];
            if (arg.kind == ast::Kind::kStringLiteral) {
                std::string text = arg.text;
                if (n.name == "__println") text += '\n';
                int id = strings::add(pool, std::move(text));
                cs.call_to_str_id[&n] = id;
            } else if (arg.kind == ast::Kind::kIdentExpr) {
                std::string fmt = "%s";
                if (n.name == "__println") fmt += '\n';
                int id = strings::add(pool, std::move(fmt));
                cs.call_to_fmt_id[&n] = id;
            }
        }
        for (auto const& c : n.children) walk(*c);
    };
    for (auto const& n : tree.nodes) walk(*n);
    return cs;
}

bool tryEmitCall(ast::Node const& call_node, CallStrings const& cs,
                 codegen::SymTab const& syms, std::ostream& out) {
    if (!isPrintIntrinsic(call_node.name)) return false;

    auto str_it = cs.call_to_str_id.find(&call_node);
    if (str_it != cs.call_to_str_id.end()) {
        out << "  call i32 (ptr, ...) @printf(ptr @.str_" << str_it->second << ")\n";
        return true;
    }

    auto fmt_it = cs.call_to_fmt_id.find(&call_node);
    if (fmt_it != cs.call_to_fmt_id.end()) {
        std::string const& ident = call_node.children[0]->name;
        auto sym = syms.find(ident);
        if (sym == syms.end()) return false;
        static int tmp_counter = 0;
        std::string tmp = std::string("%pt_") + std::to_string(tmp_counter++);
        out << "  " << tmp << " = load " << sym->second.llvm_type
            << ", ptr " << sym->second.alloca_name << "\n";
        out << "  call i32 (ptr, ...) @printf(ptr @.str_" << fmt_it->second
            << ", " << sym->second.llvm_type << " " << tmp << ")\n";
        return true;
    }

    return false;
}

}  // namespace print
