#include "resolve.h"

#include <cassert>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "diagnostic.h"
#include "parse.h"
#include "widen.h"

namespace resolve {

namespace {

bool isPrintIntrinsic(std::string const& name) {
    return name == "__println" || name == "__print";
}

// Reject the declared / return / parameter type if it's not a known spelling.
// Caret points at the construct's own tok (the type-name token's position),
// which together with surrounding source context tells the user where the
// unknown name appears.
void requireKnownType(std::string const& t, int file_id, int tok,
                      diagnostic::Sink& diag) {
    if (widen::isKnownType(t)) return;
    diagnostic::report(diag, {file_id, tok,
        "Unknown type '" + t + "'.", {}});
}

// Substitute a type-alias spelling to its underlying type, following chains
// (`alias A = B; alias B = int` → `int`) and detecting cycles. The `[]` suffix
// rides along. A spelling that isn't an alias entry is returned unchanged.
std::string resolveTypeSpelling(parse::Tree& tree, std::string const& spelling,
                                std::set<std::string>& visiting, bool& cyclic,
                                int file_id, int tok, diagnostic::Sink& diag) {
    std::string base = spelling;
    std::string suffix;
    while (base.size() >= 2 && base.compare(base.size() - 2, 2, "[]") == 0) {
        suffix += "[]";
        base.resize(base.size() - 2);
    }
    int id = parse::findInLiveScopes(tree, base);
    if (id < 0 || tree.entries[id].kind != parse::EntryKind::kAlias) {
        return spelling;
    }
    if (visiting.count(base)) {
        cyclic = true;
        diagnostic::report(diag, {file_id, tok,
            "Type alias '" + base + "' is part of a cycle.", {}});
        return base;
    }
    visiting.insert(base);
    std::string resolved = resolveTypeSpelling(
        tree, tree.entries[id].slids_type, visiting, cyclic, file_id, tok, diag);
    visiting.erase(base);
    return resolved + suffix;
}

// Rewrite a declared type spelling in place to its underlying type, then
// require the result to be a known type. A cycle has already been reported, so
// skip the redundant "Unknown type" that the broken chain would otherwise emit.
void resolveDeclType(parse::Tree& tree, std::string& spelling,
                     int file_id, int tok, diagnostic::Sink& diag) {
    std::set<std::string> visiting;
    bool cyclic = false;
    spelling = resolveTypeSpelling(tree, spelling, visiting, cyclic,
                                   file_id, tok, diag);
    if (!cyclic) requireKnownType(spelling, file_id, tok, diag);
}

// Register `alias Name = Type;` as a kAlias entry in the current frame.
void registerAlias(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    int existing = parse::findInFrame(tree, parse::currentFrameId(tree), s.name);
    if (existing >= 0) {
        parse::Entry const& prev = tree.entries[existing];
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "Duplicate declaration of '" + s.name + "'.",
            {{prev.file_id, prev.tok, "first declared here"}}});
        return;
    }
    parse::Entry e;
    e.kind = parse::EntryKind::kAlias;
    e.name = s.name;
    e.slids_type = s.return_type;   // target spelling; resolved at use
    e.file_id = s.file_id;
    e.tok = s.name_tok;
    s.resolved_entry_id = parse::addEntry(tree, std::move(e));
}

void resolveExpr(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag);
void resolveUserCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag);

void resolveExpr(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag) {
    switch (e.kind) {
        case parse::Kind::kIdentExpr: {
            int id = parse::findInLiveScopes(tree, e.name);
            if (id < 0) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Unresolved identifier '" + e.name + "'.", {}});
                return;
            }
            if (tree.entries[id].kind == parse::EntryKind::kAlias) {
                parse::Entry const& a = tree.entries[id];
                diagnostic::report(diag, {e.file_id, e.tok,
                    "'" + e.name + "' is a type, not a value.",
                    {{a.file_id, a.tok, "alias declared here"}}});
                return;
            }
            e.resolved_entry_id = id;
            return;
        }
        case parse::Kind::kCallExpr: {
            if (isPrintIntrinsic(e.name)) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "'" + e.name + "' cannot be used as an expression.", {}});
                for (auto& ch : e.children) {
                    if (ch) resolveExpr(tree, *ch, diag);
                }
                return;
            }
            resolveUserCall(tree, e, diag);
            return;
        }
        case parse::Kind::kPreIncExpr:
        case parse::Kind::kPostIncExpr: {
            // Operand must be an assignable variable. Phase 1: a bare ident
            // resolving to a LocalVar (no field / index lvalues yet).
            parse::Node& operand = *e.children[0];
            if (operand.kind != parse::Kind::kIdentExpr) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "The operand of '" + e.text + "' must be a variable.", {}});
                return;
            }
            resolveExpr(tree, operand, diag);
            if (operand.resolved_entry_id >= 0) {
                parse::Entry const& entry = tree.entries[operand.resolved_entry_id];
                if (entry.kind == parse::EntryKind::kFunction) {
                    diagnostic::report(diag, {operand.file_id, operand.tok,
                        "'" + operand.name + "' is a function, not a variable.",
                        {{entry.file_id, entry.tok, "function declared here"}}});
                } else if (entry.kind == parse::EntryKind::kConst) {
                    diagnostic::report(diag, {operand.file_id, operand.tok,
                        "Constant '" + operand.name + "' cannot be incremented.",
                        {{entry.file_id, entry.tok, "constant declared here"}}});
                }
            }
            return;
        }
        case parse::Kind::kUnaryExpr:
        case parse::Kind::kBinaryExpr:
            for (auto& ch : e.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            return;
        case parse::Kind::kStringLiteral:
        case parse::Kind::kIntLiteral:
        case parse::Kind::kUintLiteral:
        case parse::Kind::kCharLiteral:
        case parse::Kind::kBoolLiteral:
        case parse::Kind::kFloatLiteral:
            return;
        case parse::Kind::kProgram:
        case parse::Kind::kFunctionDef:
        case parse::Kind::kFunctionDecl:
        case parse::Kind::kVarDeclStmt:
        case parse::Kind::kAssignStmt:
        case parse::Kind::kAugAssignStmt:
        case parse::Kind::kCallStmt:
        case parse::Kind::kExprStmt:
        case parse::Kind::kAliasDecl:
        case parse::Kind::kReturnStmt:
        case parse::Kind::kParam:
            assert(false && "resolveExpr: not an expression kind");
            return;
    }
}

// Write target (assign / aug-assign lhs) — must point at a LocalVar entry.
bool resolveAssignTarget(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    int id = parse::findInLiveScopes(tree, s.name);
    if (id < 0) {
        diagnostic::report(diag, {s.file_id, s.tok,
            "Cannot assign to undeclared variable '" + s.name + "'.", {}});
        return false;
    }
    parse::Entry const& entry = tree.entries[id];
    if (entry.kind == parse::EntryKind::kFunction) {
        diagnostic::report(diag, {s.file_id, s.tok,
            "Cannot assign to function '" + s.name + "'.",
            {{entry.file_id, entry.tok, "function declared here"}}});
        return false;
    }
    if (entry.kind == parse::EntryKind::kConst) {
        diagnostic::report(diag, {s.file_id, s.tok,
            "Cannot assign to constant '" + s.name + "'.",
            {{entry.file_id, entry.tok, "constant declared here"}}});
        return false;
    }
    s.resolved_entry_id = id;
    return true;
}

// Call target — must point at a Function entry.
bool resolveCallTarget(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    int id = parse::findInLiveScopes(tree, s.name);
    if (id < 0) {
        diagnostic::report(diag, {s.file_id, s.tok,
            "Unknown function '" + s.name + "'.", {}});
        return false;
    }
    parse::Entry const& entry = tree.entries[id];
    if (entry.kind != parse::EntryKind::kFunction) {
        char const* what = entry.kind == parse::EntryKind::kAlias ? "type"
                         : entry.kind == parse::EntryKind::kConst ? "constant"
                         : "variable";
        diagnostic::report(diag, {s.file_id, s.tok,
            "'" + s.name + "' is a " + what + ", not a function.",
            {{entry.file_id, entry.tok, std::string(what) + " declared here"}}});
        return false;
    }
    s.resolved_entry_id = id;
    return true;
}

// User-function call (statement or expression form): resolve the target,
// check arity against the entry's param list, and cache return + param types
// for downstream stages. Then recurse into the argument expressions.
void resolveUserCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    if (resolveCallTarget(tree, s, diag)) {
        parse::Entry const& entry = tree.entries[s.resolved_entry_id];
        if (s.children.size() != entry.param_types.size()) {
            diagnostic::report(diag, {s.file_id, s.tok,
                "Function '" + s.name + "' expects "
                + std::to_string(entry.param_types.size())
                + " arguments, got "
                + std::to_string(s.children.size()) + ".", {}});
        } else {
            s.return_type = entry.slids_type;
            s.param_types = entry.param_types;
        }
    }
    for (auto& ch : s.children) {
        if (ch) resolveExpr(tree, *ch, diag);
    }
}

void resolveStmt(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    switch (s.kind) {
        case parse::Kind::kVarDeclStmt: {
            // Consts in function bodies are pre-created in the forward-decl
            // pre-pass (resolveFunctionBody). If resolved_entry_id is set,
            // entry already exists; skip creation and dup-check.
            if (s.resolved_entry_id < 0) {
                resolveDeclType(tree, s.return_type, s.file_id, s.tok, diag);
                int existing = parse::findInFrame(tree, parse::currentFrameId(tree), s.name);
                if (existing >= 0) {
                    parse::Entry const& prev = tree.entries[existing];
                    diagnostic::report(diag, {s.file_id, s.name_tok,
                        "Duplicate declaration of '" + s.name + "'.",
                        {{prev.file_id, prev.tok, "first declared here"}}});
                } else {
                    parse::Entry e;
                    e.kind = s.is_const ? parse::EntryKind::kConst
                                        : parse::EntryKind::kLocalVar;
                    e.name = s.name;
                    e.slids_type = s.return_type;
                    e.file_id = s.file_id;
                    e.tok = s.name_tok;   // caret at the ident, not at 'const'/type
                    s.resolved_entry_id = parse::addEntry(tree, std::move(e));
                }
            }
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            return;
        }
        case parse::Kind::kAssignStmt: {
            resolveAssignTarget(tree, s, diag);
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            return;
        }
        case parse::Kind::kAugAssignStmt: {
            if (resolveAssignTarget(tree, s, diag)) {
                // Cache lvalue type on the stmt so desugar's synthesized
                // IdentExpr inherits it without re-walking entries.
                s.return_type = parse::entryType(tree, s.resolved_entry_id);
            }
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            return;
        }
        case parse::Kind::kCallStmt: {
            if (isPrintIntrinsic(s.name)) {
                if (s.children.size() != 1) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "'" + s.name + "' takes exactly one argument.", {}});
                }
                for (auto& ch : s.children) {
                    if (ch) resolveExpr(tree, *ch, diag);
                }
            } else {
                resolveUserCall(tree, s, diag);
            }
            return;
        }
        case parse::Kind::kAliasDecl: {
            // Function-scope alias: register in the body frame, then validate
            // the target (forward refs within a body aren't pre-scanned).
            registerAlias(tree, s, diag);
            resolveDeclType(tree, s.return_type, s.file_id, s.tok, diag);
            return;
        }
        case parse::Kind::kExprStmt:
        case parse::Kind::kReturnStmt: {
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            return;
        }
        case parse::Kind::kProgram:
        case parse::Kind::kFunctionDef:
        case parse::Kind::kFunctionDecl:
        case parse::Kind::kStringLiteral:
        case parse::Kind::kIntLiteral:
        case parse::Kind::kUintLiteral:
        case parse::Kind::kCharLiteral:
        case parse::Kind::kBoolLiteral:
        case parse::Kind::kFloatLiteral:
        case parse::Kind::kIdentExpr:
        case parse::Kind::kUnaryExpr:
        case parse::Kind::kBinaryExpr:
        case parse::Kind::kPreIncExpr:
        case parse::Kind::kPostIncExpr:
        case parse::Kind::kCallExpr:
        case parse::Kind::kParam:
            assert(false && "resolveStmt: not a statement kind");
            return;
    }
}

void resolveFunctionBody(parse::Tree& tree, parse::Node& fn,
                         diagnostic::Sink& diag) {
    parse::pushFrame(tree);
    // Params become LocalVar entries in the body frame. Type spellings were
    // already validated in pass 1.
    for (auto& p : fn.params) {
        if (!p) continue;
        int existing = parse::findInFrame(tree, parse::currentFrameId(tree), p->name);
        if (existing >= 0) {
            parse::Entry const& prev = tree.entries[existing];
            diagnostic::report(diag, {p->file_id, p->tok,
                "Duplicate parameter name '" + p->name + "'.",
                {{prev.file_id, prev.tok, "first declared here"}}});
            continue;
        }
        parse::Entry e;
        e.kind = parse::EntryKind::kLocalVar;
        e.name = p->name;
        e.slids_type = p->return_type;
        e.file_id = p->file_id;
        e.tok = p->name_tok;
        p->resolved_entry_id = parse::addEntry(tree, std::move(e));
    }
    // Forward-decl pre-pass for kConst: pre-create entries so const init
    // expressions can reference later-declared consts in the same body.
    // Dup detection is deferred to the main pass below (which emits a single
    // "Duplicate declaration" diagnostic with a multi-source note).
    for (auto& ch : fn.children) {
        if (!ch || ch->kind != parse::Kind::kVarDeclStmt || !ch->is_const) continue;
        if (parse::findInFrame(tree, parse::currentFrameId(tree), ch->name) >= 0) {
            continue;  // pre-pass yields to main-pass dup detection
        }
        resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
        parse::Entry e;
        e.kind = parse::EntryKind::kConst;
        e.name = ch->name;
        e.slids_type = ch->return_type;
        e.file_id = ch->file_id;
        e.tok = ch->name_tok;
        ch->resolved_entry_id = parse::addEntry(tree, std::move(e));
    }
    for (auto& ch : fn.children) {
        if (ch) resolveStmt(tree, *ch, diag);
    }
    parse::popFrame(tree);
}

}  // namespace

void run(parse::Tree& tree, diagnostic::Sink& diag) {
    parse::Node* program = nullptr;
    for (auto& n : tree.nodes) {
        if (n && n->kind == parse::Kind::kProgram) {
            program = n.get();
            break;
        }
    }
    if (!program) return;

    parse::pushFrame(tree);   // program frame

    // Pass 1a-alias — register all file-scope aliases first, so any decl below
    // (in any order) can resolve through them, then validate each target.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kAliasDecl) registerAlias(tree, *ch, diag);
    }
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kAliasDecl) {
            resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
        }
    }

    // Pass 1a — collect entries at program scope WITHOUT walking init
    // expressions. This lets globals reference each other regardless of
    // declaration order (forward-decl semantics).
    for (auto& ch : program->children) {
        if (!ch) continue;
        if (ch->kind == parse::Kind::kFunctionDef
         || ch->kind == parse::Kind::kFunctionDecl) {
            resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
            std::vector<std::string> param_types;
            for (auto& p : ch->params) {
                if (!p) continue;
                resolveDeclType(tree, p->return_type, p->file_id, p->tok, diag);
                param_types.push_back(p->return_type);
            }
            bool is_def = (ch->kind == parse::Kind::kFunctionDef);
            int existing = parse::findInFrame(tree, parse::currentFrameId(tree), ch->name);
            if (existing >= 0) {
                parse::Entry& prev = tree.entries[existing];
                if (prev.slids_type != ch->return_type) {
                    diagnostic::report(diag, {ch->file_id, ch->tok,
                        "Return type '" + ch->return_type
                        + "' does not match earlier declaration's '"
                        + prev.slids_type + "'.",
                        {{prev.file_id, prev.tok, "first declared here"}}});
                    continue;
                }
                if (prev.param_types != param_types) {
                    diagnostic::report(diag, {ch->file_id, ch->tok,
                        "Parameter types do not match earlier declaration.",
                        {{prev.file_id, prev.tok, "first declared here"}}});
                    continue;
                }
                if (is_def && prev.defined) {
                    diagnostic::report(diag, {ch->file_id, ch->tok,
                        "Duplicate definition of '" + ch->name + "'.",
                        {{prev.file_id, prev.tok, "first defined here"}}});
                    continue;
                }
                if (is_def) prev.defined = true;
                ch->resolved_entry_id = existing;
                continue;
            }
            parse::Entry e;
            e.kind = parse::EntryKind::kFunction;
            e.name = ch->name;
            e.slids_type = ch->return_type;
            e.param_types = std::move(param_types);
            e.file_id = ch->file_id;
            e.tok = ch->name_tok;
            e.defined = is_def;
            ch->resolved_entry_id = parse::addEntry(tree, std::move(e));
            continue;
        }
        if (ch->kind == parse::Kind::kVarDeclStmt && ch->is_const) {
            resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
            int existing = parse::findInFrame(tree, parse::currentFrameId(tree), ch->name);
            if (existing >= 0) {
                parse::Entry const& prev = tree.entries[existing];
                diagnostic::report(diag, {ch->file_id, ch->name_tok,
                    "Duplicate declaration of '" + ch->name + "'.",
                    {{prev.file_id, prev.tok, "first declared here"}}});
                continue;
            }
            parse::Entry e;
            e.kind = parse::EntryKind::kConst;
            e.name = ch->name;
            e.slids_type = ch->return_type;
            e.file_id = ch->file_id;
            e.tok = ch->name_tok;
            ch->resolved_entry_id = parse::addEntry(tree, std::move(e));
            continue;
        }
        if (ch->kind == parse::Kind::kAliasDecl) continue;  // handled above
        // Mutable globals + other top-level shapes not supported today.
        // Grammar rejects them; if one slips through, it's a grammar bug.
        diagnostic::report(diag, {ch->file_id, ch->tok,
            "Only function definitions, function declarations, and "
            "constants are allowed at file scope.", {}});
    }

    // Pass 1b — walk each top-level const's init expression to resolve
    // ident references. By now every program-scope entry is in the table,
    // so forward refs between globals resolve cleanly.
    for (auto& ch : program->children) {
        if (!ch || ch->kind != parse::Kind::kVarDeclStmt || !ch->is_const) continue;
        for (auto& init : ch->children) {
            if (init) resolveExpr(tree, *init, diag);
        }
    }

    // Pass 2 — walk each function body.
    for (auto& ch : program->children) {
        if (!ch || ch->kind != parse::Kind::kFunctionDef) continue;
        resolveFunctionBody(tree, *ch, diag);
    }

    parse::popFrame(tree);
}

}  // namespace resolve
