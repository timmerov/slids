#pragma once

#include <vector>

#include "widen.h"

namespace parse { struct Tree; struct Node; }
namespace diagnostic { struct Sink; }

namespace resolve {

void run(parse::Tree& tree, diagnostic::Sink& diag);

// Instantiate the function template `tmpl_entry_id` with `args` (one bound type per
// template-list name, in order). Memoized: a repeat binding returns the existing
// instance entry id with `created` false. A new binding clones the pristine
// definition, re-enters resolution under the template's definition-point scope
// snapshot (with each type parameter aliased to its bound type), munges the params,
// and parks the instance node in tree.pending_tmpl_instances; `created` comes back
// true and `instance_node` points at it — the caller (classify) owns running the
// remaining stages (constfold + classify) over it and splicing it in at walk end.
// Returns -1 on failure (unknown template / registration had errored).
int instantiateTemplate(parse::Tree& tree, int tmpl_entry_id,
                        std::vector<widen::TypeRef> const& args,
                        int file_id, int tok, diagnostic::Sink& diag,
                        bool& created, parse::Node*& instance_node);

}  // namespace resolve
