#include "classify.h"

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

#include "diagnostic.h"
#include "parse.h"
#include "widen.h"

namespace classify {

namespace {

bool isPrintIntrinsic(std::string const& name) {
    return name == "__println" || name == "__print";
}

bool isLiteralKind(parse::Kind k) {
    return k == parse::Kind::kIntLiteral
        || k == parse::Kind::kUintLiteral
        || k == parse::Kind::kCharLiteral
        || k == parse::Kind::kBoolLiteral
        || k == parse::Kind::kFloatLiteral;
}

bool isFloatType(std::string const& t) {
    return t == "float" || t == "float32" || t == "float64";
}

bool isPtrLikeType(std::string const& t) {
    return t.size() >= 2 && t.substr(t.size() - 2) == "[]";
}

bool isIntegerClass(std::string const& t) {
    widen::TypeKind k;
    if (!widen::classify(t, k)) return false;
    return k.cat != widen::Category::kFloat;
}

bool isNumericType(std::string const& t) {
    widen::TypeKind k;
    return widen::classify(t, k);
}

// `!` and the logical operators truthy-coerce: numerics via cmp-against-zero,
// pointer-like via cmp-against-null. Void (and any future non-value type) is
// rejected here, before codegen.
bool isCoercibleToBool(std::string const& t) {
    return isNumericType(t) || isPtrLikeType(t);
}

// Reject the declared / return type if classify can't recognize it. Caret
// points at the type-name token via the statement / def's own tok.
void requireKnownType(std::string const& t, int file_id, int tok,
                      char const* kind, diagnostic::Sink& diag) {
    if (widen::isKnownType(t)) return;
    diagnostic::report(diag, {file_id, tok,
        std::string(kind) + " type '" + t + "' is not a known type spelling.",
        {}});
}

// Text suitable for widen::intLiteralFits / floatLiteralFits — bool "true"/"false"
// → "1"/"0"; everything else passes through.
std::string literalTextForFit(parse::Node const& n) {
    if (n.kind == parse::Kind::kBoolLiteral) return (n.text == "true") ? "1" : "0";
    return n.text;
}

// Per-kind default when a literal has no usable context. Mirrors codegen's
// defaultLiteralType (will collapse to a single copy once codegen reads
// inferred_type and drops its own).
std::string defaultLiteralType(parse::Node const& n) {
    switch (n.kind) {
        case parse::Kind::kIntLiteral: {
            std::string const& s = n.text;
            bool neg = !s.empty() && s[0] == '-';
            std::string mag_str = neg ? s.substr(1) : s;
            errno = 0;
            char* end = nullptr;
            uint64_t mag = std::strtoull(mag_str.c_str(), &end, 10);
            assert(!mag_str.empty() && end != mag_str.c_str() && *end == '\0'
                && errno != ERANGE
                && "defaultLiteralType: malformed int text from numeric");
            if (neg) {
                if (mag <= static_cast<uint64_t>(INT32_MAX) + 1) return "int32";
                return "int64";
            }
            if (mag <= static_cast<uint64_t>(INT32_MAX)) return "int32";
            if (mag <= static_cast<uint64_t>(INT64_MAX)) return "int64";
            return "uint64";
        }
        case parse::Kind::kUintLiteral: {
            std::string const& s = n.text;
            errno = 0;
            char* end = nullptr;
            uint64_t mag = std::strtoull(s.c_str(), &end, 10);
            assert(!s.empty() && end != s.c_str() && *end == '\0'
                && errno != ERANGE
                && "defaultLiteralType: malformed uint text from numeric");
            if (mag <= static_cast<uint64_t>(UINT32_MAX)) return "uint32";
            return "uint64";
        }
        case parse::Kind::kCharLiteral:  return "char";
        case parse::Kind::kBoolLiteral:  return "bool";
        case parse::Kind::kFloatLiteral: return "float32";
        case parse::Kind::kStringLiteral:
        case parse::Kind::kIdentExpr:
        case parse::Kind::kUnaryExpr:
        case parse::Kind::kBinaryExpr:
        case parse::Kind::kProgram:
        case parse::Kind::kFunctionDef:
        case parse::Kind::kFunctionDecl:
        case parse::Kind::kVarDeclStmt:
        case parse::Kind::kAssignStmt:
        case parse::Kind::kAugAssignStmt:
        case parse::Kind::kCallStmt:
        case parse::Kind::kReturnStmt:
        case parse::Kind::kParam:
            assert(false && "defaultLiteralType: not a literal kind");
            __builtin_unreachable();
    }
    assert(false && "defaultLiteralType: unhandled parse::Kind");
    __builtin_unreachable();
}

bool literalFitsContext(parse::Node const& lit, std::string const& context_type) {
    if (context_type.empty()) return false;
    if (lit.kind == parse::Kind::kFloatLiteral) {
        return widen::floatLiteralFits(lit.text, context_type);
    }
    return widen::intLiteralFits(literalTextForFit(lit), context_type);
}

void inferExpr(parse::Tree& tree, parse::Node& e,
               std::string const& context, diagnostic::Sink& diag);

// Walk a left-leaning '+' chain in a print-intrinsic argument. Each leaf
// segment infers in isolation — '+' here is print's concatenation marker,
// not the arith operator, so commonType across the chain is wrong.
void inferPrintArg(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag) {
    if (e.kind == parse::Kind::kBinaryExpr && e.text == "+"
        && e.children.size() == 2) {
        inferPrintArg(tree, *e.children[0], diag);
        inferPrintArg(tree, *e.children[1], diag);
        return;
    }
    inferExpr(tree, e, "", diag);
}

// Literal-flex preamble for non-shift binaries: when one operand is a literal
// and the other is not, try-flex the literal into the partner's type. Updates
// child.inferred_type in place.
void flexBinaryOperands(parse::Node& lhs, parse::Node& rhs) {
    bool lhs_lit = isLiteralKind(lhs.kind);
    bool rhs_lit = isLiteralKind(rhs.kind);
    if (lhs_lit && !rhs_lit && !rhs.inferred_type.empty()) {
        if (literalFitsContext(lhs, rhs.inferred_type)) {
            lhs.inferred_type = rhs.inferred_type;
        }
    } else if (rhs_lit && !lhs_lit && !lhs.inferred_type.empty()) {
        if (literalFitsContext(rhs, lhs.inferred_type)) {
            rhs.inferred_type = lhs.inferred_type;
        }
    }
}

void inferExpr(parse::Tree& tree, parse::Node& e,
               std::string const& context, diagnostic::Sink& diag) {
    switch (e.kind) {
        case parse::Kind::kIntLiteral:
        case parse::Kind::kUintLiteral:
        case parse::Kind::kCharLiteral:
        case parse::Kind::kBoolLiteral:
        case parse::Kind::kFloatLiteral: {
            if (literalFitsContext(e, context)) {
                e.inferred_type = context;
            } else {
                e.inferred_type = defaultLiteralType(e);
            }
            return;
        }
        case parse::Kind::kStringLiteral: {
            e.inferred_type = "char[]";
            return;
        }
        case parse::Kind::kIdentExpr: {
            int id = parse::findInLiveScopes(tree, e.name);
            if (id < 0) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Unresolved identifier '" + e.name + "'.", {}});
                return;
            }
            e.resolved_entry_id = id;
            e.inferred_type = parse::entryType(tree, id);
            return;
        }
        case parse::Kind::kUnaryExpr: {
            assert(e.children.size() == 1 && "UnaryExpr needs 1 child");
            parse::Node& operand = *e.children[0];
            std::string const& op = e.text;
            if (op == "!") {
                inferExpr(tree, operand, "", diag);
                if (!operand.inferred_type.empty()
                    && !isCoercibleToBool(operand.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Operator '!' is not defined on type '"
                        + operand.inferred_type + "'.", {}});
                }
                e.inferred_type = "bool";
            } else {  // + - ~
                inferExpr(tree, operand, context, diag);
                e.inferred_type = operand.inferred_type;
            }
            return;
        }
        case parse::Kind::kBinaryExpr: {
            assert(e.children.size() == 2 && "BinaryExpr needs 2 children");
            parse::Node& lhs = *e.children[0];
            parse::Node& rhs = *e.children[1];
            std::string const& op = e.text;

            if (op == "&&" || op == "||" || op == "^^") {
                inferExpr(tree, lhs, "", diag);
                inferExpr(tree, rhs, "", diag);
                if (!lhs.inferred_type.empty()
                    && !isCoercibleToBool(lhs.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Operator '" + op + "' is not defined on type '"
                        + lhs.inferred_type + "'.", {}});
                }
                if (!rhs.inferred_type.empty()
                    && !isCoercibleToBool(rhs.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Operator '" + op + "' is not defined on type '"
                        + rhs.inferred_type + "'.", {}});
                }
                e.inferred_type = "bool";
                e.op_type = "bool";
                return;
            }

            if (op == "<<" || op == ">>") {
                inferExpr(tree, lhs, context, diag);
                // Shift count stands alone — flexing into a float lhs would
                // mis-type a small int literal. Codegen handles width mismatch.
                inferExpr(tree, rhs, "", diag);
                if (!lhs.inferred_type.empty()
                    && !isNumericType(lhs.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Shift left-hand side must be numeric; got '"
                        + lhs.inferred_type + "'.", {}});
                }
                if (!rhs.inferred_type.empty()
                    && !isIntegerClass(rhs.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Shift count must be integer-class; got '"
                        + rhs.inferred_type + "'.", {}});
                }
                e.inferred_type = lhs.inferred_type;
                e.op_type = lhs.inferred_type;
                return;
            }

            // Comparison and arith/bitwise: infer each side without context,
            // then literal-flex, then commonType.
            inferExpr(tree, lhs, "", diag);
            inferExpr(tree, rhs, "", diag);
            flexBinaryOperands(lhs, rhs);

            std::string opty;
            bool ok = widen::commonType(lhs.inferred_type, rhs.inferred_type, opty);
            if (!ok) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "No common type for '" + lhs.inferred_type + "' and '"
                    + rhs.inferred_type + "'; use an explicit type conversion.",
                    {}});
                return;
            }

            // Bitwise on float doesn't exist — sharper here than at codegen.
            if ((op == "&" || op == "|" || op == "^") && isFloatType(opty)) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Bitwise '" + op + "' not defined on floating-point type '"
                    + opty + "'.", {}});
                return;
            }

            bool is_cmp = (op == "==" || op == "!=" || op == "<"
                        || op == "<=" || op == ">"  || op == ">=");
            e.inferred_type = is_cmp ? std::string("bool") : opty;
            e.op_type = opty;
            return;
        }
        case parse::Kind::kProgram:
        case parse::Kind::kFunctionDef:
        case parse::Kind::kFunctionDecl:
        case parse::Kind::kVarDeclStmt:
        case parse::Kind::kAssignStmt:
        case parse::Kind::kAugAssignStmt:
        case parse::Kind::kCallStmt:
        case parse::Kind::kReturnStmt:
        case parse::Kind::kParam:
            assert(false && "inferExpr: not an expression kind");
            return;
    }
}

// Resolve a write target (assign or aug-assign lhs). Must point at a LocalVar;
// hitting a Function entry produces a sharper diagnostic than codegen could.
// Returns true when resolution succeeds AND the entry is a LocalVar.
bool resolveAssignTarget(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    int id = parse::findInLiveScopes(tree, s.name);
    if (id < 0) {
        diagnostic::report(diag, {s.file_id, s.tok,
            "Cannot assign to undeclared variable '" + s.name + "'.", {}});
        return false;
    }
    parse::Entry const& entry = tree.entries[id];
    if (entry.kind != parse::EntryKind::kLocalVar) {
        diagnostic::report(diag, {s.file_id, s.tok,
            "Cannot assign to function '" + s.name + "'.",
            {{entry.file_id, entry.tok, "function declared here"}}});
        return false;
    }
    s.resolved_entry_id = id;
    return true;
}

// Resolve a call target. Must point at a Function entry.
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

void classifyStmt(parse::Tree& tree, parse::Node& s,
                  std::string const& fn_return_type,
                  diagnostic::Sink& diag) {
    switch (s.kind) {
        case parse::Kind::kVarDeclStmt: {
            requireKnownType(s.return_type, s.file_id, s.tok, "Variable", diag);
            int existing = parse::findInFrame(tree, parse::currentFrameId(tree), s.name);
            if (existing >= 0) {
                parse::Entry const& prev = tree.entries[existing];
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Duplicate declaration of '" + s.name + "'.",
                    {{prev.file_id, prev.tok, "first declared here"}}});
            } else {
                parse::Entry e;
                e.kind = parse::EntryKind::kLocalVar;
                e.name = s.name;
                e.slids_type = s.return_type;
                e.file_id = s.file_id;
                e.tok = s.tok;
                s.resolved_entry_id = parse::addEntry(tree, std::move(e));
            }
            if (!s.children.empty()) {
                inferExpr(tree, *s.children[0], s.return_type, diag);
            }
            return;
        }
        case parse::Kind::kAssignStmt: {
            std::string lvalue_type;
            if (resolveAssignTarget(tree, s, diag)) {
                lvalue_type = parse::entryType(tree, s.resolved_entry_id);
            }
            for (auto& ch : s.children) {
                if (ch) inferExpr(tree, *ch, lvalue_type, diag);
            }
            return;
        }
        case parse::Kind::kAugAssignStmt: {
            assert(s.children.size() == 1 && "AugAssignStmt needs 1 rhs child");
            parse::Node& rhs = *s.children[0];
            std::string const& op = s.text;

            if (!resolveAssignTarget(tree, s, diag)) {
                // Still walk rhs so any errors inside it are reported.
                inferExpr(tree, rhs, "", diag);
                return;
            }
            std::string lvalue_type = parse::entryType(tree, s.resolved_entry_id);
            // Carry lvalue type forward for desugar's synthesized IdentExpr.
            s.return_type = lvalue_type;

            if (op == "<<" || op == ">>") {
                inferExpr(tree, rhs, "", diag);
                if (!isNumericType(lvalue_type)) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "Shift left-hand side must be numeric; got '"
                        + lvalue_type + "'.", {}});
                }
                if (!rhs.inferred_type.empty()
                    && !isIntegerClass(rhs.inferred_type)) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "Shift count must be integer-class; got '"
                        + rhs.inferred_type + "'.", {}});
                }
                s.inferred_type = lvalue_type;
                s.op_type = lvalue_type;
                return;
            }
            if (op == "&&" || op == "||" || op == "^^") {
                inferExpr(tree, rhs, "", diag);
                if (!isCoercibleToBool(lvalue_type)) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "Operator '" + op + "' is not defined on type '"
                        + lvalue_type + "'.", {}});
                }
                if (!rhs.inferred_type.empty()
                    && !isCoercibleToBool(rhs.inferred_type)) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "Operator '" + op + "' is not defined on type '"
                        + rhs.inferred_type + "'.", {}});
                }
                s.inferred_type = "bool";
                s.op_type = "bool";
                return;
            }
            // arith / bitwise — rhs literal flexes into lvalue's type, then
            // commonType drives the op.
            inferExpr(tree, rhs, lvalue_type, diag);
            std::string opty;
            if (!widen::commonType(lvalue_type, rhs.inferred_type, opty)) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "No common type for '" + lvalue_type + "' and '"
                    + rhs.inferred_type
                    + "'; use an explicit type conversion.", {}});
                return;
            }
            if ((op == "&" || op == "|" || op == "^") && isFloatType(opty)) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Bitwise '" + op + "' not defined on floating-point type '"
                    + opty + "'.", {}});
                return;
            }
            s.inferred_type = opty;
            s.op_type = opty;
            return;
        }
        case parse::Kind::kCallStmt: {
            if (isPrintIntrinsic(s.name)) {
                if (s.children.size() != 1) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "'" + s.name + "' takes exactly one argument.", {}});
                }
                for (auto& ch : s.children) {
                    if (ch) inferPrintArg(tree, *ch, diag);
                }
                return;
            }
            if (!resolveCallTarget(tree, s, diag)) {
                // Still walk args so any errors inside them are reported.
                for (auto& ch : s.children) {
                    if (ch) inferExpr(tree, *ch, "", diag);
                }
                return;
            }
            parse::Entry const& entry = tree.entries[s.resolved_entry_id];
            if (s.children.size() != entry.param_types.size()) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Function '" + s.name + "' expects "
                    + std::to_string(entry.param_types.size())
                    + " arguments, got "
                    + std::to_string(s.children.size()) + ".", {}});
                // Walk args with no context so internal errors still report.
                for (auto& ch : s.children) {
                    if (ch) inferExpr(tree, *ch, "", diag);
                }
                return;
            }
            for (size_t i = 0; i < s.children.size(); i++) {
                if (s.children[i]) {
                    inferExpr(tree, *s.children[i], entry.param_types[i], diag);
                }
            }
            // Stamp for codegen: return type drives the call's `call <ret>` slot;
            // param_types drive each arg's emit dest_type.
            s.return_type = entry.slids_type;
            s.param_types = entry.param_types;
            return;
        }
        case parse::Kind::kReturnStmt: {
            for (auto& ch : s.children) {
                if (ch) inferExpr(tree, *ch, fn_return_type, diag);
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
        case parse::Kind::kParam:
            assert(false && "classifyStmt: not a statement kind");
            return;
    }
}

void classifyFunctionBody(parse::Tree& tree, parse::Node& fn,
                          diagnostic::Sink& diag) {
    parse::pushFrame(tree);
    // Params become LocalVar entries seeded into the body frame. Their type
    // spellings were already validated in pass 1 via requireKnownType.
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
        e.tok = p->tok;
        p->resolved_entry_id = parse::addEntry(tree, std::move(e));
    }
    for (auto& ch : fn.children) {
        if (ch) classifyStmt(tree, *ch, fn.return_type, diag);
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

    // Pass 1 — collect functions at program scope. Decl+Def pair binds to the
    // same entry; Def+Def is a duplicate; signature mismatch is rejected.
    for (auto& ch : program->children) {
        if (!ch) continue;
        if (ch->kind != parse::Kind::kFunctionDef
         && ch->kind != parse::Kind::kFunctionDecl) {
            continue;
        }
        requireKnownType(ch->return_type, ch->file_id, ch->tok, "Return", diag);
        std::vector<std::string> param_types;
        for (auto& p : ch->params) {
            if (!p) continue;
            requireKnownType(p->return_type, p->file_id, p->tok, "Parameter", diag);
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
        e.tok = ch->tok;
        e.defined = is_def;
        ch->resolved_entry_id = parse::addEntry(tree, std::move(e));
    }

    // Pass 2 — walk each function body.
    for (auto& ch : program->children) {
        if (!ch || ch->kind != parse::Kind::kFunctionDef) continue;
        classifyFunctionBody(tree, *ch, diag);
    }

    parse::popFrame(tree);
}

}  // namespace classify
