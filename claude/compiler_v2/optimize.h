#pragma once

namespace ast { struct Tree; }
namespace diagnostic { struct Sink; }

namespace optimize {

void run(ast::Tree& tree, diagnostic::Sink& diag);

}  // namespace optimize
