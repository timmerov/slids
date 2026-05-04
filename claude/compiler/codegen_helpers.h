#pragma once
#include "parser.h"
#include <string>
#include <map>
#include <set>
#include <vector>
#include <algorithm>

// op symbols that have a meaningful compound-assign form (op<sym>=) — used by
// codegen to fuse `temp = lhs op rhs` into `temp op= rhs`. Comparison ops are
// excluded because appending `=` to `<` or `>` produces another comparison op,
// not a compound assign.
inline bool isCompoundableOp(const std::string& op) {
    static const std::set<std::string> ok = {
        "+", "-", "*", "/", "%",
        "&", "|", "^", "<<", ">>",
        "&&", "||", "^^"
    };
    return ok.count(op) > 0;
}

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

// Built-in scalar / pointer types — anything that isn't a slid, anon-tuple,
// or fixed-size array. Used by the for-loop inference rule: bare `for (x : ...)`
// over a non-primitive element type is a compile error.
inline bool isPrimitive(const std::string& t) {
    if (t.empty()) return false;
    if (isRefType(t) || isPtrType(t)) return true;
    static const std::set<std::string> ok = {
        "int", "int8", "int16", "int32", "int64",
        "uint", "uint8", "uint16", "uint32", "uint64",
        "intptr", "anyptr",
        "bool", "char",
        "float32", "float64",
    };
    return ok.count(t) > 0;
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

// Build the parameter-type token used inside a mangled function symbol.
// "T[]" -> "Ts" (chars, ints), "T^" -> "Tr" (charr, Stringr).
// Anon-tuple "(t1,t2,...)" -> "t_<token(t1)>_<token(t2)>_..._e", recursively.
// Single source of truth — every call/decl/define site that builds a mangled
// symbol must use this so consumer and instantiator TUs agree byte-for-byte.
inline std::string paramTokenForType(const std::string& t) {
    if (t.size() >= 2 && t.substr(t.size() - 2) == "[]")
        return paramTokenForType(t.substr(0, t.size() - 2)) + "s";
    if (!t.empty() && t.back() == '^')
        return paramTokenForType(t.substr(0, t.size() - 1)) + "r";
    if (isAnonTupleType(t)) {
        std::string s = "t";
        for (auto& elem : anonTupleElems(t))
            s += "_" + paramTokenForType(elem);
        s += "_e";
        return s;
    }
    return t;
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

// Result Slids type of a `new`-expression, or "" if `expr` is not one.
// Single source of truth for the three new-expression node types.
inline std::string newExprResultType(const Expr& expr) {
    if (auto* ne = dynamic_cast<const NewExpr*>(&expr))          return ne->elem_type + "[]";
    if (auto* ne = dynamic_cast<const NewScalarExpr*>(&expr))    return ne->elem_type + "^";
    if (auto* ne = dynamic_cast<const PlacementNewExpr*>(&expr)) return ne->elem_type + "^";
    return "";
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
