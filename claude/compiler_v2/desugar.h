#pragma once

namespace parse { struct Tree; }
namespace ast { struct Tree; }
namespace diagnostic { struct Sink; }

namespace desugar {

void run(parse::Tree const& in, ast::Tree& out, diagnostic::Sink& diag);

}  // namespace desugar
