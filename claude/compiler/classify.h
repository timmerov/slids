#pragma once

namespace parse { struct Tree; }
namespace diagnostic { struct Sink; }

namespace classify {

void run(parse::Tree& tree, diagnostic::Sink& diag);

}  // namespace classify
