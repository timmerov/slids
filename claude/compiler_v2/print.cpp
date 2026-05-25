#include "print.h"

#include <cstdio>
#include <functional>
#include <ostream>
#include <string>
#include <utility>

#include "ast.h"

namespace print {

namespace {

bool isPrintIntrinsic(std::string const& name) {
    return name == "__println" || name == "__print";
}

void emitStringConstant(std::ostream& out, int id, std::string const& text) {
    out << "@.str_" << id << " = private unnamed_addr constant ["
        << (text.size() + 1) << " x i8] c\"";
    for (unsigned char c : text) {
        if (c == '\\')      out << "\\\\";
        else if (c == '"')  out << "\\22";
        else if (c >= 0x20 && c < 0x7F) out << static_cast<char>(c);
        else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\%02X", static_cast<unsigned>(c));
            out << buf;
        }
    }
    out << "\\00\"\n";
}

}  // namespace

State collect(ast::Tree const& tree) {
    State state;
    std::function<void(ast::Node const&)> walk = [&](ast::Node const& n) {
        if (n.kind == ast::Kind::kCallStmt && isPrintIntrinsic(n.name)) {
            if (n.children.size() == 1
                && n.children[0]->kind == ast::Kind::kStringLiteral) {
                int id = static_cast<int>(state.str_texts.size());
                state.call_to_str_id[&n] = id;
                std::string text = n.children[0]->text;
                if (n.name == "__println") text += '\n';
                state.str_texts.push_back(std::move(text));
            }
        }
        for (auto const& c : n.children) walk(*c);
    };
    for (auto const& n : tree.nodes) walk(*n);
    return state;
}

void emitConstants(State const& state, std::ostream& out) {
    for (int i = 0; i < static_cast<int>(state.str_texts.size()); i++) {
        emitStringConstant(out, i, state.str_texts[i]);
    }
    if (!state.str_texts.empty()) out << "\n";
    out << "declare i32 @printf(ptr, ...)\n\n";
}

bool tryEmitCall(ast::Node const& call_node, State const& state, std::ostream& out) {
    if (!isPrintIntrinsic(call_node.name)) return false;
    auto it = state.call_to_str_id.find(&call_node);
    if (it == state.call_to_str_id.end()) return false;
    out << "  call i32 (ptr, ...) @printf(ptr @.str_" << it->second << ")\n";
    return true;
}

}  // namespace print
