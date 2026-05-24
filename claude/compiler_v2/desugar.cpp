#include "desugar.h"

#include "ast.h"
#include "diagnostic.h"
#include "parse.h"

namespace desugar {

void run(parse::Tree const& in, ast::Tree& out, diagnostic::Sink& diag) {
    (void)in;
    (void)diag;
    ast::addProgram(out);
}

}  // namespace desugar
