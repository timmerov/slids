#include "constfold.h"

#include <cassert>
#include <cerrno>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

#include "diagnostic.h"
#include "parse.h"
#include "widen.h"

namespace constfold {

namespace {

bool isLiteral(parse::Node const& n) {
    return n.kind == parse::Kind::kIntLiteral
        || n.kind == parse::Kind::kUintLiteral
        || n.kind == parse::Kind::kCharLiteral
        || n.kind == parse::Kind::kBoolLiteral
        || n.kind == parse::Kind::kFloatLiteral;
}

bool isIntClassLiteral(parse::Node const& n) {
    return n.kind == parse::Kind::kIntLiteral
        || n.kind == parse::Kind::kUintLiteral
        || n.kind == parse::Kind::kCharLiteral
        || n.kind == parse::Kind::kBoolLiteral;
}

bool parseSignedDigits(std::string const& text, bool& negative, uint64_t& mag) {
    if (text.empty()) return false;
    char const* s = text.c_str();
    negative = (*s == '-');
    if (negative) s++;
    if (*s == '\0') return false;
    char* end = nullptr;
    errno = 0;
    mag = std::strtoull(s, &end, 10);
    if (end == s || *end != '\0') return false;
    if (errno == ERANGE) return false;
    return true;
}

// fold.sl:16-23 nominal type by value.
// integer kind: smallest bit-width tier that fits, signed-first within tier.
std::string nominalForInt(uint64_t mag, bool negative) {
    if (negative) {
        if (mag <= 128ULL) return "int8";
        if (mag <= 32768ULL) return "int16";
        if (mag <= (uint64_t)INT32_MAX + 1ULL) return "int32";
        if (mag <= (uint64_t)INT64_MAX + 1ULL) return "int64";
        return "";  // overflow — widen::checkIntLiteralFits reports at codegen.
    }
    if (mag <= (uint64_t)INT8_MAX) return "int8";
    if (mag <= 255ULL) return "uint8";
    if (mag <= (uint64_t)INT16_MAX) return "int16";
    if (mag <= 65535ULL) return "uint16";
    if (mag <= (uint64_t)INT32_MAX) return "int32";
    if (mag <= (uint64_t)UINT32_MAX) return "uint32";
    if (mag <= (uint64_t)INT64_MAX) return "int64";
    return "uint64";
}

std::string nominalForUint(uint64_t mag) {
    if (mag <= 1ULL) return "uint1";
    if (mag <= 255ULL) return "uint8";
    if (mag <= 65535ULL) return "uint16";
    if (mag <= (uint64_t)UINT32_MAX) return "uint32";
    return "uint64";
}

std::string nominalForFloat(std::string const& text) {
    errno = 0;
    double v = std::strtod(text.c_str(), nullptr);
    if (errno == ERANGE || !std::isfinite(v)) return "float64";
    if (v > (double)FLT_MAX || v < -(double)FLT_MAX) return "float64";
    float f = (float)v;
    double back = (double)f;
    return (back == v) ? "float32" : "float64";
}

// Assign nominal_type to a literal node (no-op for non-literals).
// Negative-int overflow (magnitude > INT64_MAX+1) leaves nominal_type empty
// and is reported downstream by widen::checkIntLiteralFits at the literal-
// emit site as "Integer literal does not fit in 'int64'." — uniform with how
// other overflowing literal/dest pairs are reported. user notified, accepts
// state.
void assignNominal(parse::Node& n) {
    if (!n.nominal_type.empty()) return;  // already assigned (e.g. by a prior fold)
    switch (n.kind) {
        case parse::Kind::kIntLiteral: {
            bool neg = false;
            uint64_t mag = 0;
            if (parseSignedDigits(n.text, neg, mag)) {
                n.nominal_type = nominalForInt(mag, neg);
            }
            return;
        }
        case parse::Kind::kUintLiteral: {
            bool neg = false;
            uint64_t mag = 0;
            if (parseSignedDigits(n.text, neg, mag) && !neg) {
                n.nominal_type = nominalForUint(mag);
            }
            return;
        }
        case parse::Kind::kCharLiteral:
            n.nominal_type = "uint8";
            return;
        case parse::Kind::kBoolLiteral:
            n.nominal_type = "uint1";
            return;
        case parse::Kind::kFloatLiteral:
            n.nominal_type = nominalForFloat(n.text);
            return;
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
        case parse::Kind::kReturnStmt:
        case parse::Kind::kBlockStmt:
        case parse::Kind::kIfStmt:
        case parse::Kind::kWhileStmt:
        case parse::Kind::kDoWhileStmt:
        case parse::Kind::kBreakStmt:
        case parse::Kind::kContinueStmt:
        case parse::Kind::kStringLiteral:
        case parse::Kind::kIdentExpr:
        case parse::Kind::kUnaryExpr:
        case parse::Kind::kBinaryExpr:
        case parse::Kind::kPreIncExpr:
        case parse::Kind::kPostIncExpr:
        case parse::Kind::kParam:
            return;  // non-literal: no nominal type
    }
}

void stripLeadingPlus(std::string& text) {
    if (!text.empty() && text[0] == '+') text.erase(0, 1);
}

void toggleSign(std::string& text) {
    if (text.empty()) return;
    if (text[0] == '+') text.erase(0, 1);
    if (!text.empty() && text[0] == '-') text.erase(0, 1);
    else text.insert(0, "-");
    if (text == "-0") text = "0";
}

bool isZeroIntText(std::string const& s) {
    bool any = false;
    for (char c : s) {
        if (c == '_' || c == '+' || c == '-') continue;
        if (c != '0') return false;
        any = true;
    }
    return any;
}

bool isZeroFloatText(std::string const& s) {
    errno = 0;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || errno == ERANGE) return false;
    return v == 0.0;
}

// ~N as text. Operate in int64. Magnitudes out of range stay unfolded so the
// catch-all surfaces them.
void foldBitNotIntText(std::string& text) {
    std::string clean;
    for (char c : text) if (c != '_') clean += c;
    bool neg = !clean.empty() && clean[0] == '-';
    if (neg) clean.erase(0, 1);
    else if (!clean.empty() && clean[0] == '+') clean.erase(0, 1);
    errno = 0;
    char* end = nullptr;
    uint64_t mag = std::strtoull(clean.c_str(), &end, 10);
    if (end == clean.c_str() || *end != '\0' || errno == ERANGE) return;
    if (neg) {
        if (mag == 0) { text = "-1"; return; }
        text = std::to_string(mag - 1);
    } else {
        if (mag == UINT64_MAX) return;
        text = "-" + std::to_string(mag + 1);
    }
}

// Unary fold per fold.sl rules 1a-1f.
std::unique_ptr<parse::Node> tryFoldUnary(parse::Node& node, diagnostic::Sink& diag) {
    if (node.kind != parse::Kind::kUnaryExpr) return nullptr;
    assert(node.children.size() == 1
        && "tryFoldUnary: UnaryExpr needs 1 child");
    parse::Node& operand = *node.children[0];
    std::string const& op = node.text;

    if (op == "+") {
        // 1a: nop, kind preserved.
        if (!isLiteral(operand)) return nullptr;
        auto child = std::move(node.children[0]);
        stripLeadingPlus(child->text);
        child->nominal_type.clear();  // recompute by value
        return child;
    }
    if (op == "-") {
        // 1c (float): widen to float64, kind float.
        // 1d (bool, char, integer, unsigned): widen to int64, kind integer.
        if (!isLiteral(operand)) return nullptr;
        auto child = std::move(node.children[0]);
        toggleSign(child->text);
        if (child->kind != parse::Kind::kFloatLiteral) {
            // Per rule 1d the result is kind=integer regardless of operand kind.
            child->kind = parse::Kind::kIntLiteral;
        }
        child->nominal_type.clear();
        return child;
    }
    if (op == "~") {
        // 1e (bool, char, integer, unsigned): kind preserved (bool→unsigned),
        // nominal size = operand's nominal size.
        // 1f (float): compile error.
        if (operand.kind == parse::Kind::kFloatLiteral) {
            diagnostic::report(diag, {node.file_id, node.tok,
                "Bitwise '~' not defined on floating-point literal.", {}});
            return nullptr;
        }
        if (!isIntClassLiteral(operand)) return nullptr;
        std::string operand_nominal = operand.nominal_type;
        auto child = std::move(node.children[0]);
        foldBitNotIntText(child->text);
        // Per 1e: bool becomes unsigned, char/integer/unsigned kind preserved.
        if (child->kind == parse::Kind::kBoolLiteral) {
            child->kind = parse::Kind::kUintLiteral;
        }
        // For integer kind operand the result may carry a negative text;
        // kIntLiteral handles signed text. For uint/char/bool→unsigned, the
        // computation is in uint64 — store the unsigned wraparound.
        if (child->kind == parse::Kind::kUintLiteral) {
            // Recompute as uint64 wraparound: ~mag in uint64.
            bool neg = false;
            uint64_t mag = 0;
            // After foldBitNotIntText, text is signed (e.g. -1 for ~0).
            if (parseSignedDigits(child->text, neg, mag)) {
                uint64_t comp = neg ? (uint64_t)(0ULL - mag) : mag;
                child->text = std::to_string(comp);
            }
        }
        // Result nominal = operand's nominal per rule 1e.
        child->nominal_type = operand_nominal;
        return child;
    }
    if (op == "!") {
        // 1b: all literal types accepted, result bool.
        bool result;
        if (operand.kind == parse::Kind::kIntLiteral
         || operand.kind == parse::Kind::kUintLiteral
         || operand.kind == parse::Kind::kCharLiteral
         || operand.kind == parse::Kind::kBoolLiteral) {
            result = isZeroIntText(operand.text);
        } else if (operand.kind == parse::Kind::kFloatLiteral) {
            result = isZeroFloatText(operand.text);
        } else {
            return nullptr;
        }
        auto out = std::make_unique<parse::Node>();
        out->kind = parse::Kind::kBoolLiteral;
        out->text = result ? "1" : "0";
        out->nominal_type = "uint1";
        out->file_id = node.file_id;
        out->tok = node.tok;
        return out;
    }
    return nullptr;
}

bool parseI64(std::string const& t, int64_t& v) {
    if (t.empty()) return false;
    errno = 0;
    char* end = nullptr;
    v = static_cast<int64_t>(std::strtoll(t.c_str(), &end, 10));
    if (end == t.c_str() || *end != '\0' || errno == ERANGE) return false;
    return true;
}

bool parseU64(std::string const& t, uint64_t& v) {
    if (t.empty()) return false;
    if (t[0] == '-') return false;
    errno = 0;
    char* end = nullptr;
    v = std::strtoull(t.c_str(), &end, 10);
    if (end == t.c_str() || *end != '\0' || errno == ERANGE) return false;
    return true;
}

bool parseDouble(std::string const& t, double& v) {
    if (t.empty()) return false;
    errno = 0;
    char* end = nullptr;
    v = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || *end != '\0' || errno == ERANGE) return false;
    return std::isfinite(v);
}

std::unique_ptr<parse::Node> makeLitAt(parse::Node const& src, parse::Kind kind,
                                        std::string text) {
    auto out = std::make_unique<parse::Node>();
    out->kind = kind;
    out->text = std::move(text);
    out->file_id = src.file_id;
    out->tok = src.tok;
    return out;
}

std::string floatToText(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

// D1 + D3 float arm. Returns kFloatLiteral for arith, kBoolLiteral for cmp.
std::unique_ptr<parse::Node> tryFoldFloatBinary(parse::Node& node,
                                                 parse::Node& lhs, parse::Node& rhs,
                                                 std::string const& op, bool is_cmp,
                                                 diagnostic::Sink& diag) {
    double a, b;
    if (!parseDouble(lhs.text, a) || !parseDouble(rhs.text, b)) return nullptr;

    if (is_cmp) {
        bool r;
        if      (op == "==") r = (a == b);
        else if (op == "!=") r = (a != b);
        else if (op == "<")  r = (a <  b);
        else if (op == "<=") r = (a <= b);
        else if (op == ">")  r = (a >  b);
        else /*  >= */       r = (a >= b);
        return makeLitAt(node, parse::Kind::kBoolLiteral, r ? "1" : "0");
    }

    if (op == "&" || op == "|" || op == "^") {
        diagnostic::report(diag, {node.file_id, node.tok,
            "Bitwise '" + op + "' not defined on floating-point literal.", {}});
        return nullptr;
    }

    double r;
    if      (op == "+") r = a + b;
    else if (op == "-") r = a - b;
    else if (op == "*") r = a * b;
    else if (op == "/") {
        if (b == 0.0) {
            diagnostic::report(diag, {node.file_id, node.tok,
                "Division by zero.", {}});
            return nullptr;
        }
        r = a / b;
    }
    else if (op == "%") {
        if (b == 0.0) {
            diagnostic::report(diag, {node.file_id, node.tok,
                "Modulo by zero.", {}});
            return nullptr;
        }
        r = std::fmod(a, b);
    }
    else return nullptr;

    if (!std::isfinite(r)) {
        diagnostic::report(diag, {node.file_id, node.tok,
            "Floating-point overflow in folded expression.", {}});
        return nullptr;
    }
    return makeLitAt(node, parse::Kind::kFloatLiteral, floatToText(r));
}

// D2 shift fold. Dispatches on lhs kind: float lhs lowers to pow2 mul/div;
// int lhs shifts at int64 width. Caller has already verified both operands
// are literals.
std::unique_ptr<parse::Node> tryFoldShift(parse::Node& node,
                                          parse::Node& lhs, parse::Node& rhs,
                                          std::string const& op,
                                          diagnostic::Sink& diag) {
    if (rhs.kind == parse::Kind::kFloatLiteral) {
        diagnostic::report(diag, {node.file_id, node.tok,
            "Shift count must be integer-class.", {}});
        return nullptr;
    }
    bool count_neg = false;
    uint64_t count_mag = 0;
    if (!parseSignedDigits(rhs.text, count_neg, count_mag)) return nullptr;
    if (count_neg) {
        diagnostic::report(diag, {node.file_id, node.tok,
            "Shift count is negative.", {}});
        return nullptr;
    }

    if (lhs.kind == parse::Kind::kFloatLiteral) {
        double v;
        if (!parseDouble(lhs.text, v)) return nullptr;
        // Pow2 must fit uint64 to scale precisely.
        if (count_mag >= 64) return nullptr;
        double pow2 = static_cast<double>(1ULL << count_mag);
        double r = (op == "<<") ? v * pow2 : v / pow2;
        if (!std::isfinite(r)) {
            diagnostic::report(diag, {node.file_id, node.tok,
                "Floating-point overflow in folded shift.", {}});
            return nullptr;
        }
        return makeLitAt(node, parse::Kind::kFloatLiteral, floatToText(r));
    }

    // Int lhs: count >= 64 folds to 0 per spec.
    if (count_mag >= 64) {
        return makeLitAt(node, parse::Kind::kIntLiteral, "0");
    }
    int count = static_cast<int>(count_mag);
    bool lhs_neg = false;
    uint64_t lhs_mag = 0;
    if (!parseSignedDigits(lhs.text, lhs_neg, lhs_mag)) return nullptr;
    bool lhs_unsigned = (lhs.kind == parse::Kind::kUintLiteral
                      || lhs.kind == parse::Kind::kCharLiteral
                      || lhs.kind == parse::Kind::kBoolLiteral);

    if (op == "<<") {
        // Compute in uint64 to avoid signed-overflow UB; reinterpret per kind.
        uint64_t v = lhs_neg ? (0ULL - lhs_mag) : lhs_mag;
        uint64_t r = v << count;
        if (lhs_unsigned) {
            return makeLitAt(node, parse::Kind::kUintLiteral, std::to_string(r));
        }
        return makeLitAt(node, parse::Kind::kIntLiteral,
                         std::to_string(static_cast<int64_t>(r)));
    }
    // >>
    if (lhs_unsigned) {
        uint64_t r = lhs_mag >> count;
        return makeLitAt(node, parse::Kind::kUintLiteral, std::to_string(r));
    }
    int64_t sv = lhs_neg ? -static_cast<int64_t>(lhs_mag)
                         :  static_cast<int64_t>(lhs_mag);
    int64_t r = sv >> count;  // arithmetic right shift (C++20-defined)
    return makeLitAt(node, parse::Kind::kIntLiteral, std::to_string(r));
}

// D3 int-class comparison fold.
std::unique_ptr<parse::Node> foldIntCompare(parse::Node& node,
                                            parse::Node& lhs, parse::Node& rhs,
                                            std::string const& op) {
    int64_t a, b;
    bool r;
    if (parseI64(lhs.text, a) && parseI64(rhs.text, b)) {
        if      (op == "==") r = (a == b);
        else if (op == "!=") r = (a != b);
        else if (op == "<")  r = (a <  b);
        else if (op == "<=") r = (a <= b);
        else if (op == ">")  r = (a >  b);
        else /*  >= */       r = (a >= b);
        return makeLitAt(node, parse::Kind::kBoolLiteral, r ? "1" : "0");
    }
    // One side overflows int64. Per fold.sl widening: only foldable if both
    // are non-negative (then compare in uint64); cross-sign skips.
    uint64_t ua, ub;
    if (!parseU64(lhs.text, ua) || !parseU64(rhs.text, ub)) return nullptr;
    if      (op == "==") r = (ua == ub);
    else if (op == "!=") r = (ua != ub);
    else if (op == "<")  r = (ua <  ub);
    else if (op == "<=") r = (ua <= ub);
    else if (op == ">")  r = (ua >  ub);
    else /*  >= */       r = (ua >= ub);
    return makeLitAt(node, parse::Kind::kBoolLiteral, r ? "1" : "0");
}

// D4 — int-class arith with rule-6 overflow-to-unsigned exception. Result
// is kIntLiteral when the int64 path succeeds, kUintLiteral when int64
// overflows but uint64 holds it.
std::unique_ptr<parse::Node> foldIntArithBitwise(parse::Node& node,
                                                  parse::Node& lhs, parse::Node& rhs,
                                                  std::string const& op,
                                                  diagnostic::Sink& diag) {
    int64_t a, b;
    if (!parseI64(lhs.text, a) || !parseI64(rhs.text, b)) return nullptr;

    if (op == "/" || op == "%") {
        if (b == 0) {
            diagnostic::report(diag, {node.file_id, node.tok,
                (op == "/" ? "Division by zero." : "Modulo by zero."), {}});
            return nullptr;
        }
        // INT64_MIN / -1 overflows int64 → uint64 holds the magnitude.
        if (a == INT64_MIN && b == -1) {
            if (op == "/") {
                return makeLitAt(node, parse::Kind::kUintLiteral,
                                 std::to_string(static_cast<uint64_t>(INT64_MAX) + 1ULL));
            }
            // INT64_MIN % -1 = 0 mathematically.
            return makeLitAt(node, parse::Kind::kIntLiteral, "0");
        }
        int64_t r = (op == "/") ? (a / b) : (a % b);
        return makeLitAt(node, parse::Kind::kIntLiteral, std::to_string(r));
    }

    if (op == "&" || op == "|" || op == "^") {
        int64_t r;
        if      (op == "&") r = a & b;
        else if (op == "|") r = a | b;
        else /*    ^   */   r = a ^ b;
        return makeLitAt(node, parse::Kind::kIntLiteral, std::to_string(r));
    }

    // + - * with rule-6 overflow detection.
    int64_t r;
    bool overflow;
    if      (op == "+") overflow = __builtin_add_overflow(a, b, &r);
    else if (op == "-") overflow = __builtin_sub_overflow(a, b, &r);
    else if (op == "*") overflow = __builtin_mul_overflow(a, b, &r);
    else return nullptr;

    if (!overflow) {
        return makeLitAt(node, parse::Kind::kIntLiteral, std::to_string(r));
    }
    // Retry in uint64; if it also overflows, leave unfolded (catch-all surfaces).
    uint64_t ua = static_cast<uint64_t>(a);
    uint64_t ub = static_cast<uint64_t>(b);
    uint64_t ur;
    bool u_overflow;
    if      (op == "+") u_overflow = __builtin_add_overflow(ua, ub, &ur);
    else if (op == "-") u_overflow = __builtin_sub_overflow(ua, ub, &ur);
    else /*    *   */   u_overflow = __builtin_mul_overflow(ua, ub, &ur);
    if (u_overflow) return nullptr;
    return makeLitAt(node, parse::Kind::kUintLiteral, std::to_string(ur));
}

// Binary fold dispatcher. Both operands must be literals; per-shape arms
// handle int-class vs float separately, plus the shift carve-out where
// the lhs/rhs kinds don't have to match.
std::unique_ptr<parse::Node> tryFoldBinary(parse::Node& node, diagnostic::Sink& diag) {
    if (node.kind != parse::Kind::kBinaryExpr) return nullptr;
    assert(node.children.size() == 2
        && "tryFoldBinary: BinaryExpr needs 2 children");
    parse::Node& lhs = *node.children[0];
    parse::Node& rhs = *node.children[1];
    std::string const& op = node.text;

    if (!isLiteral(lhs) || !isLiteral(rhs)) return nullptr;

    // Shifts pre-empt the same-family check: float-lhs / int-rhs is the
    // normal shape per spec rule 4.
    if (op == "<<" || op == ">>") {
        return tryFoldShift(node, lhs, rhs, op, diag);
    }

    // Logical short-circuit folds need purity tracking (PPID / calls). Defer.
    if (op == "&&" || op == "||" || op == "^^") return nullptr;

    bool lhs_float = lhs.kind == parse::Kind::kFloatLiteral;
    bool rhs_float = rhs.kind == parse::Kind::kFloatLiteral;

    // fold.sl:35-36 no-mix: one float + one int-class is a compile error.
    if (lhs_float != rhs_float) {
        diagnostic::report(diag, {node.file_id, node.tok,
            "No common type for floating-point and integer-class literals; "
            "use an explicit type conversion.", {}});
        return nullptr;
    }

    bool is_cmp = (op == "==" || op == "!=" || op == "<"
                || op == "<=" || op == ">"  || op == ">=");

    if (lhs_float) {
        return tryFoldFloatBinary(node, lhs, rhs, op, is_cmp, diag);
    }
    if (is_cmp) {
        return foldIntCompare(node, lhs, rhs, op);
    }
    return foldIntArithBitwise(node, lhs, rhs, op, diag);
}

// Substitute a kIdentExpr that resolves to a kConst with a captured value.
// Returns true when the slot was rewritten.
bool trySubstituteConst(std::unique_ptr<parse::Node>& slot, parse::Tree& tree) {
    if (slot->kind != parse::Kind::kIdentExpr) return false;
    if (slot->resolved_entry_id < 0) return false;
    parse::Entry const& entry = tree.entries[slot->resolved_entry_id];
    if (entry.kind != parse::EntryKind::kConst) return false;
    if (entry.literal_text.empty()) return false;
    int file_id = slot->file_id;
    int tok = slot->tok;
    auto lit = std::make_unique<parse::Node>();
    lit->kind = entry.literal_kind;
    lit->text = entry.literal_text;
    lit->file_id = file_id;
    lit->tok = tok;
    slot = std::move(lit);
    return true;
}

// When a const decl's rhs has folded to a single literal, capture the value
// on the entry. Floats round-trip through the declared type for precision
// capture (3.14 -> float32 stored value ~ 3.1400001049). Ints/bools/chars
// store the literal text verbatim; range validation against the declared
// type runs here so out-of-range constants reject sharply rather than
// silently narrowing.
bool tryCaptureConst(parse::Node& decl, parse::Tree& tree, diagnostic::Sink& diag) {
    if (!decl.is_const) return false;
    if (decl.resolved_entry_id < 0) return false;
    parse::Entry& entry = tree.entries[decl.resolved_entry_id];
    if (entry.kind != parse::EntryKind::kConst) return false;
    if (!entry.literal_text.empty()) return false;  // already captured
    if (decl.children.empty()) return false;        // grammar should have rejected
    parse::Node const& rhs = *decl.children[0];

    std::string const& declared = entry.slids_type;

    // A string literal is a constant, but isLiteral (used below to mean "folded
    // to a numeric literal") excludes it — so without this branch a string init
    // would fall through to the fixpoint sweep's blunt "not a constant
    // expression", mis-attributing a type mismatch as non-constness. Report
    // precisely instead. (char[] string constants are a real value but their
    // capture / codegen isn't built yet — an honest "not yet supported", not a
    // type error.)
    if (rhs.kind == parse::Kind::kStringLiteral) {
        if (declared == "char[]") {
            diagnostic::report(diag, {decl.file_id, decl.tok,
                "String constants are not yet supported.", {}});
        } else {
            diagnostic::report(diag, {decl.file_id, decl.tok,
                "Constant '" + entry.name + "' has a string initializer, which "
                "does not match declared type '" + declared + "'.", {}});
        }
        return false;
    }

    if (!isLiteral(rhs)) return false;              // not yet folded

    if (rhs.kind == parse::Kind::kFloatLiteral) {
        if (!widen::floatLiteralFits(rhs.text, declared)) {
            diagnostic::report(diag, {decl.file_id, decl.tok,
                "Constant '" + entry.name + "' value '" + rhs.text
                + "' does not fit declared type '" + declared + "'.", {}});
            return false;
        }
        double d;
        if (!parseDouble(rhs.text, d)) return false;
        double captured;
        if (declared == "float" || declared == "float32") {
            // Round through float32 to capture the actual storage value.
            float f = static_cast<float>(d);
            captured = static_cast<double>(f);
        } else {
            // float64 — no rounding.
            captured = d;
        }
        entry.literal_text = floatToText(captured);
        entry.literal_kind = parse::Kind::kFloatLiteral;
        return true;
    }

    // Integer-class literal. Range check against the declared type, then
    // store verbatim with the literal's own kind. Bool's text was already
    // canonicalized by numeric to "1"/"0".
    std::string text_for_fit = rhs.text;
    if (rhs.kind == parse::Kind::kBoolLiteral) {
        // numeric canonicalizes bool to "1"/"0" already; pass through.
    }
    if (!widen::intLiteralFits(text_for_fit, declared)) {
        diagnostic::report(diag, {decl.file_id, decl.tok,
            "Constant '" + entry.name + "' value '" + rhs.text
            + "' does not fit declared type '" + declared + "'.", {}});
        return false;
    }
    entry.literal_text = rhs.text;
    entry.literal_kind = rhs.kind;
    return true;
}

void walk(std::unique_ptr<parse::Node>& slot, parse::Tree& tree,
          bool& changed, diagnostic::Sink& diag) {
    if (!slot) return;
    for (auto& c : slot->children) {
        walk(c, tree, changed, diag);
    }
    if (slot->kind == parse::Kind::kUnaryExpr) {
        if (auto folded = tryFoldUnary(*slot, diag)) {
            slot = std::move(folded);
            changed = true;
        }
    } else if (slot->kind == parse::Kind::kBinaryExpr) {
        if (auto folded = tryFoldBinary(*slot, diag)) {
            slot = std::move(folded);
            changed = true;
        }
    } else if (slot->kind == parse::Kind::kIdentExpr) {
        if (trySubstituteConst(slot, tree)) {
            changed = true;
        }
    }
    if (slot->kind == parse::Kind::kVarDeclStmt) {
        if (tryCaptureConst(*slot, tree, diag)) {
            changed = true;
        }
    }
    assignNominal(*slot);
}

}  // namespace

void run(parse::Tree& tree, diagnostic::Sink& diag) {
    // Iterate to a fixpoint: each round may substitute kConst idents
    // (exposing new literal-only sub-trees that the binary/unary folders
    // can then collapse) and capture const-decl values from now-folded
    // rhs expressions. Convergence is guaranteed because each round only
    // adds substitutions / captures; never removes them.
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& n : tree.nodes) {
            walk(n, tree, changed, diag);
        }
        // Stop at the first reported error. A capture that failed its range
        // check (tryCaptureConst) leaves literal_text empty, so a later round —
        // triggered by some other node folding — would re-enter and re-report
        // the same const. One round, one diagnostic.
        if (diagnostic::hasErrors(diag)) break;
    }
    // Fixpoint check: any kConst whose rhs never folded is unresolvable
    // (cyclic or refers to a non-constant). Emit ONE diagnostic at the
    // first unfolded entry — a cycle of N consts becomes one error block
    // rather than N. Caret lands at the ident via entry.tok (grammar
    // captured the ident's tok in name_tok; resolve stamped it on the
    // entry at creation time).
    //
    // Skip the sweep entirely if folding already reported an error: a capture
    // that failed its range check (tryCaptureConst) leaves literal_text empty,
    // and the primary "does not fit declared type" diagnostic already named it
    // — the cascade "not a constant expression" would be a spurious second
    // report on the same const.
    if (diagnostic::hasErrors(diag)) return;
    for (parse::Entry const& entry : tree.entries) {
        if (entry.kind != parse::EntryKind::kConst) continue;
        if (!entry.literal_text.empty()) continue;
        diagnostic::report(diag, {entry.file_id, entry.tok,
            "Initializer for '" + entry.name + "' is not a constant expression.",
            {}});
        return;
    }
}

}  // namespace constfold
