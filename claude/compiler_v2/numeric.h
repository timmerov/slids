#pragma once

namespace token { struct List; }
namespace diagnostic { struct Sink; }

namespace numeric {

void run(token::List& tokens, diagnostic::Sink& diag);

}  // namespace numeric
