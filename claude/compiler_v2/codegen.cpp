#include "codegen.h"

#include <ostream>

#include "ast.h"
#include "diagnostic.h"

namespace codegen {

void run(ast::Tree const& tree, std::ostream& out, diagnostic::Sink& diag) {
    (void)tree;
    (void)out;
    (void)diag;
    // TODO
}

}  // namespace codegen
