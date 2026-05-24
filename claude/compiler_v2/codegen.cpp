#include "codegen.h"

#include <ostream>

#include "ast.h"
#include "diagnostic.h"

namespace codegen {

void run(ast::Tree const& tree, std::ostream& out, diagnostic::Sink& diag) {
    (void)tree;
    (void)diag;
    out << "target triple = \"x86_64-pc-linux-gnu\"\n"
        << "\n"
        << "define i32 @main() {\n"
        << "  ret i32 0\n"
        << "}\n";
}

}  // namespace codegen
