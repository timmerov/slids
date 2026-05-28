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
        return "";  // overflow; downstream catches via catch-all
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
        case parse::Kind::kReturnStmt:
        case parse::Kind::kStringLiteral:
        case parse::Kind::kIdentExpr:
        case parse::Kind::kUnaryExpr:
        case parse::Kind::kBinaryExpr:
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

// Binary fold for two integer-class literals. Mirrors v1's tryFoldIntBinary
// (signed int64 arithmetic), plus div/mod-by-zero rejection and int↔float
// mixing rejection.
std::unique_ptr<parse::Node> tryFoldBinary(parse::Node& node, diagnostic::Sink& diag) {
    if (node.kind != parse::Kind::kBinaryExpr) return nullptr;
    assert(node.children.size() == 2
        && "tryFoldBinary: BinaryExpr needs 2 children");
    parse::Node& lhs = *node.children[0];
    parse::Node& rhs = *node.children[1];
    std::string const& op = node.text;

    if (!isLiteral(lhs) || !isLiteral(rhs)) return nullptr;

    bool lhs_float = lhs.kind == parse::Kind::kFloatLiteral;
    bool rhs_float = rhs.kind == parse::Kind::kFloatLiteral;

    // fold.sl:35-36 no-mix: one float + one int-class is a compile error.
    if (lhs_float != rhs_float) {
        diagnostic::report(diag, {node.file_id, node.tok,
            "No common type for floating-point and integer-class literals; "
            "use an explicit type conversion.", {}});
        return nullptr;
    }

    // Float fold deferred — codegen handles float-literal expressions today.
    if (lhs_float) return nullptr;

    // Integer-class fold: signed int64 arithmetic (v1 parity). Logicals stay
    // for codegen; shifts and comparisons stay too (not in v1's fold scope).
    auto parseI64 = [](std::string const& t, int64_t& v) -> bool {
        if (t.empty()) return false;
        errno = 0;
        char* end = nullptr;
        v = (int64_t)std::strtoll(t.c_str(), &end, 10);
        if (end == t.c_str() || *end != '\0' || errno == ERANGE) return false;
        return true;
    };
    int64_t a = 0, b = 0;
    if (!parseI64(lhs.text, a)) return nullptr;
    if (!parseI64(rhs.text, b)) return nullptr;
    int64_t r = 0;
    if      (op == "+") r = a + b;
    else if (op == "-") r = a - b;
    else if (op == "*") r = a * b;
    else if (op == "/") {
        if (b == 0) {
            diagnostic::report(diag, {node.file_id, node.tok,
                "Division by zero.", {}});
            return nullptr;
        }
        r = a / b;
    }
    else if (op == "%") {
        if (b == 0) {
            diagnostic::report(diag, {node.file_id, node.tok,
                "Modulo by zero.", {}});
            return nullptr;
        }
        r = a % b;
    }
    else if (op == "&") r = a & b;
    else if (op == "|") r = a | b;
    else if (op == "^") r = a ^ b;
    else return nullptr;

    auto out = std::make_unique<parse::Node>();
    out->kind = parse::Kind::kIntLiteral;
    out->text = std::to_string(r);
    out->file_id = node.file_id;
    out->tok = node.tok;
    return out;
}

void walk(std::unique_ptr<parse::Node>& slot, diagnostic::Sink& diag) {
    if (!slot) return;
    for (auto& c : slot->children) {
        walk(c, diag);
    }
    if (slot->kind == parse::Kind::kUnaryExpr) {
        if (auto folded = tryFoldUnary(*slot, diag)) {
            slot = std::move(folded);
        }
    } else if (slot->kind == parse::Kind::kBinaryExpr) {
        if (auto folded = tryFoldBinary(*slot, diag)) {
            slot = std::move(folded);
        }
    }
    assignNominal(*slot);
}

}  // namespace

void run(parse::Tree& tree, diagnostic::Sink& diag) {
    for (auto& n : tree.nodes) {
        walk(n, diag);
    }
}

}  // namespace constfold
