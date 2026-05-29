#include "classify.h"

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>

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
// rejected here.
bool isCoercibleToBool(std::string const& t) {
    return isNumericType(t) || isPtrLikeType(t);
}

// Text suitable for widen::intLiteralFits / floatLiteralFits — bool "true"/"false"
// → "1"/"0"; everything else passes through.
std::string literalTextForFit(parse::Node const& n) {
    if (n.kind == parse::Kind::kBoolLiteral) return (n.text == "true") ? "1" : "0";
    return n.text;
}

// Per-kind default when a literal has no usable context.
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
        case parse::Kind::kCallExpr:
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
// not the arith operator.
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
// and the other is not, try-flex the literal into the partner's type.
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
            assert(e.resolved_entry_id >= 0
                && "inferExpr kIdentExpr: resolve did not stamp resolved_entry_id");
            e.inferred_type = parse::entryType(tree, e.resolved_entry_id);
            return;
        }
        case parse::Kind::kCallExpr: {
            // resolve stamped return_type + param_types (and already rejected
            // print intrinsics in expression position). A call yields its
            // return type; widening into `context` happens at codegen, like an
            // ident read. Reject a void return used where a value is wanted.
            if (e.return_type == "void") {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Function '" + e.name + "' returns no value and cannot be "
                    "used as an expression.", {}});
            }
            e.inferred_type = e.return_type;
            for (size_t i = 0; i < e.children.size(); i++) {
                if (!e.children[i]) continue;
                std::string const& dest = (i < e.param_types.size())
                    ? e.param_types[i]
                    : std::string();
                inferExpr(tree, *e.children[i], dest, diag);
            }
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

void classifyStmt(parse::Tree& tree, parse::Node& s,
                  std::string const& fn_return_type,
                  diagnostic::Sink& diag) {
    switch (s.kind) {
        case parse::Kind::kVarDeclStmt: {
            if (!s.children.empty()) {
                inferExpr(tree, *s.children[0], s.return_type, diag);
            }
            return;
        }
        case parse::Kind::kAssignStmt: {
            // resolve already stamped resolved_entry_id (or emitted an error
            // and we won't reach here — main short-circuits on resolve errors).
            std::string lvalue_type;
            if (s.resolved_entry_id >= 0) {
                lvalue_type = parse::entryType(tree, s.resolved_entry_id);
            }
            for (auto& ch : s.children) {
                if (ch) inferExpr(tree, *ch, lvalue_type, diag);
            }
            return;
        }
        case parse::Kind::kAugAssignStmt: {
            // resolve stamped resolved_entry_id and cached lvalue type as
            // s.return_type. If resolve failed, we shouldn't be here (main
            // short-circuits) — but be defensive and walk rhs anyway.
            assert(s.children.size() == 1 && "AugAssignStmt needs 1 rhs child");
            parse::Node& rhs = *s.children[0];
            std::string const& op = s.text;
            std::string const& lvalue_type = s.return_type;

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
                for (auto& ch : s.children) {
                    if (ch) inferPrintArg(tree, *ch, diag);
                }
                return;
            }
            // resolve stamped resolved_entry_id + return_type + param_types.
            // Arity already validated upstream; infer each arg with the
            // corresponding param_type as context.
            for (size_t i = 0; i < s.children.size(); i++) {
                if (!s.children[i]) continue;
                std::string const& dest = (i < s.param_types.size())
                    ? s.param_types[i]
                    : std::string();
                inferExpr(tree, *s.children[i], dest, diag);
            }
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
        case parse::Kind::kCallExpr:
        case parse::Kind::kParam:
            assert(false && "classifyStmt: not a statement kind");
            return;
    }
}

void classifyFunctionBody(parse::Tree& tree, parse::Node& fn,
                          diagnostic::Sink& diag) {
    // No frame push — resolve already handled scope discipline. We just
    // walk stmts and infer types using resolved_entry_id stamped upstream.
    for (auto& ch : fn.children) {
        if (ch) classifyStmt(tree, *ch, fn.return_type, diag);
    }
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

    for (auto& ch : program->children) {
        if (!ch) continue;
        if (ch->kind == parse::Kind::kFunctionDef) {
            classifyFunctionBody(tree, *ch, diag);
        } else if (ch->kind == parse::Kind::kVarDeclStmt && ch->is_const) {
            // Type-infer top-level const init in its declared type's context.
            for (auto& init : ch->children) {
                if (init) inferExpr(tree, *init, ch->return_type, diag);
            }
        }
    }
}

}  // namespace classify
