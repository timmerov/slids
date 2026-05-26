#include "desugar.h"

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
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
        case parse::Kind::kReturnStmt:    return ast::Kind::kReturnStmt;
        case parse::Kind::kStringLiteral: return ast::Kind::kStringLiteral;
        case parse::Kind::kIntLiteral:    return ast::Kind::kIntLiteral;
        case parse::Kind::kCharLiteral:   return ast::Kind::kCharLiteral;
        case parse::Kind::kBoolLiteral:   return ast::Kind::kBoolLiteral;
        case parse::Kind::kFloatLiteral:  return ast::Kind::kFloatLiteral;
        case parse::Kind::kIdentExpr:     return ast::Kind::kIdentExpr;
        case parse::Kind::kUnaryExpr:     return ast::Kind::kUnaryExpr;
        case parse::Kind::kBinaryExpr:    return ast::Kind::kBinaryExpr;
    }
    assert(false && "toAstKind: unhandled parse::Kind");
    __builtin_unreachable();
}

// Strip leading '+' from a literal text (e.g. "+5" → "5").
void stripLeadingPlus(std::string& text) {
    if (!text.empty() && text[0] == '+') text.erase(0, 1);
}

// Toggle the sign of a numeric-literal text. Works for both int and float
// spellings since both carry their sign as a leading '-' (or none).
void toggleSign(std::string& text) {
    if (text.empty()) return;
    if (text[0] == '+') text.erase(0, 1);
    if (!text.empty() && text[0] == '-') text.erase(0, 1);
    else text.insert(0, "-");
    if (text == "-0") text = "0";
}

// True if the integer-literal text represents zero (handles underscores,
// optional sign).
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

// Compute ~N as text. Range: parsed magnitudes must fit in uint64; results
// outside int64 leave the text unchanged and let widen surface the overflow.
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
        // ~(-N) = N - 1
        if (mag == 0) { text = "-1"; return; }
        text = std::to_string(mag - 1);
    } else {
        // ~N = -(N + 1). +1 cannot overflow uint64 unless mag == UINT64_MAX;
        // in that case we'd produce a value out of int64 range — leave the
        // expression unfolded and let widen catch it downstream.
        if (mag == UINT64_MAX) return;
        text = "-" + std::to_string(mag + 1);
    }
}

// If `node` is a foldable UnaryExpr(op, literal), return a fresh literal
// node carrying the folded result; otherwise return nullptr.
std::unique_ptr<ast::Node> tryFoldUnary(ast::Node& node) {
    if (node.kind != ast::Kind::kUnaryExpr) return nullptr;
    assert(node.children.size() == 1
        && "tryFoldUnary: UnaryExpr needs 1 child");
    ast::Node& operand = *node.children[0];
    std::string const& op = node.text;

    if (op == "+") {
        bool numeric =
               operand.kind == ast::Kind::kIntLiteral
            || operand.kind == ast::Kind::kCharLiteral
            || operand.kind == ast::Kind::kFloatLiteral;
        if (!numeric) return nullptr;
        auto child = std::move(node.children[0]);
        stripLeadingPlus(child->text);
        return child;
    }
    if (op == "-") {
        bool numeric =
               operand.kind == ast::Kind::kIntLiteral
            || operand.kind == ast::Kind::kCharLiteral
            || operand.kind == ast::Kind::kFloatLiteral;
        if (!numeric) return nullptr;
        auto child = std::move(node.children[0]);
        toggleSign(child->text);
        return child;
    }
    if (op == "~") {
        bool intish =
               operand.kind == ast::Kind::kIntLiteral
            || operand.kind == ast::Kind::kCharLiteral;
        if (!intish) return nullptr;
        auto child = std::move(node.children[0]);
        foldBitNotIntText(child->text);
        child->kind = ast::Kind::kIntLiteral;
        return child;
    }
    if (op == "!") {
        bool result;
        if (operand.kind == ast::Kind::kIntLiteral
         || operand.kind == ast::Kind::kCharLiteral) {
            result = isZeroIntText(operand.text);
        } else if (operand.kind == ast::Kind::kFloatLiteral) {
            result = isZeroFloatText(operand.text);
        } else if (operand.kind == ast::Kind::kBoolLiteral) {
            result = (operand.text == "false");
        } else {
            return nullptr;
        }
        auto out = std::make_unique<ast::Node>();
        out->kind = ast::Kind::kBoolLiteral;
        out->text = result ? "true" : "false";
        out->file_id = node.file_id;
        out->tok = node.tok;
        return out;
    }
    return nullptr;
}

// Rewrite `lhs op= rhs;` into `lhs = lhs op rhs;`. Only fires when `node` is
// a kAugAssignStmt. Lvalue is a bare ident today — when complex lvalues land
// (`arr[f()] += 1`), bind the lhs to a tmp here to avoid double-evaluation.
std::unique_ptr<ast::Node> tryDesugarAugAssign(ast::Node& node) {
    if (node.kind != ast::Kind::kAugAssignStmt) return nullptr;
    assert(node.children.size() == 1
        && "tryDesugarAugAssign: AugAssignStmt needs 1 rhs child");

    auto lhs_ref = std::make_unique<ast::Node>();
    lhs_ref->kind = ast::Kind::kIdentExpr;
    lhs_ref->name = node.name;
    lhs_ref->file_id = node.file_id;
    lhs_ref->tok = node.tok;

    auto binop = std::make_unique<ast::Node>();
    binop->kind = ast::Kind::kBinaryExpr;
    binop->text = node.text;
    binop->file_id = node.file_id;
    binop->tok = node.tok;
    binop->children.push_back(std::move(lhs_ref));
    binop->children.push_back(std::move(node.children[0]));

    auto out = std::make_unique<ast::Node>();
    out->kind = ast::Kind::kAssignStmt;
    out->name = std::move(node.name);
    out->file_id = node.file_id;
    out->tok = node.tok;
    out->children.push_back(std::move(binop));
    return out;
}

std::unique_ptr<ast::Node> copyNode(parse::Node const& p) {
    auto node = std::make_unique<ast::Node>();
    node->kind = toAstKind(p.kind);
    node->name = p.name;
    node->text = p.text;
    node->return_type = p.return_type;
    node->file_id = p.file_id;
    node->tok = p.tok;
    for (auto const& c : p.children) {
        node->children.push_back(copyNode(*c));
    }
    if (auto folded = tryFoldUnary(*node)) return folded;
    if (auto rewritten = tryDesugarAugAssign(*node)) return rewritten;
    return node;
}

}  // namespace

void run(parse::Tree const& in, ast::Tree& out, diagnostic::Sink& diag) {
    (void)diag;
    for (auto const& n : in.nodes) {
        out.nodes.push_back(copyNode(*n));
    }
}

}  // namespace desugar
