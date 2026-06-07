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
    if (n.nominal_type != widen::kNoType) return;  // already assigned (e.g. by a prior fold)
    switch (n.kind) {
        case parse::Kind::kIntLiteral: {
            bool neg = false;
            uint64_t mag = 0;
            if (parseSignedDigits(n.text, neg, mag)) {
                n.nominal_type = widen::internOrNone(nominalForInt(mag, neg));
            }
            return;
        }
        case parse::Kind::kUintLiteral: {
            bool neg = false;
            uint64_t mag = 0;
            if (parseSignedDigits(n.text, neg, mag) && !neg) {
                n.nominal_type = widen::internOrNone(nominalForUint(mag));
            }
            return;
        }
        case parse::Kind::kCharLiteral:
            n.nominal_type = widen::intern("uint8");
            return;
        case parse::Kind::kBoolLiteral:
            n.nominal_type = widen::intern("uint1");
            return;
        case parse::Kind::kFloatLiteral:
            n.nominal_type = widen::internOrNone(nominalForFloat(n.text));
            return;
        case parse::Kind::kProgram:
        case parse::Kind::kFunctionDef:
        case parse::Kind::kFunctionDecl:
        case parse::Kind::kVarDeclStmt:
        case parse::Kind::kAssignStmt:
        case parse::Kind::kAugAssignStmt:
        case parse::Kind::kStoreStmt:
        case parse::Kind::kMoveStmt:
        case parse::Kind::kSwapStmt:
        case parse::Kind::kDestructureStmt:
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
        case parse::Kind::kForLongStmt:
        case parse::Kind::kForEnumStmt:
        case parse::Kind::kBreakStmt:
        case parse::Kind::kContinueStmt:
        case parse::Kind::kSwitchStmt:
        case parse::Kind::kCaseClause:
        case parse::Kind::kStringLiteral:
        case parse::Kind::kNullptrLiteral:
        case parse::Kind::kIdentExpr:
        case parse::Kind::kUnaryExpr:
        case parse::Kind::kBinaryExpr:
        case parse::Kind::kPreIncExpr:
        case parse::Kind::kPostIncExpr:
        case parse::Kind::kAddrOfExpr:
        case parse::Kind::kDerefExpr:
        case parse::Kind::kIndexExpr:
        case parse::Kind::kTupleExpr:
        case parse::Kind::kCastExpr:
        case parse::Kind::kConvertExpr:
        case parse::Kind::kNewExpr:
        case parse::Kind::kDeleteStmt:
        case parse::Kind::kSizeofExpr:
        case parse::Kind::kStringifyType:
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
        child->nominal_type = widen::kNoType;  // recompute by value
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
        child->nominal_type = widen::kNoType;
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
        widen::TypeRef operand_nominal = operand.nominal_type;
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
        out->nominal_type = widen::intern("uint1");
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

// Strength of a binary result: strong if either operand was strong (a typed
// const), taking the strong operand's type — or, when both are strong, their
// common type (widen-within-family). Empty = weak.
widen::TypeRef combineStrong(parse::Node const& a, parse::Node const& b) {
    if (a.strong_type == widen::kNoType) return b.strong_type;
    if (b.strong_type == widen::kNoType) return a.strong_type;
    std::string out;
    if (widen::commonType(widen::spell(a.strong_type), widen::spell(b.strong_type), out))
        return widen::internOrNone(out);
    // No common type — commonType only fails cross-family (int vs float). For
    // arith/compare that's pre-empted by tryFoldBinary's no-mix guard, so the
    // sole path here is the shift carve-out (e.g. float lhs, int rhs), where the
    // result type is the lhs by definition — taking a.strong_type is correct,
    // not a silent pick.
    assert(a.strong_type != widen::kNoType && b.strong_type != widen::kNoType
        && "combineStrong: fallback expects two strong operands");
    return a.strong_type;
}

// The preferred user-facing default type for a WEAK const (a named literal):
// int/uint/float for the 32-bit defaults, widening only when the magnitude
// requires it. The narrowest nominal is kept separately (assignNominal).
std::string weakDefaultType(parse::Node const& lit) {
    if (lit.kind == parse::Kind::kBoolLiteral)  return "bool";
    if (lit.kind == parse::Kind::kCharLiteral)  return "char";
    if (lit.kind == parse::Kind::kFloatLiteral) return "float";
    bool neg = false;
    uint64_t mag = 0;
    if (lit.kind == parse::Kind::kUintLiteral) {
        if (parseSignedDigits(lit.text, neg, mag) && mag > (uint64_t)UINT32_MAX)
            return "uint64";
        return "uint";
    }
    if (!parseSignedDigits(lit.text, neg, mag)) return "int";
    if (neg) return (mag <= (uint64_t)INT32_MAX + 1) ? "int" : "int64";
    if (mag <= (uint64_t)INT32_MAX) return "int";
    if (mag <= (uint64_t)INT64_MAX) return "int64";
    return "uint64";
}

std::unique_ptr<parse::Node> makeLitAt(parse::Node const& src, parse::Kind kind,
                                        std::string text) {
    auto out = std::make_unique<parse::Node>();
    out->kind = kind;
    out->text = std::move(text);
    out->file_id = src.file_id;
    out->tok = src.tok;
    // Strength rides through arith/bitwise folds (a strong/typed const operand
    // makes the result strong). A bool result (comparison) is always weak.
    if (kind != parse::Kind::kBoolLiteral && src.children.size() == 2) {
        out->strong_type = combineStrong(*src.children[0], *src.children[1]);
    }
    return out;
}

std::string floatToText(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

// ---- const + const result kind + strength (spec: test_v2/constfold/constant.sl)
// The folded operands' literal KIND maps to a constant kind: kBoolLiteral=bool,
// kCharLiteral=char, kIntLiteral=integer (signed), kUintLiteral=unsigned,
// kFloatLiteral=float. The result of a binary op gets a KIND (matrix below, then
// promoted to hold the value) and a TYPE (strong or flex/weak).

// Matrix base kind for an integer-class binary op (before value-fit promotion),
// symmetric: char preferred; bool yields to its partner; a signed/unsigned mix
// is signed; otherwise the shared kind.
parse::Kind baseConstKind(parse::Kind lk, parse::Kind rk) {
    using K = parse::Kind;
    if (lk == K::kCharLiteral || rk == K::kCharLiteral) return K::kCharLiteral;
    if (lk == K::kBoolLiteral && rk == K::kBoolLiteral)  return K::kBoolLiteral;
    if (lk == K::kBoolLiteral) return rk;
    if (rk == K::kBoolLiteral) return lk;
    if (lk == K::kUintLiteral && rk == K::kUintLiteral)  return K::kUintLiteral;
    return K::kIntLiteral;
}

// Promote the base kind so it stays consistent with the value `val` (decimal
// text): bool not 0/1 -> integer; char not 0..255 -> integer; unsigned negative
// -> integer; integer past int64 -> unsigned (uint64).
parse::Kind promoteConstKind(parse::Kind base, std::string const& val) {
    using K = parse::Kind;
    bool neg = !val.empty() && val[0] == '-';
    if (base == K::kBoolLiteral && val != "0" && val != "1") base = K::kIntLiteral;
    if (base == K::kCharLiteral && !widen::intLiteralFits(val, "char")) base = K::kIntLiteral;
    if (base == K::kUintLiteral && neg) base = K::kIntLiteral;
    if (base == K::kIntLiteral && !widen::intLiteralFits(val, "int64")) base = K::kUintLiteral;
    return base;
}

// An operand's strong type for the purpose of an INTEGER/UNSIGNED result's
// strength: bool/char strong types don't propagate into an integer result (they
// only make a bool/char result strong), so treat them as flex here.
widen::TypeRef famStrong(parse::Node const& n) {
    if (n.strong_type == widen::kNoType
        || n.strong_type == widen::intern("bool")
        || n.strong_type == widen::intern("char"))
        return widen::kNoType;
    return n.strong_type;
}

// Strength (strong type, or "" for flex) of an integer-class binary result.
// bool/char result -> always strong. A kind PROMOTED to fit the value -> flex.
// Otherwise: flex+flex -> flex; strong+flex -> the strong type if the value fits
// it, else flex; both strong same-sign -> their common type (widen-within-family,
// honoring an explicit width) if the value fits, else flex; mixed sign -> flex.
std::string constResultStrong(parse::Node const& lhs, parse::Node const& rhs,
                              parse::Kind base, parse::Kind result,
                              std::string const& val) {
    using K = parse::Kind;
    if (result == K::kBoolLiteral) return "bool";
    if (result == K::kCharLiteral) return "char";
    if (result != base) return "";                 // promoted to fit -> flex
    std::string a = widen::spellOrEmpty(famStrong(lhs)), b = widen::spellOrEmpty(famStrong(rhs));
    if (a.empty() && b.empty()) return "";          // flex + flex
    if (a.empty() || b.empty()) {                   // strong + flex
        std::string s = a.empty() ? b : a;
        return widen::intLiteralFits(val, s) ? s : "";
    }
    widen::TypeKind ka, kb;                          // both strong
    if (!widen::classify(a, ka) || !widen::classify(b, kb)) return "";
    if ((ka.cat == widen::Category::kSignedInt)
        != (kb.cat == widen::Category::kSignedInt)) return "";   // mixed sign -> flex
    std::string out;
    if (!widen::commonType(a, b, out)) return "";
    return widen::intLiteralFits(val, out) ? out : "";
}

// Strength of a FLOAT binary result (no kind promotion, no sign): flex+flex ->
// flex; strong+flex -> the strong float type if the value fits, else flex; both
// strong -> the wider float type if it fits, else flex.
std::string floatResultStrong(parse::Node const& lhs, parse::Node const& rhs,
                              std::string const& val) {
    std::string a = widen::spellOrEmpty(famStrong(lhs)), b = widen::spellOrEmpty(famStrong(rhs));
    if (a.empty() && b.empty()) return "";
    if (a.empty() || b.empty()) {
        std::string s = a.empty() ? b : a;
        return widen::floatLiteralFits(val, s) ? s : "";
    }
    std::string out;
    if (!widen::commonType(a, b, out)) return "";
    return widen::floatLiteralFits(val, out) ? out : "";
}

// Emit a folded SHIFT result of value `val`: kind and strength follow the LEFT
// operand (then the value-fit promotion). A bool/char result is strong; a
// promoted kind is flex; otherwise the lhs's strong type if the value fits, else
// flex.
std::unique_ptr<parse::Node> emitShiftResult(parse::Node& node, parse::Node& lhs,
                                             std::string val) {
    parse::Kind base = lhs.kind;
    parse::Kind kind = promoteConstKind(base, val);
    auto out = makeLitAt(node, kind, std::move(val));
    std::string st;
    if (kind == parse::Kind::kBoolLiteral)      st = "bool";
    else if (kind == parse::Kind::kCharLiteral) st = "char";
    else if (kind != base)                      st = "";   // promoted -> flex
    else {
        std::string a = widen::spellOrEmpty(famStrong(lhs));
        st = (!a.empty() && widen::intLiteralFits(out->text, a)) ? a : "";
    }
    out->strong_type = widen::internOrNone(st);
    return out;
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
    auto out = makeLitAt(node, parse::Kind::kFloatLiteral, floatToText(r));
    out->strong_type = widen::internOrNone(floatResultStrong(lhs, rhs, out->text));
    return out;
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
        return emitShiftResult(node, lhs, "0");
    }
    int count = static_cast<int>(count_mag);
    bool lhs_neg = false;
    uint64_t lhs_mag = 0;
    if (!parseSignedDigits(lhs.text, lhs_neg, lhs_mag)) return nullptr;
    bool lhs_signed = (lhs.kind == parse::Kind::kIntLiteral);

    if (op == "<<") {
        // Compute in uint64 to avoid signed-overflow UB; a signed lhs reinterprets
        // the bits as int64. emitShiftResult settles kind + strength off the lhs.
        uint64_t v = lhs_neg ? (0ULL - lhs_mag) : lhs_mag;
        uint64_t r = v << count;
        std::string val = lhs_signed ? std::to_string(static_cast<int64_t>(r))
                                      : std::to_string(r);
        return emitShiftResult(node, lhs, std::move(val));
    }
    // >>
    std::string val;
    if (lhs_signed) {
        int64_t sv = lhs_neg ? -static_cast<int64_t>(lhs_mag)
                             :  static_cast<int64_t>(lhs_mag);
        val = std::to_string(sv >> count);   // arithmetic right shift
    } else {
        val = std::to_string(lhs_mag >> count);
    }
    return emitShiftResult(node, lhs, std::move(val));
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

// Emit a folded integer-class result of value `val` (decimal text): the result
// KIND comes from the operand-kind matrix promoted to hold the value, and the
// strong/flex TYPE from constResultStrong (the const+const rules).
std::unique_ptr<parse::Node> emitIntResult(parse::Node& node, parse::Node& lhs,
                                           parse::Node& rhs, std::string val) {
    parse::Kind base = baseConstKind(lhs.kind, rhs.kind);
    parse::Kind kind = promoteConstKind(base, val);
    auto out = makeLitAt(node, kind, std::move(val));
    out->strong_type = widen::internOrNone(constResultStrong(lhs, rhs, base, kind, out->text));
    return out;
}

// D4 — int-class arith/bitwise. Computes the value, then settles kind + strength
// per the const+const rules (rule-6 overflow-to-unsigned falls out of the
// integer->unsigned value-fit promotion).
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
            std::string val = (op == "/")
                ? std::to_string(static_cast<uint64_t>(INT64_MAX) + 1ULL) : "0";
            return emitIntResult(node, lhs, rhs, std::move(val));
        }
        return emitIntResult(node, lhs, rhs,
                             std::to_string((op == "/") ? (a / b) : (a % b)));
    }

    if (op == "&" || op == "|" || op == "^") {
        int64_t r;
        if      (op == "&") r = a & b;
        else if (op == "|") r = a | b;
        else /*    ^   */   r = a ^ b;
        return emitIntResult(node, lhs, rhs, std::to_string(r));
    }

    // + - * with rule-6 overflow detection.
    int64_t r;
    bool overflow;
    if      (op == "+") overflow = __builtin_add_overflow(a, b, &r);
    else if (op == "-") overflow = __builtin_sub_overflow(a, b, &r);
    else if (op == "*") overflow = __builtin_mul_overflow(a, b, &r);
    else return nullptr;

    if (!overflow) {
        return emitIntResult(node, lhs, rhs, std::to_string(r));
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
    return emitIntResult(node, lhs, rhs, std::to_string(ur));
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
    lit->strong_type = entry.const_strong_type;   // strong const -> strong literal
    lit->alias_label = entry.alias_label;         // carry the const's type label
    slot = std::move(lit);
    return true;
}

// When a const decl's rhs has folded to a single literal, capture the value
// on the entry. Floats round-trip through the declared type for precision
// capture (3.14 -> float32 stored value ~ 3.1400001049). Ints/bools/chars
// store the literal text verbatim; range validation against the declared
// type runs here so out-of-range constants reject sharply rather than
// silently narrowing.
// The literal-node kind that represents a value of the given (resolved) type.
// A const's stored value takes its DECLARED type's kind, not the initializer's:
// `const char x = 65` prints as a char, `const bool b = 1` as `true`, and an
// `enum char` member keeps char through its auto-incremented (int-folded) value.
parse::Kind literalKindForType(std::string const& t) {
    if (t == "char") return parse::Kind::kCharLiteral;
    if (t == "bool") return parse::Kind::kBoolLiteral;
    if (t == "float" || t == "float32" || t == "float64")
        return parse::Kind::kFloatLiteral;
    if (t.compare(0, 4, "uint") == 0) return parse::Kind::kUintLiteral;
    return parse::Kind::kIntLiteral;
}

bool tryCaptureConst(parse::Node& decl, parse::Tree& tree, diagnostic::Sink& diag) {
    if (!decl.is_const) return false;
    if (decl.resolved_entry_id < 0) return false;
    parse::Entry& entry = tree.entries[decl.resolved_entry_id];
    if (entry.kind != parse::EntryKind::kConst) return false;
    if (!entry.literal_text.empty()) return false;  // already captured
    if (decl.children.empty()) return false;        // grammar should have rejected
    parse::Node const& rhs = *decl.children[0];

    // Typeless const: infer slids_type from the folded rhs before the capture
    // below range-checks against it. A strong rhs (carried a typed const) takes
    // that type; a bare literal is WEAK — present the preferred default spelling,
    // keep the narrowest nominal under the hood.
    if (entry.slids_type == widen::kNoType && isLiteral(rhs)) {
        entry.slids_type = (rhs.strong_type == widen::kNoType)
            ? widen::internOrNone(weakDefaultType(rhs)) : rhs.strong_type;
        entry.const_strong_type = rhs.strong_type;   // kNoType = weak
    } else if (decl.return_type != widen::kNoType) {
        // An explicitly-typed const is strong: it carries its declared type when
        // substituted into another const's rhs.
        entry.const_strong_type = entry.slids_type;
    }

    std::string declared = widen::spellOrEmpty(entry.slids_type);
    // A typeless const's type was inferred from the rhs, not declared — word the
    // range-check diagnostics accordingly.
    char const* type_word =
        decl.return_type == widen::kNoType ? "inferred type" : "declared type";

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
        if (!widen::floatLiteralFits(rhs.text, entry.slids_type)) {   // sees through alias
            diagnostic::report(diag, {decl.file_id, decl.tok,
                "Constant '" + entry.name + "' value '" + rhs.text
                + "' does not fit " + type_word + " '" + declared + "'.", {}});
            return false;
        }
        double d;
        if (!parseDouble(rhs.text, d)) return false;
        double captured;
        widen::TypeKind fk;
        bool is_f32 = widen::classify(entry.slids_type, fk)
                   && fk.cat == widen::Category::kFloat && fk.bits == 32;
        if (is_f32) {
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
    if (!widen::intLiteralFits(text_for_fit, entry.slids_type)) {   // sees through alias
        diagnostic::report(diag, {decl.file_id, decl.tok,
            "Constant '" + entry.name + "' value '" + rhs.text
            + "' does not fit " + type_word + " '" + declared + "'.", {}});
        return false;
    }
    entry.literal_text = rhs.text;
    // The stored value takes the const's DECLARED type's kind, not the folded
    // initializer's — so a char const stays char (incl. an enum char member
    // whose auto-increment folded to an int literal), a bool prints true/false.
    entry.literal_kind = literalKindForType(widen::spell(widen::strip(entry.slids_type)));
    return true;
}

// sizeof(...) folds to a strong `intptr` constant whenever its size is known
// without type inference — a type operand (return_type), a string literal,
// nullptr, an address-of (always a pointer), or a plain ident (its declared
// type). Folding HERE (before const capture) lets sizeof initialize a const or
// size an array dimension. Operands that need inference — a deref, an index,
// arithmetic — and a slid type are left as kSizeofExpr for classify. Like
// ##type, sizeof never evaluates its operand, so the operand is not substituted.
std::unique_ptr<parse::Node> tryFoldSizeof(parse::Node& n, parse::Tree& tree) {
    long long size = -1;
    if (!n.children.empty()
        && n.children[0]->kind == parse::Kind::kStringLiteral) {
        size = static_cast<long long>(n.children[0]->text.size()) + 1;  // + null
    } else if (n.return_type != widen::kNoType) {
        size = widen::typeByteSize(n.return_type);
    } else if (!n.children.empty()) {
        parse::Node const& op = *n.children[0];
        if (op.kind == parse::Kind::kNullptrLiteral
            || op.kind == parse::Kind::kAddrOfExpr) {
            size = 8;   // any pointer / iterator
        } else if (op.kind == parse::Kind::kIdentExpr
                   && op.resolved_entry_id >= 0) {
            widen::TypeRef ty = tree.entries[op.resolved_entry_id].slids_type;
            if (ty != widen::kNoType) size = widen::typeByteSize(ty);
        }
    }
    if (size < 0) return nullptr;   // needs inference / a slid type -> classify
    auto lit = std::make_unique<parse::Node>();
    lit->kind = parse::Kind::kIntLiteral;
    lit->text = std::to_string(size);
    lit->file_id = n.file_id;
    lit->tok = n.tok;
    lit->strong_type = widen::intern("intptr");   // sizeof is a strong intptr constant
    return lit;
}

// `(Type=literal)` value conversion folds to a STRONG target-typed literal with
// C semantics: anything->bool is a nonzero test; ->float converts the value
// (rounded to the float's precision); ->intN takes the low N bits and interprets
// them per the target signedness (float source truncates toward zero first). A
// pointer operand (nullptr / an address / a non-constant) never folds — those
// are lowered at runtime by codegen. Folding lets a conversion initialize a
// const or size an array dimension.
std::unique_ptr<parse::Node> tryFoldConvert(parse::Node& n) {
    assert(n.children.size() == 1 && "kConvertExpr needs 1 operand");
    parse::Node const& op = *n.children[0];
    widen::TypeKind tk;
    if (!widen::classify(n.return_type, tk)) return nullptr;  // non-value target -> classify errors

    using K = parse::Kind;
    bool int_src = (op.kind == K::kIntLiteral || op.kind == K::kUintLiteral
                 || op.kind == K::kCharLiteral || op.kind == K::kBoolLiteral);
    bool float_src = (op.kind == K::kFloatLiteral);
    if (!int_src && !float_src) return nullptr;  // ident / pointer / non-constant -> runtime

    bool neg = false;
    uint64_t mag = 0;
    double fv = 0.0;
    if (int_src) {
        // A numeric-stage-canonicalized int/uint/char/bool literal always parses
        // (bool is "1"/"0", char is its numeric code); the bail is defensive —
        // not folding is always safe (codegen lowers it). user notified, accepts state.
        if (!parseSignedDigits(op.text, neg, mag)) return nullptr;
    } else {
        errno = 0;
        fv = std::strtod(op.text.c_str(), nullptr);
        if (errno == ERANGE || !std::isfinite(fv)) return nullptr;
    }

    auto lit = std::make_unique<parse::Node>();
    lit->file_id = n.file_id;
    lit->tok = n.tok;
    lit->strong_type = n.return_type;   // a conversion yields a STRONG typed value

    if (tk.cat == widen::Category::kBool) {
        bool nz = int_src ? (mag != 0) : (fv != 0.0);
        lit->kind = K::kBoolLiteral;
        lit->text = nz ? "1" : "0";
        return lit;
    }
    if (tk.cat == widen::Category::kFloat) {
        double v = int_src ? (neg ? -static_cast<double>(mag) : static_cast<double>(mag)) : fv;
        if (tk.bits == 32) v = static_cast<double>(static_cast<float>(v));   // round to float precision
        lit->kind = K::kFloatLiteral;
        lit->text = floatToText(v);
        return lit;
    }
    // Integer target. Build the source's 64-bit two's-complement pattern, then
    // mask to the target width and sign-extend if the target is signed.
    uint64_t bits;
    if (int_src) {
        bits = neg ? (0ULL - mag) : mag;
    } else {
        double t = std::trunc(fv);
        if (t >= 9223372036854775808.0 || t < -9223372036854775808.0) {
            return nullptr;   // out of int64 range — leave the operand to runtime
        }
        bits = static_cast<uint64_t>(static_cast<int64_t>(t));
    }
    if (tk.bits < 64) {
        uint64_t mask = (1ULL << tk.bits) - 1ULL;
        bits &= mask;
        if (tk.cat == widen::Category::kSignedInt && (bits & (1ULL << (tk.bits - 1)))) {
            bits |= ~mask;   // sign-extend the in-range pattern
        }
    }
    if (tk.cat == widen::Category::kSignedInt) {
        lit->kind = K::kIntLiteral;
        lit->text = std::to_string(static_cast<int64_t>(bits));
    } else {
        // char is an unsigned-8 too — keep it a char literal so it types/prints
        // as char; its text is the numeric code (the lexer's canonical form).
        lit->kind = (widen::spellOrEmpty(widen::deepStrip(n.return_type)) == "char")
                  ? K::kCharLiteral : K::kUintLiteral;
        lit->text = std::to_string(bits);
    }
    return lit;
}

void bakeNodeDims(parse::Node& n, parse::Tree& tree, bool final,
                  bool& changed, diagnostic::Sink& diag);

// `no_substitute` (set inside a ##type operand): fold pure-literal subtrees but
// do NOT substitute a kConst ident to its value. ##type reports the type of its
// operand as written, so substituting a const would erase its const-qualified
// type; but a literal-only subtree (`'A' + 1`) SHOULD fold so ##type reports the
// folded result's type (e.g. char), matching a const/inferred initializer. A
// const left unsubstituted stays a kIdentExpr, so any binary containing it isn't
// all-literal and won't fold.
void walk(std::unique_ptr<parse::Node>& slot, parse::Tree& tree,
          bool& changed, diagnostic::Sink& diag, bool no_substitute = false) {
    if (!slot) return;
    if (slot->kind == parse::Kind::kStringifyType) {
        for (auto& c : slot->children) {
            walk(c, tree, changed, diag, /*no_substitute=*/true);
        }
        return;
    }
    // sizeof likewise never evaluates its operand — fold it in place to an intptr
    // constant where statically known, without substituting inside the operand.
    if (slot->kind == parse::Kind::kSizeofExpr) {
        if (auto folded = tryFoldSizeof(*slot, tree)) {
            slot = std::move(folded);
            changed = true;
            assignNominal(*slot);
        }
        return;
    }
    for (auto& c : slot->children) {
        walk(c, tree, changed, diag, no_substitute);
    }
    // Fold inside parameter defaults too (params live in a separate vector, not
    // children) so a constant-expression default reaches a literal.
    for (auto& p : slot->params) {
        walk(p, tree, changed, diag, no_substitute);
    }
    // Fold const-expression array dims (a separate vector) so they reach a
    // literal; bakeArrayDims (after the fixpoint) splices the size into the type.
    for (auto& d : slot->dim_exprs) {
        walk(d, tree, changed, diag, no_substitute);
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
    } else if (slot->kind == parse::Kind::kConvertExpr) {
        if (auto folded = tryFoldConvert(*slot)) {
            slot = std::move(folded);
            changed = true;
        }
    } else if (slot->kind == parse::Kind::kIdentExpr) {
        if (!no_substitute && trySubstituteConst(slot, tree)) {
            changed = true;
        }
    }
    if (slot->kind == parse::Kind::kVarDeclStmt) {
        if (tryCaptureConst(*slot, tree, diag)) {
            changed = true;
        }
        // Bake const-expression array dims as soon as they've folded (so the array
        // type is correct before a later sizeof of it folds). final=false: a dim
        // not yet folded is left for a later round.
        bakeNodeDims(*slot, tree, /*final=*/false, changed, diag);
    }
    assignNominal(*slot);
}

// Splice a var-decl's folded const-expression dims into its type spelling (and
// entry). dim_exprs is aligned to the spelling's dims, left to right (a null slot
// is a literal dim, already baked). Each must have folded to a POSITIVE INTEGER
// literal. `final` (the post-fixpoint pass): a dim that STILL isn't a literal is a
// non-constant array size -> error. During the fold fixpoint (final=false) an
// unfolded dim is just left for a later round — important so a dim like
// `sizeof(int)` bakes the array BEFORE a `sizeof(thatArray)` elsewhere folds.
void bakeNodeDims(parse::Node& n, parse::Tree& tree, bool final,
                  bool& changed, diagnostic::Sink& diag) {
    if (n.dim_exprs.empty()) return;
    for (auto& d : n.dim_exprs) {
        if (d && !isLiteral(*d)) {
            if (!final) return;             // not folded yet — wait for a later round
            diagnostic::report(diag, {d->file_id, d->tok,
                "Array size must be an integer constant.", {}});
            n.dim_exprs.clear();
            return;
        }
    }
    std::string t = widen::spellOrEmpty(n.return_type);
    std::size_t base = t.find('[');
    std::string out = (base == std::string::npos) ? t : t.substr(0, base);
    std::size_t pos = out.size();
    std::size_t i = 0;
    bool failed = false;
    while (pos < t.size() && t[pos] == '[') {
        std::size_t rb = t.find(']', pos);
        if (rb == std::string::npos) break;
        std::string content = t.substr(pos + 1, rb - pos - 1);
        if (i < n.dim_exprs.size() && n.dim_exprs[i]) {
            parse::Node const& d = *n.dim_exprs[i];
            bool int_lit = d.kind == parse::Kind::kIntLiteral
                        || d.kind == parse::Kind::kUintLiteral
                        || d.kind == parse::Kind::kCharLiteral;
            int64_t v = 0;
            if (!int_lit || !parseI64(d.text, v)) {
                diagnostic::report(diag, {d.file_id, d.tok,
                    "Array size must be an integer constant.", {}});
                failed = true;
            } else if (v <= 0) {
                diagnostic::report(diag, {d.file_id, d.tok,
                    "Array size must be a positive integer constant.", {}});
                failed = true;
            } else {
                content = d.text;
            }
        }
        out += "[" + content + "]";
        pos = rb + 1;
        i++;
    }
    out += t.substr(pos);   // trailing `^` / `[]` suffix, if any
    if (!failed) {
        // Update BOTH the node spelling and the symbol-table entry — downstream
        // (classify bounds checks, codegen, sizeof) reads the array type from the
        // entry's slids_type, which resolve stamped with the provisional `[1]`.
        if (n.resolved_entry_id >= 0)
            tree.entries[n.resolved_entry_id].slids_type = widen::internOrNone(out);
        n.return_type = widen::internOrNone(out);
        changed = true;
    }
    n.dim_exprs.clear();
}

// Post-fixpoint sweep: bake any remaining dims (final — an unfolded dim errors).
void finalizeArrayDims(parse::Node& n, parse::Tree& tree, diagnostic::Sink& diag) {
    for (auto& c : n.children) if (c) finalizeArrayDims(*c, tree, diag);
    for (auto& p : n.params)   if (p) finalizeArrayDims(*p, tree, diag);
    bool dummy = false;
    bakeNodeDims(n, tree, /*final=*/true, dummy, diag);
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
    // Bake const-expression array dims now the fold has reached its fixpoint (the
    // dim exprs have folded as far as they will). Skip if folding already errored.
    if (!diagnostic::hasErrors(diag)) {
        for (auto& n : tree.nodes) finalizeArrayDims(*n, tree, diag);
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
