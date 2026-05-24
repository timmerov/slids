#include "grammar.h"

#include "diagnostic.h"
#include "parse.h"
#include "token.h"

namespace grammar {

void run(token::List const& in, parse::Tree& out, diagnostic::Sink& diag) {
    (void)in;
    (void)diag;
    parse::addProgram(out);
}

}  // namespace grammar
