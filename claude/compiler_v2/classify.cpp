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
        case parse::Kind::kExprStmt:
        case parse::Kind::kAliasDecl:
        case parse::Kind::kNamespaceDecl:
        case parse::Kind::kEnumDecl:
        case parse::Kind::kPreIncExpr:
        case parse::Kind::kPostIncExpr:
        case parse::Kind::kStringifyType:
        case parse::Kind::kReturnStmt:
        case parse::Kind::kBlockStmt:
        case parse::Kind::kIfStmt:
        case parse::Kind::kWhileStmt:
        case parse::Kind::kDoWhileStmt:
        case parse::Kind::kForLongStmt:
        case parse::Kind::kForEnumStmt:
        case parse::Kind::kBreakStmt:
        case parse::Kind::kContinueStmt:
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
void classifyFunctionBody(parse::Tree& tree, parse::Node& fn,
                          diagnostic::Sink& diag);
void classifyNamespace(parse::Tree& tree, parse::Node& node,
                       diagnostic::Sink& diag);

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

// Alias-label propagation for an arith/bitwise binary. An alias is sticky against
// itself or a const literal (which flexes into the named partner), and drops to
// its underlying (empty label) against any other typed operand:
//   Integer + Integer -> Integer;  Integer + 1 -> Integer;  Integer + int -> int.
std::string binaryLabel(parse::Node const& lhs, parse::Node const& rhs) {
    bool lhs_lit = isLiteralKind(lhs.kind);
    bool rhs_lit = isLiteralKind(rhs.kind);
    if (!lhs.alias_label.empty()
        && (rhs_lit || lhs.alias_label == rhs.alias_label)) {
        return lhs.alias_label;
    }
    if (!rhs.alias_label.empty() && lhs_lit) return rhs.alias_label;
    return "";
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
        case parse::Kind::kStringifyType: {
            // ##type(expr): infer the operand's type, then BECOME a string
            // literal holding that type name. An alias/enum-labeled operand
            // reports its label (the as-declared name); everything else reports
            // its erased underlying type. Lowered in place here so every
            // downstream stage only ever sees a kStringLiteral.
            assert(e.children.size() == 1 && "kStringifyType needs 1 operand");
            parse::Node& operand = *e.children[0];
            inferExpr(tree, operand, "", diag);
            e.text = operand.alias_label.empty() ? operand.inferred_type
                                                 : operand.alias_label;
            e.children.clear();
            e.kind = parse::Kind::kStringLiteral;
            e.inferred_type = "char[]";
            return;
        }
        case parse::Kind::kIdentExpr: {
            assert(e.resolved_entry_id >= 0
                && "inferExpr kIdentExpr: resolve did not stamp resolved_entry_id");
            e.inferred_type = parse::entryType(tree, e.resolved_entry_id);
            e.alias_label = tree.entries[e.resolved_entry_id].alias_label;
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
        case parse::Kind::kPreIncExpr:
        case parse::Kind::kPostIncExpr: {
            // An inc/dec yields its operand's type. Phase 1: int-class (not
            // bool) and float scalars step by 1; pointers (element stride) and
            // everything else are rejected until their phases wire the step.
            parse::Node& operand = *e.children[0];
            inferExpr(tree, operand, "", diag);
            std::string const& ot = operand.inferred_type;
            if (!ot.empty() && (!isNumericType(ot) || ot == "bool")) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Operator '" + e.text + "' is not defined on type '"
                    + ot + "'.", {}});
            }
            e.inferred_type = ot;
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
                e.alias_label = operand.alias_label;   // a unary keeps the label
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
                e.alias_label = lhs.alias_label;   // a shift keeps the lhs label
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
            // A comparison yields bool (no label); an arith/bitwise result keeps
            // an alias label only when both sides agree (or a literal flexes in).
            if (!is_cmp) e.alias_label = binaryLabel(lhs, rhs);
            return;
        }
        case parse::Kind::kProgram:
        case parse::Kind::kFunctionDef:
        case parse::Kind::kFunctionDecl:
        case parse::Kind::kVarDeclStmt:
        case parse::Kind::kAssignStmt:
        case parse::Kind::kAugAssignStmt:
        case parse::Kind::kCallStmt:
        case parse::Kind::kExprStmt:
        case parse::Kind::kAliasDecl:
        case parse::Kind::kNamespaceDecl:
        case parse::Kind::kEnumDecl:
        case parse::Kind::kReturnStmt:
        case parse::Kind::kBlockStmt:
        case parse::Kind::kIfStmt:
        case parse::Kind::kWhileStmt:
        case parse::Kind::kDoWhileStmt:
        case parse::Kind::kForLongStmt:
        case parse::Kind::kForEnumStmt:
        case parse::Kind::kBreakStmt:
        case parse::Kind::kContinueStmt:
        case parse::Kind::kParam:
            assert(false && "inferExpr: not an expression kind");
            return;
    }
}

// A condition whose value is known at compile time (constfold ran upstream, so a
// constant condition is a folded literal — incl. a substituted const or the
// synthesized empty-`()` true). Drives constant-branch unreachable detection.
enum class CondConst { True, False, NotConst };

bool literalIsZero(parse::Node const& n) {
    if (n.kind == parse::Kind::kFloatLiteral) {
        return std::strtod(n.text.c_str(), nullptr) == 0.0;   // ±0.0
    }
    // bool ("1"/"0"), int / uint / char: decimal magnitude (numeric-canonical).
    errno = 0;
    char* end = nullptr;
    long long v = std::strtoll(n.text.c_str(), &end, 10);
    if (errno == ERANGE) return false;   // doesn't fit -> certainly nonzero
    return v == 0;
}

CondConst constTruth(parse::Node const& cond) {
    if (cond.kind == parse::Kind::kBoolLiteral
        || cond.kind == parse::Kind::kIntLiteral
        || cond.kind == parse::Kind::kUintLiteral
        || cond.kind == parse::Kind::kCharLiteral
        || cond.kind == parse::Kind::kFloatLiteral) {
        return literalIsZero(cond) ? CondConst::False : CondConst::True;
    }
    return CondConst::NotConst;
}

// Report the first statement of an unreachable branch. The branch is a kBlockStmt
// (then / else / loop body) or, for an `else if`, a nested kIfStmt. An empty
// block has nothing to flag.
void reportUnreachableBranch(parse::Node const& branch, diagnostic::Sink& diag) {
    if (branch.kind == parse::Kind::kBlockStmt) {
        for (auto const& ch : branch.children) {
            if (ch) {
                diagnostic::report(diag, {ch->file_id, ch->tok,
                    "Unreachable statement.", {}});
                return;
            }
        }
        return;   // empty block: nothing unreachable
    }
    diagnostic::report(diag, {branch.file_id, branch.tok,
        "Unreachable statement.", {}});   // else-if chain
}

// `a cmp b` for the ranged-for empty-range check. Unknown cmp -> true (don't flag).
template <typename T>
bool applyCmp(T a, T b, std::string const& cmp) {
    if (cmp == "<")  return a < b;
    if (cmp == "<=") return a <= b;
    if (cmp == ">")  return a > b;
    if (cmp == ">=") return a >= b;
    if (cmp == "!=") return a != b;
    return true;
}

// Empty-range check for a ranged-for: both bounds constant (folded literals) and
// `start cmp end` FALSE means the loop body can never run. Non-literal bounds ->
// not decidable -> not an error.
bool rangeFirstTestFalse(parse::Node const& start, parse::Node const& end,
                         std::string const& cmp) {
    auto isLit = [](parse::Node const& n) {
        return n.kind == parse::Kind::kBoolLiteral
            || n.kind == parse::Kind::kIntLiteral
            || n.kind == parse::Kind::kUintLiteral
            || n.kind == parse::Kind::kCharLiteral
            || n.kind == parse::Kind::kFloatLiteral;
    };
    if (!isLit(start) || !isLit(end)) return false;
    if (start.kind == parse::Kind::kFloatLiteral
        || end.kind == parse::Kind::kFloatLiteral) {
        double a = std::strtod(start.text.c_str(), nullptr);
        double b = std::strtod(end.text.c_str(), nullptr);
        return !applyCmp(a, b, cmp);
    }
    errno = 0;
    long long a = std::strtoll(start.text.c_str(), nullptr, 10);
    if (errno == ERANGE) return false;
    errno = 0;
    long long b = std::strtoll(end.text.c_str(), nullptr, 10);
    if (errno == ERANGE) return false;
    return !applyCmp(a, b, cmp);
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
            // A void function returning a value would emit `ret void <val>`
            // (invalid IR). Reject at the `return`. (Non-literal values would
            // otherwise slip past the literal-fit check into codegen.)
            if (fn_return_type == "void" && !s.children.empty()) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "A void function cannot return a value.", {}});
                return;
            }
            for (auto& ch : s.children) {
                if (ch) inferExpr(tree, *ch, fn_return_type, diag);
            }
            return;
        }
        case parse::Kind::kExprStmt: {
            // value discarded — infer for its checks (lvalue / numeric) only.
            for (auto& ch : s.children) {
                if (ch) inferExpr(tree, *ch, "", diag);
            }
            return;
        }
        case parse::Kind::kAliasDecl:
            // resolve substituted the alias away; nothing to type here.
            return;
        case parse::Kind::kNamespaceDecl:
            classifyNamespace(tree, s, diag);
            return;
        case parse::Kind::kEnumDecl:
            // Enum members were lowered to kConst entries at resolve and folded
            // by constfold; the enum node carries nothing to type-infer.
            return;
        case parse::Kind::kBlockStmt:
            // A nested scope: type-infer each contained statement.
            for (auto& ch : s.children) {
                if (ch) classifyStmt(tree, *ch, fn_return_type, diag);
            }
            return;
        case parse::Kind::kIfStmt: {
            // children[0] = condition, [1] = then-branch, [2] = optional else.
            // The condition truthy-coerces (same rule as `!`/`&&`/`||`); a void
            // or other non-value-typed condition is rejected.
            assert(s.children.size() >= 2 && "kIfStmt needs condition + then");
            parse::Node& cond = *s.children[0];
            inferExpr(tree, cond, "", diag);
            if (!cond.inferred_type.empty()
                && !isCoercibleToBool(cond.inferred_type)) {
                diagnostic::report(diag, {cond.file_id, cond.tok,
                    "An if condition must be a condition expression; type '"
                    + cond.inferred_type + "' is not.", {}});
            }
            // Constant condition -> the opposite branch is dead: a const-true if
            // never enters the else, a const-false if never enters the then.
            bool has_else = s.children.size() > 2 && s.children[2];
            CondConst c = constTruth(cond);
            if (c == CondConst::True && has_else) {
                reportUnreachableBranch(*s.children[2], diag);
            } else if (c == CondConst::False) {
                reportUnreachableBranch(*s.children[1], diag);
            }
            classifyStmt(tree, *s.children[1], fn_return_type, diag);
            if (has_else) {
                classifyStmt(tree, *s.children[2], fn_return_type, diag);
            }
            return;
        }
        case parse::Kind::kWhileStmt: {
            // children[0] = condition, [1] = body. Condition truthy-coerces
            // (same rule as the if condition).
            assert(s.children.size() == 2 && "kWhileStmt needs condition + body");
            parse::Node& cond = *s.children[0];
            inferExpr(tree, cond, "", diag);
            if (!cond.inferred_type.empty()
                && !isCoercibleToBool(cond.inferred_type)) {
                diagnostic::report(diag, {cond.file_id, cond.tok,
                    "A while condition must be a condition expression; type '"
                    + cond.inferred_type + "' is not.", {}});
            }
            // A constant-false pre-condition never runs the body. A constant-true
            // loop is NOT flagged: per 3B there is no constant-true loop special
            // case (the body is reachable; an unreachable after-loop is deferred).
            if (constTruth(cond) == CondConst::False) {
                reportUnreachableBranch(*s.children[1], diag);
            }
            classifyStmt(tree, *s.children[1], fn_return_type, diag);
            return;
        }
        case parse::Kind::kDoWhileStmt: {
            // children[0] = condition, [1] = body. Same shape as kWhileStmt; the
            // condition truthy-coerces. (Flow ordering — body-then-test — is a
            // resolve/codegen concern; type inference is order-independent.)
            assert(s.children.size() == 2 && "kDoWhileStmt needs condition + body");
            parse::Node& cond = *s.children[0];
            inferExpr(tree, cond, "", diag);
            if (!cond.inferred_type.empty()
                && !isCoercibleToBool(cond.inferred_type)) {
                diagnostic::report(diag, {cond.file_id, cond.tok,
                    "A while condition must be a condition expression; type '"
                    + cond.inferred_type + "' is not.", {}});
            }
            classifyStmt(tree, *s.children[1], fn_return_type, diag);
            return;
        }
        case parse::Kind::kForLongStmt: {
            // children[0]=cond, [1]=update, [2]=body, [3..]=varlist decls.
            assert(s.children.size() >= 3 && "kForLongStmt needs cond+update+body");
            for (std::size_t i = 3; i < s.children.size(); i++) {
                if (s.children[i]) classifyStmt(tree, *s.children[i], fn_return_type, diag);
            }
            parse::Node& cond = *s.children[0];
            inferExpr(tree, cond, "", diag);
            if (!cond.inferred_type.empty()
                && !isCoercibleToBool(cond.inferred_type)) {
                diagnostic::report(diag, {cond.file_id, cond.tok,
                    "A for condition must be a condition expression; type '"
                    + cond.inferred_type + "' is not.", {}});
            }
            classifyStmt(tree, *s.children[1], fn_return_type, diag);   // update
            classifyStmt(tree, *s.children[2], fn_return_type, diag);   // body
            // Ranged-for empty-range check: children[3] = loop var (init = start),
            // [4] = _$end (init = end); cond.text = cmp. Both bounds constant and
            // `start cmp end` false -> the body never runs -> "Invalid range." at
            // the `..`. (Ranged-for only — gated on range_dotdot_tok.)
            if (s.range_dotdot_tok >= 0 && s.children.size() >= 5
                && !s.children[3]->children.empty()
                && !s.children[4]->children.empty()
                && rangeFirstTestFalse(*s.children[3]->children[0],
                                       *s.children[4]->children[0], cond.text)) {
                diagnostic::report(diag, {s.file_id, s.range_dotdot_tok,
                    "Invalid range.", {}});
            }
            return;
        }
        case parse::Kind::kBreakStmt:
        case parse::Kind::kContinueStmt:
            // Nothing to type-infer; resolve handled loop-legality.
            return;
        case parse::Kind::kForEnumStmt:
            // Lowered to a kForLongStmt during resolve; never reaches classify.
            assert(false && "classifyStmt: kForEnumStmt survived resolve");
            return;
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
        case parse::Kind::kStringifyType:
        case parse::Kind::kCallExpr:
        case parse::Kind::kParam:
            assert(false && "classifyStmt: not a statement kind");
            return;
    }
}

// "Last statement completes by returning" — the trailing-return heuristic. A
// trailing block satisfies it if ITS last statement does (recurse), so
// `int f(){ { return 1; } }` passes. Not full reachability (the Completion
// lattice in Session 2 supersedes this); just enough that a block at the tail
// doesn't trip the check.
bool endsInReturnNode(parse::Node const& s);

bool endsInReturn(std::vector<std::unique_ptr<parse::Node>> const& stmts) {
    if (stmts.empty() || !stmts.back()) return false;
    return endsInReturnNode(*stmts.back());
}

// Whether a single statement guarantees control leaves via a return: a return,
// a block whose last statement does, or an if/else WITH an else where both arms
// do (an else-less if always has a fall-through path). An `else if` chain works
// by recursion — the else-branch is itself a kIfStmt.
bool endsInReturnNode(parse::Node const& s) {
    if (s.kind == parse::Kind::kReturnStmt) return true;
    if (s.kind == parse::Kind::kBlockStmt) return endsInReturn(s.children);
    if (s.kind == parse::Kind::kIfStmt && s.children.size() > 2
        && s.children[2]) {
        return endsInReturnNode(*s.children[1])   // then-branch (a block)
            && endsInReturnNode(*s.children[2]);  // else-branch (block or if)
    }
    return false;
}

void classifyFunctionBody(parse::Tree& tree, parse::Node& fn,
                          diagnostic::Sink& diag) {
    // No frame push — resolve already handled scope discipline. We just
    // walk stmts and infer types using resolved_entry_id stamped upstream.
    for (auto& ch : fn.children) {
        if (ch) classifyStmt(tree, *ch, fn.return_type, diag);
    }
    // A non-void function must end with a return statement, else codegen
    // would emit an unterminated block. This is the "last statement is a
    // return" heuristic, not full reachability (see todo: revisit non-void
    // function returns). void bodies fall through to an implicit `ret void`.
    if (fn.return_type != "void") {
        if (!endsInReturn(fn.children)) {
            diagnostic::report(diag, {fn.file_id, fn.name_tok,
                "Function '" + fn.name + "' must end with a return statement.", {}});
        }
    }
}

// Type-infer a namespace's members: const inits in their declared-type context,
// member function bodies, and nested namespaces. Mirrors classify::run's
// file-scope handling, recursing through the namespace structure.
void classifyNamespace(parse::Tree& tree, parse::Node& node,
                       diagnostic::Sink& diag) {
    for (auto& m : node.children) {
        if (!m) continue;
        if (m->kind == parse::Kind::kNamespaceDecl) {
            classifyNamespace(tree, *m, diag);
        } else if (m->kind == parse::Kind::kVarDeclStmt && m->is_const) {
            for (auto& init : m->children) {
                if (init) inferExpr(tree, *init, m->return_type, diag);
            }
        } else if (m->kind == parse::Kind::kFunctionDef) {
            classifyFunctionBody(tree, *m, diag);
        }
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
        } else if (ch->kind == parse::Kind::kNamespaceDecl) {
            classifyNamespace(tree, *ch, diag);
        } else if (ch->kind == parse::Kind::kVarDeclStmt && ch->is_const) {
            // Type-infer top-level const init in its declared type's context.
            for (auto& init : ch->children) {
                if (init) inferExpr(tree, *init, ch->return_type, diag);
            }
        }
    }
}

}  // namespace classify
