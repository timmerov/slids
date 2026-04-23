#pragma once
#include "parser.h"
#include <string>
#include <map>
#include <vector>
#include <algorithm>

// type ends in ^ — reference, no arithmetic
inline bool isRefType(const std::string& t) {
    return !t.empty() && t.back() == '^';
}
// type ends in [] — pointer, arithmetic allowed
inline bool isPtrType(const std::string& t) {
    return t.size() >= 2 && t.substr(t.size()-2) == "[]";
}
// inline fixed-size array: T[N]
inline bool isInlineArrayType(const std::string& t) {
    auto lb = t.rfind('[');
    if (lb == std::string::npos || lb == 0 || t.back() != ']') return false;
    auto sz = t.substr(lb + 1, t.size() - lb - 2);
    return !sz.empty() && std::all_of(sz.begin(), sz.end(), ::isdigit);
}
// either indirect type
inline bool isIndirectType(const std::string& t) {
    return isRefType(t) || isPtrType(t) || isInlineArrayType(t);
}

// Quote a global/function name if it contains non-identifier characters (e.g. "op+")
inline std::string llvmGlobalName(const std::string& name) {
    for (char c : name) {
        if (!isalnum((unsigned char)c) && c != '_' && c != '.') {
            return "\"" + name + "\"";
        }
    }
    return name;
}

inline std::string llvmEscape(const std::string& s, int& len) {
    std::string result;
    len = 0;
    for (char c : s) {
        if (c == '\n')      { result += "\\0A"; }
        else if (c == '\t') { result += "\\09"; }
        else if (c == '"')  { result += "\\22"; }
        else if (c == '\\') { result += "\\5C"; }
        else                { result += c; }
        len++;
    }
    result += "\\00";
    len++;
    return result;
}

// true if `t` is an anonymous tuple type: "(t1,t2,...)"
inline bool isAnonTupleType(const std::string& t) {
    return t.size() >= 2 && t.front() == '(' && t.back() == ')';
}

// split "(t1,t2,...)" into element type strings, respecting nested parens.
inline std::vector<std::string> anonTupleElems(const std::string& t) {
    std::vector<std::string> out;
    if (!isAnonTupleType(t)) return out;
    std::string inner = t.substr(1, t.size() - 2);
    int depth = 0;
    std::string cur;
    for (char c : inner) {
        if (c == '(') { depth++; cur += c; }
        else if (c == ')') { depth--; cur += c; }
        else if (c == ',' && depth == 0) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Resolve a constant expression (integer literal or enum value) to an int
// without emitting any IR. Returns false if not constant.
inline bool constExprToInt(const Expr& expr,
                            const std::map<std::string, int>& enum_values,
                            int& out) {
    if (auto* il = dynamic_cast<const IntLiteralExpr*>(&expr)) {
        out = il->value; return true;
    }
    if (auto* ve = dynamic_cast<const VarExpr*>(&expr)) {
        auto it = enum_values.find(ve->name);
        if (it != enum_values.end()) { out = it->second; return true; }
    }
    if (auto* ue = dynamic_cast<const UnaryExpr*>(&expr)) {
        if (ue->op == "-") {
            int v; if (constExprToInt(*ue->operand, enum_values, v)) { out = -v; return true; }
        }
        if (ue->op == "+") {
            int v; if (constExprToInt(*ue->operand, enum_values, v)) { out = v; return true; }
        }
        if (ue->op == "~") {
            int v; if (constExprToInt(*ue->operand, enum_values, v)) { out = ~v; return true; }
        }
    }
    if (auto* be = dynamic_cast<const BinaryExpr*>(&expr)) {
        int l, r;
        if (constExprToInt(*be->left, enum_values, l) && constExprToInt(*be->right, enum_values, r)) {
            if (be->op == "+") { out = l + r; return true; }
            if (be->op == "-") { out = l - r; return true; }
            if (be->op == "*") { out = l * r; return true; }
            if (be->op == "/") { if (r == 0) return false; out = l / r; return true; }
            if (be->op == "%") { if (r == 0) return false; out = l % r; return true; }
            if (be->op == "&") { out = l & r; return true; }
            if (be->op == "|") { out = l | r; return true; }
            if (be->op == "^") { out = l ^ r; return true; }
            if (be->op == "<<") { out = l << r; return true; }
            if (be->op == ">>") { out = l >> r; return true; }
        }
    }
    return false;
}

// Returns true if this expression already produces a 0/1 i32 truth value —
// i.e. it is a comparison or logical op. emitCondBool can skip the extra icmp.
inline bool isAlreadyBool(const Expr& expr) {
    if (auto* b = dynamic_cast<const BinaryExpr*>(&expr)) {
        return b->op=="==" || b->op=="!=" || b->op=="<"  || b->op==">"  ||
               b->op=="<=" || b->op==">=" || b->op=="&&" || b->op=="||" ||
               b->op=="^^";
    }
    if (auto* u = dynamic_cast<const UnaryExpr*>(&expr))
        return u->op == "!";
    return false;
}
