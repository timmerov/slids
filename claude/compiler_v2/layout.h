#pragma once

namespace ast { struct Tree; }
namespace diagnostic { struct Sink; }

namespace layout {

void run(ast::Tree& tree, diagnostic::Sink& diag);

}  // namespace layout
