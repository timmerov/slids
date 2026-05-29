#pragma once

namespace parse { struct Tree; }
namespace diagnostic { struct Sink; }

namespace resolve {

void run(parse::Tree& tree, diagnostic::Sink& diag);

}  // namespace resolve
