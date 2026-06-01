#include "desugar.h"

#include <cassert>
#include <memory>
#include <string>

#include "ast.h"
#include "diagnostic.h"
#include "parse.h"

namespace desugar {

namespace {

ast::Kind toAstKind(parse::Kind k) {
    switch (k) {
        case parse::Kind::kProgram:       return ast::Kind::kProgram;
        case parse::Kind::kFunctionDef:   return ast::Kind::kFunctionDef;
        case parse::Kind::kFunctionDecl:  return ast::Kind::kFunctionDecl;
        case parse::Kind::kVarDeclStmt:   return ast::Kind::kVarDeclStmt;
        case parse::Kind::kAssignStmt:    return ast::Kind::kAssignStmt;
        case parse::Kind::kAugAssignStmt: return ast::Kind::kAugAssignStmt;
        case parse::Kind::kCallStmt:      return ast::Kind::kCallStmt;
        case parse::Kind::kCallExpr:      return ast::Kind::kCallExpr;
        case parse::Kind::kExprStmt:      return ast::Kind::kExprStmt;
        case parse::Kind::kAliasDecl:
            // Consumed by resolve (types substituted); never copied to the ast.
            assert(false && "toAstKind: alias should be dropped before copy");
            __builtin_unreachable();
        case parse::Kind::kNamespaceDecl:
            // Consumed by resolve; members are hoisted, the wrapper dropped.
            assert(false && "toAstKind: namespace should be dropped before copy");
            __builtin_unreachable();
        case parse::Kind::kEnumDecl:
            // Consumed by resolve (lowered to alias+namespace+consts / bare
            // consts); members folded by constfold. The node is dropped on copy.
            assert(false && "toAstKind: enum should be dropped before copy");
            __builtin_unreachable();
        case parse::Kind::kReturnStmt:    return ast::Kind::kReturnStmt;
        case parse::Kind::kBlockStmt:     return ast::Kind::kBlockStmt;
        case parse::Kind::kStringLiteral: return ast::Kind::kStringLiteral;
        case parse::Kind::kIntLiteral:    return ast::Kind::kIntLiteral;
        case parse::Kind::kUintLiteral:   return ast::Kind::kUintLiteral;
        case parse::Kind::kCharLiteral:   return ast::Kind::kCharLiteral;
        case parse::Kind::kBoolLiteral:   return ast::Kind::kBoolLiteral;
        case parse::Kind::kFloatLiteral:  return ast::Kind::kFloatLiteral;
        case parse::Kind::kIdentExpr:     return ast::Kind::kIdentExpr;
        case parse::Kind::kUnaryExpr:     return ast::Kind::kUnaryExpr;
        case parse::Kind::kBinaryExpr:    return ast::Kind::kBinaryExpr;
        case parse::Kind::kPreIncExpr:    return ast::Kind::kPreIncExpr;
        case parse::Kind::kPostIncExpr:   return ast::Kind::kPostIncExpr;
        case parse::Kind::kParam:         return ast::Kind::kParam;
    }
    assert(false && "toAstKind: unhandled parse::Kind");
    __builtin_unreachable();
}

// Rewrite `lhs op= rhs;` into `lhs = lhs op rhs;`. Only fires when `node` is
// a kAugAssignStmt. Lvalue is a bare ident today — when complex lvalues land
// (`arr[f()] += 1`), bind the lhs to a tmp here to avoid double-evaluation.
// Classify stamped: node.return_type = lvalue type, node.inferred_type = opty.
std::unique_ptr<ast::Node> tryDesugarAugAssign(ast::Node& node) {
    if (node.kind != ast::Kind::kAugAssignStmt) return nullptr;
    assert(node.children.size() == 1
        && "tryDesugarAugAssign: AugAssignStmt needs 1 rhs child");

    auto lhs_ref = std::make_unique<ast::Node>();
    lhs_ref->kind = ast::Kind::kIdentExpr;
    lhs_ref->name = node.name;
    lhs_ref->inferred_type = node.return_type;
    lhs_ref->resolved_entry_id = node.resolved_entry_id;
    lhs_ref->file_id = node.file_id;
    lhs_ref->tok = node.tok;

    auto binop = std::make_unique<ast::Node>();
    binop->kind = ast::Kind::kBinaryExpr;
    binop->text = node.text;
    binop->inferred_type = node.inferred_type;
    binop->op_type = node.op_type;
    binop->file_id = node.file_id;
    binop->tok = node.tok;
    binop->children.push_back(std::move(lhs_ref));
    binop->children.push_back(std::move(node.children[0]));

    auto out = std::make_unique<ast::Node>();
    out->kind = ast::Kind::kAssignStmt;
    out->name = std::move(node.name);
    out->resolved_entry_id = node.resolved_entry_id;
    out->file_id = node.file_id;
    out->tok = node.tok;
    out->children.push_back(std::move(binop));
    return out;
}

// The LLVM symbol for a function reference. File-scope functions keep their bare
// name; a namespace member is disambiguated by its entry id (computed identically
// at the definition and every call site from the same resolved_entry_id — never
// stored as a canonical-name string). Two namespaces may both define `foo`; this
// keeps their symbols distinct without a scope-path string.
std::string functionSymbol(parse::Node const& p, parse::Tree const& tree) {
    if (p.resolved_entry_id < 0) return p.name;
    parse::Entry const& e = tree.entries[p.resolved_entry_id];
    if (e.kind != parse::EntryKind::kFunction || e.owner_ns_frame < 0) {
        return p.name;
    }
    return e.name + "." + std::to_string(p.resolved_entry_id);
}

std::unique_ptr<ast::Node> copyNode(parse::Node const& p, parse::Tree const& tree) {
    auto node = std::make_unique<ast::Node>();
    node->kind = toAstKind(p.kind);
    node->name = p.name;
    node->text = p.text;
    node->return_type = p.return_type;
    node->nominal_type = p.nominal_type;
    node->inferred_type = p.inferred_type;
    node->op_type = p.op_type;
    node->file_id = p.file_id;
    node->tok = p.tok;
    node->name_tok = p.name_tok;
    node->resolved_entry_id = p.resolved_entry_id;
    node->is_const = p.is_const;
    node->param_types = p.param_types;
    // Function definitions and calls (including qualified `Space:bar()`) resolve
    // to their entry-id-derived symbol; the qualifier is dropped (ast carries no
    // qualifier — a flat symbol replaces it).
    if (p.kind == parse::Kind::kFunctionDef
     || p.kind == parse::Kind::kFunctionDecl
     || p.kind == parse::Kind::kCallExpr
     || p.kind == parse::Kind::kCallStmt) {
        node->name = functionSymbol(p, tree);
    }
    for (auto const& c : p.children) {
        if (c->kind == parse::Kind::kAliasDecl) continue;      // resolve-only
        if (c->kind == parse::Kind::kNamespaceDecl) continue;  // members hoisted
        if (c->kind == parse::Kind::kEnumDecl) continue;       // resolve-lowered
        node->children.push_back(copyNode(*c, tree));
    }
    for (auto const& pp : p.params) {
        node->params.push_back(copyNode(*pp, tree));
    }
    if (auto rewritten = tryDesugarAugAssign(*node)) return rewritten;
    return node;
}

// Collect a namespace's member function definitions (recursing into nested
// namespaces) so they can be hoisted to program scope.
void collectNamespaceFunctions(parse::Node const& ns,
                               std::vector<parse::Node const*>& fns) {
    for (auto const& m : ns.children) {
        if (!m) continue;
        if (m->kind == parse::Kind::kFunctionDef) {
            fns.push_back(m.get());
        } else if (m->kind == parse::Kind::kNamespaceDecl) {
            collectNamespaceFunctions(*m, fns);
        }
    }
}

// Find the outermost namespace decls reachable from `node`: at file scope and
// inside function bodies (a namespace may be opened in either). Nesting is left
// to collectNamespaceFunctions, so this does not descend into namespaces.
void findTopNamespaces(parse::Node const& node,
                       std::vector<parse::Node const*>& out) {
    for (auto const& c : node.children) {
        if (!c) continue;
        if (c->kind == parse::Kind::kNamespaceDecl) {
            out.push_back(c.get());
        } else if (c->kind == parse::Kind::kFunctionDef) {
            findTopNamespaces(*c, out);
        }
    }
}

// ---- PPID lowering -------------------------------------------------------
// ++/-- are extracted from the phrase they sit in: pre-bumps fire at the
// phrase start, post-bumps at its end. Each inc/dec becomes a read of its
// operand, and the bump is emitted as a kBumpExpr. A phrase carrying bumps is
// wrapped in a kSeqExpr {pre... value post...} (value_index = #pre), so a
// conditionally-evaluated phrase (the rhs of && / ||) keeps its bumps inside
// the subtree that only runs when that side is reached.

std::unique_ptr<ast::Node> makeBump(ast::Node const& operand,
                                    std::string const& op) {
    auto b = std::make_unique<ast::Node>();
    b->kind = ast::Kind::kBumpExpr;
    b->text = op;
    b->resolved_entry_id = operand.resolved_entry_id;
    b->inferred_type = operand.inferred_type;
    b->file_id = operand.file_id;
    b->tok = operand.tok;
    return b;
}

void lowerPhraseSlot(std::unique_ptr<ast::Node>& slot);

// Walk an expression that belongs to the CURRENT phrase: collect its bumps into
// pre/post, replace each inc/dec with a read of the operand. Sub-phrase
// children — call args and the rhs of && / || — recurse via lowerPhraseSlot.
void lowerInPhrase(std::unique_ptr<ast::Node>& slot,
                   std::vector<std::unique_ptr<ast::Node>>& pre,
                   std::vector<std::unique_ptr<ast::Node>>& post) {
    if (!slot) return;
    ast::Node& n = *slot;
    if (n.kind == ast::Kind::kPreIncExpr || n.kind == ast::Kind::kPostIncExpr) {
        auto operand = std::move(n.children[0]);   // ident lvalue (resolve checked)
        auto bump = makeBump(*operand, n.text);
        if (n.kind == ast::Kind::kPreIncExpr) pre.push_back(std::move(bump));
        else                                  post.push_back(std::move(bump));
        slot = std::move(operand);                 // the read replaces the inc/dec
        return;
    }
    if (n.kind == ast::Kind::kCallExpr) {
        for (auto& arg : n.children) lowerPhraseSlot(arg);
        return;
    }
    if (n.kind == ast::Kind::kBinaryExpr && (n.text == "&&" || n.text == "||")) {
        lowerInPhrase(n.children[0], pre, post);   // lhs: same phrase
        lowerPhraseSlot(n.children[1]);            // rhs: own conditional phrase
        return;
    }
    // Every other interior node (arith/bitwise/^^/unary) is part of this
    // phrase; leaves have no children, so the loop is a no-op for them.
    // seq/bump are synthesized by this pass and must never be re-walked.
    assert(n.kind != ast::Kind::kSeqExpr && n.kind != ast::Kind::kBumpExpr
        && "lowerInPhrase: re-walked a synthesized PPID node");
    for (auto& c : n.children) lowerInPhrase(c, pre, post);
}

// Lower the phrase rooted at `slot`; wrap it in a seq iff it carried bumps.
void lowerPhraseSlot(std::unique_ptr<ast::Node>& slot) {
    if (!slot) return;
    std::vector<std::unique_ptr<ast::Node>> pre, post;
    lowerInPhrase(slot, pre, post);
    if (pre.empty() && post.empty()) return;
    auto seq = std::make_unique<ast::Node>();
    seq->kind = ast::Kind::kSeqExpr;
    seq->inferred_type = slot->inferred_type;
    seq->file_id = slot->file_id;
    seq->tok = slot->tok;
    seq->value_index = static_cast<int>(pre.size());
    for (auto& b : pre) seq->children.push_back(std::move(b));
    seq->children.push_back(std::move(slot));
    for (auto& b : post) seq->children.push_back(std::move(b));
    slot = std::move(seq);
}

std::unique_ptr<ast::Node> makeBumpStmt(std::unique_ptr<ast::Node> bump) {
    auto stmt = std::make_unique<ast::Node>();
    stmt->kind = ast::Kind::kExprStmt;
    stmt->file_id = bump->file_id;
    stmt->tok = bump->tok;
    stmt->children.push_back(std::move(bump));
    return stmt;
}

// Lower one statement's phrases. The statement itself is a phrase, so its
// statement-level bumps are returned in pre/post for the caller to splice as
// sibling statements around it — a post-inc of the store target must fire
// AFTER the store (`a = a++` leaves a one larger), not inside the rhs. Bumps
// inside a sub-phrase (call args, the rhs of && / ||) stay in a seq, since that
// phrase genuinely exits before the store. A return embeds its bump: there is
// no slot after a terminator, and its value is read before the bump anyway.
void lowerStatementPPID(ast::Node& stmt,
                        std::vector<std::unique_ptr<ast::Node>>& pre,
                        std::vector<std::unique_ptr<ast::Node>>& post) {
    if (stmt.kind == ast::Kind::kVarDeclStmt
        || stmt.kind == ast::Kind::kAssignStmt) {
        if (!stmt.children.empty() && stmt.children[0]) {
            lowerInPhrase(stmt.children[0], pre, post);
        }
    } else if (stmt.kind == ast::Kind::kReturnStmt) {
        if (!stmt.children.empty() && stmt.children[0]) {
            lowerPhraseSlot(stmt.children[0]);
        }
    } else if (stmt.kind == ast::Kind::kCallStmt) {
        for (auto& arg : stmt.children) lowerPhraseSlot(arg);
    } else if (stmt.kind == ast::Kind::kExprStmt) {
        // The value is discarded. A bare inc/dec needs only its bump — no read.
        auto& child = stmt.children[0];
        if (child->kind == ast::Kind::kPreIncExpr
            || child->kind == ast::Kind::kPostIncExpr) {
            child = makeBump(*child->children[0], child->text);
        } else {
            lowerPhraseSlot(child);
        }
    }
}

// Run PPID statement-bump splicing over a statement list, rebuilding it with
// pre-bumps before / post-bumps after each statement. Recurses into a nested
// kBlockStmt so bumps inside a block splice within that block, not at the
// enclosing scope.
void lowerStatementList(std::vector<std::unique_ptr<ast::Node>>& stmts) {
    std::vector<std::unique_ptr<ast::Node>> lowered;
    for (auto& stmt : stmts) {
        if (!stmt) continue;
        if (stmt->kind == ast::Kind::kBlockStmt) {
            lowerStatementList(stmt->children);
            lowered.push_back(std::move(stmt));
            continue;
        }
        std::vector<std::unique_ptr<ast::Node>> pre, post;
        lowerStatementPPID(*stmt, pre, post);
        for (auto& b : pre) lowered.push_back(makeBumpStmt(std::move(b)));
        lowered.push_back(std::move(stmt));
        for (auto& b : post) lowered.push_back(makeBumpStmt(std::move(b)));
    }
    stmts = std::move(lowered);
}

}  // namespace

void run(parse::Tree const& in, ast::Tree& out, diagnostic::Sink& diag) {
    (void)diag;
    for (auto const& n : in.nodes) {
        out.nodes.push_back(copyNode(*n, in));
    }
    // Hoist namespace member functions to program scope as plain (symbol-renamed)
    // functions; the namespace wrappers were dropped by copyNode. Member consts
    // were substituted away by constfold and need no runtime form.
    for (std::size_t i = 0; i < in.nodes.size(); i++) {
        if (!in.nodes[i] || in.nodes[i]->kind != parse::Kind::kProgram) continue;
        ast::Node* prog = out.nodes[i].get();
        std::vector<parse::Node const*> nss;
        findTopNamespaces(*in.nodes[i], nss);
        for (parse::Node const* ns : nss) {
            std::vector<parse::Node const*> fns;
            collectNamespaceFunctions(*ns, fns);
            for (parse::Node const* f : fns) {
                prog->children.push_back(copyNode(*f, in));
            }
        }
    }
    // PPID lowering: walk each function body's statements, extract ++/--, and
    // splice statement-level pre/post bumps as sibling statements around each
    // statement (post-bumps land after the statement's store).
    for (auto& n : out.nodes) {
        if (!n || n->kind != ast::Kind::kProgram) continue;
        for (auto& fn : n->children) {
            if (!fn || fn->kind != ast::Kind::kFunctionDef) continue;
            lowerStatementList(fn->children);
        }
    }
}

}  // namespace desugar
