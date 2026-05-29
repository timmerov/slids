#include "resolve.h"

#include <cassert>
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
        diagnostic::report(diag, {s.file_id, s.tok,
            "'" + s.name + "' is a variable, not a function.",
            {{entry.file_id, entry.tok, "variable declared here"}}});
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
                requireKnownType(s.return_type, s.file_id, s.tok, diag);
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
        requireKnownType(ch->return_type, ch->file_id, ch->tok, diag);
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

    // Pass 1a — collect entries at program scope WITHOUT walking init
    // expressions. This lets globals reference each other regardless of
    // declaration order (forward-decl semantics).
    for (auto& ch : program->children) {
        if (!ch) continue;
        if (ch->kind == parse::Kind::kFunctionDef
         || ch->kind == parse::Kind::kFunctionDecl) {
            requireKnownType(ch->return_type, ch->file_id, ch->tok, diag);
            std::vector<std::string> param_types;
            for (auto& p : ch->params) {
                if (!p) continue;
                requireKnownType(p->return_type, p->file_id, p->tok, diag);
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
            requireKnownType(ch->return_type, ch->file_id, ch->tok, diag);
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
