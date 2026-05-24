#pragma once

#include <iosfwd>

namespace ast { struct Tree; }
namespace diagnostic { struct Sink; }

namespace codegen {

void run(ast::Tree const& tree, std::ostream& out, diagnostic::Sink& diag);

}  // namespace codegen
