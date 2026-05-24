#pragma once
#include "token.h"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <functional>

// Strip every leading "const " qualifier substring from a type string.
// "const int" -> "int"; "const (const T)^" -> "T^"; non-pointer and pointer
// types are handled uniformly. The canonical form is used for overload-match,
// mangling, and dedup-key comparisons where const distinctions on by-value
// slots and pointee-const on pointer slots collapse together. Also strips
// redundant single-element paren wrappers — the "(const T)^" form produced
// by default-const-on-indirect-params collapses to "T^" after const-strip.
// Real anon-tuple types ("(t1,t2,...)") keep their parens.
inline std::string canonicalType(const std::string& s) {
    // First pass: strip every `const ` qualifier.
    std::string t;
    t.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        if (s.compare(i, 6, "const ") == 0) { i += 6; continue; }
        t.push_back(s[i++]);
    }
    // Second pass: recursively normalize structure. Strip redundant `(X)`
    // wrappers around a single type at every level, peel trailing `^`/`[]`
    // suffixes onto the head, and recurse into anon-tuple elements so a
    // paren-wrapped slot like `(char[], char[], (float64)^)` canonicalizes
    // to `(char[],char[],float64^)`. A naked outer-only strip is not enough
    // — type inference (`inferTypeArgs` → unify) recurses into tuple slots
    // and would otherwise bind `T → "(float64)"` instead of `T → "float64"`.
    std::function<std::string(const std::string&)> norm =
        [&](const std::string& u) -> std::string {
        std::string r = u;
        // Strip outer single-element parens. Loops in case of nested wrap.
        while (r.size() >= 2 && r.front() == '(') {
            int depth = 0; size_t close = 0; bool has_top_comma = false;
            for (size_t k = 0; k < r.size(); k++) {
                if (r[k] == '(') depth++;
                else if (r[k] == ')') { depth--; if (depth == 0) { close = k; break; } }
                else if (r[k] == ',' && depth == 1) has_top_comma = true;
            }
            if (close == 0 || has_top_comma) break;
            r = r.substr(1, close - 1) + r.substr(close + 1);
        }
        // Peel `[]` / `^` and recurse on the head.
        if (r.size() >= 2 && r.substr(r.size() - 2) == "[]")
            return norm(r.substr(0, r.size() - 2)) + "[]";
        if (!r.empty() && r.back() == '^')
            return norm(r.substr(0, r.size() - 1)) + "^";
        // Anon tuple: split on top-level commas, recurse on each element.
        if (r.size() >= 2 && r.front() == '(' && r.back() == ')') {
            std::vector<std::string> elems;
            int depth = 0; size_t start = 1;
            for (size_t k = 1; k + 1 < r.size(); k++) {
                if (r[k] == '(') depth++;
                else if (r[k] == ')') depth--;
                else if (r[k] == ',' && depth == 0) {
                    elems.push_back(r.substr(start, k - start));
                    start = k + 1;
                }
            }
            elems.push_back(r.substr(start, r.size() - 1 - start));
            std::string out = "(";
            for (size_t k = 0; k < elems.size(); k++) {
                if (k > 0) out += ",";
                std::string e = elems[k];
                // tolerate either ", " or "," element delimiters at input.
                while (!e.empty() && e.front() == ' ') e.erase(0, 1);
                out += norm(e);
            }
            return out + ")";
        }
        return r;
    };
    return norm(t);
}

// Returns true if the type string carries `const ` anywhere (pointee-const or
// by-value const). Used to reject const-arg → mutable-param bindings.
inline bool typeHasConst(const std::string& s) {
    return s.find("const ") != std::string::npos;
}

// Returns true if the type string is top-level const — i.e. the qualifier
// applies to the whole encoded type, not just an inner pointee. By the
// left-to-right binding rule: "const T^" is fully const (handle+pointee),
// "(const T)^" is reference-to-const (mutable handle, const pointee). Only
// the first form has top-level const. Used to reject rebinds (assign, delete,
// destructure-slot) where rebinding the storage itself is forbidden.
inline bool typeStartsWithConst(const std::string& s) {
    return s.rfind("const ", 0) == 0;
}

// Strip leading "const " and any wrapping "(const T)" parens from a slids
// type string, returning the bare slid type suitable for `slid_info_` lookup.
// Does NOT strip trailing ^/[] — use pointeeForLookup for that.
inline std::string baseSlidType(std::string t) {
    // Unwrap "(const X)" → "const X" first; downstream then strips "const ".
    if (t.size() >= 2 && t.front() == '(' && t.back() == ')') {
        int depth = 0; bool top_comma = false;
        for (size_t i = 1; i + 1 < t.size(); i++) {
            if (t[i] == '(') depth++;
            else if (t[i] == ')') depth--;
            else if (t[i] == ',' && depth == 0) { top_comma = true; break; }
        }
        if (!top_comma) {
            std::string inner = t.substr(1, t.size() - 2);
            if (inner.rfind("const ", 0) == 0) t = inner;
        }
    }
    if (t.rfind("const ", 0) == 0) t = t.substr(6);
    return t;
}

// True iff a value of slids type `src` widens to slids type `dst` without
// information loss. Integer ranks: bool=0, int8/uint8/char=1, int16/uint16=2,
// int32/uint32/int/uint=3, int64/uint64/intptr=4. Float ranks: float/float32=1,
// float64=2. Pointer types require exact equality. Exact `src == dst` is
// always true. Drift from the LLVM-level widen helper in codegen.cpp is
// possible — that one runs on LLVM types (i8/i16/...) at emit time; this
// runs on slids type names at parse/classify time. Reconcile if discovered.
inline bool widensTo(const std::string& src, const std::string& dst) {
    if (src == dst) return true;
    static const std::map<std::string,int> int_rank = {
        {"bool",0},
        {"int8",1},{"uint8",1},{"char",1},
        {"int16",2},{"uint16",2},
        {"int32",3},{"uint32",3},{"int",3},{"uint",3},
        {"int64",4},{"uint64",4},{"intptr",4}
    };
    static const std::map<std::string,int> float_rank = {
        {"float",1},{"float32",1},{"float64",2}
    };
    auto si = int_rank.find(src), di = int_rank.find(dst);
    if (si != int_rank.end() && di != int_rank.end()) return di->second >= si->second;
    auto sf = float_rank.find(src), df = float_rank.find(dst);
    if (sf != float_rank.end() && df != float_rank.end()) return df->second >= sf->second;
    return false;
}

// Strip one pointer suffix (^ or []) from a type string AND canonicalize the
// result for `slid_info_` lookup (drop leading const, unwrap "(const T)").
// Used wherever a pointer/iterator-typed local's pointee is consulted as a
// slid type — common pattern across method dispatch, deref-write, field
// access, etc.
inline std::string pointeeForLookup(const std::string& ptr_type) {
    std::string t = ptr_type;
    if (t.size() >= 2 && t.substr(t.size() - 2) == "[]") t = t.substr(0, t.size() - 2);
    else if (!t.empty() && t.back() == '^') t.pop_back();
    return baseSlidType(t);
}

// Extract pointee info from a pointer/iterator type: the canonical slid name
// for `slid_info_` lookup, and whether the pointee was const. Centralizes the
// "strip ^/[], unwrap (const T) paren, check const" trio that comes up at
// every site doing FieldAccess / method-dispatch through an indirect local.
// `is_const` is set when the pointee carries `const` anywhere (top-level or
// paren-wrapped) — used to propagate const through `obj.field` / `obj.method`.
struct PointeeInfo {
    std::string name;        // canonical slid type, suitable for slid_info_ lookup
    bool is_const = false;   // whether the pointee carries const
};
inline PointeeInfo pointeeInfo(const std::string& ptr_type) {
    std::string t = ptr_type;
    if (t.size() >= 2 && t.substr(t.size() - 2) == "[]") t = t.substr(0, t.size() - 2);
    else if (!t.empty() && t.back() == '^') t.pop_back();
    PointeeInfo r;
    r.is_const = typeHasConst(t);
    r.name = baseSlidType(t);
    return r;
}

// Propagate const through field access / index access. For an indirect type
// (`T^` / `T[]`) the result is the "reference to const" form `(const T)^` /
// `(const T)[]` — handle stays mutable (so a local that copies the field can
// be advanced/rebound), pointee carries const. For non-indirect types the
// value itself becomes const (`T` → `const T`). Already-const types are
// returned unchanged.
inline std::string propagateConst(const std::string& t) {
    if (t.empty() || t.find("const ") != std::string::npos) return t;
    if (t.size() >= 2 && t.substr(t.size() - 2) == "[]")
        return "(const " + t.substr(0, t.size() - 2) + ")[]";
    if (t.back() == '^')
        return "(const " + t.substr(0, t.size() - 1) + ")^";
    return "const " + t;
}

// Slids passes by value or reference-to-const. A pointer/iterator param
// without `mutable` is treated as `(const T)^` / `(const T)[]` inside the
// body — the HANDLE stays mutable (rebind / advance permitted), the POINTEE
// carries const (deref-write / delete-through rejected). `mutable` opts in
// to writes-through. By-value params (no ^/[]) are unaffected; their
// const-ness is opt-in via the declaration form `const T x`. From outside
// the function the qualifier collapses to canonical (overload identity
// unchanged).
// Compute the lhs type from the rhs type on a value-copy.
//
// Rule: a copy yields a mutable lhs by default. The only place const carries
// across is the *pointee* of a pointer/iterator — losing it would launder
// away the rhs's promise that the data behind the pointer is read-only.
// Top-level (handle) const decomposes:
//   const T   → T                 (value form — copy is mutable)
//   const T^  → (const T)^        (mutable handle, pointee const)
//   const T[] → (const T)[]       (mutable iterator, pointee const)
// Already pointee-const forms (`(const T)^`, `(const T)[]`) are unchanged.
// Used at the inferred-`VarDeclStmt` site so `x = expr;` (no explicit type)
// follows the same copy semantics as an explicit `T x = expr;`.
inline std::string copyConst(const std::string& t) {
    // Leading `const ` — top-level const on a copy decomposes: handle-const
    // drops; pointee-const survives by promoting `const T^` / `const T[]`
    // into the `(const T)^` / `(const T)[]` pointee-const form.
    if (t.rfind("const ", 0) == 0) {
        std::string body = t.substr(6);
        if (!body.empty() && body.back() == '^')
            return "(const " + body.substr(0, body.size() - 1) + ")^";
        if (body.size() >= 2 && body.substr(body.size() - 2) == "[]")
            return "(const " + body.substr(0, body.size() - 2) + ")[]";
        return body;
    }
    // Paren-wrapped const value `(const T)` — the rvalue read of a pointee-
    // const (e.g. an element from a `(const T)[]` iter, or a deref of a
    // `(const T)^`). A copy yields a mutable lhs. Anon tuples (top-level
    // commas inside the parens) are not this shape — leave those alone.
    if (t.size() >= 2 && t.front() == '(' && t.back() == ')') {
        std::string inner = t.substr(1, t.size() - 2);
        int depth = 0; bool top_comma = false;
        for (char c : inner) {
            if (c == '(') depth++;
            else if (c == ')') depth--;
            else if (c == ',' && depth == 0) { top_comma = true; break; }
        }
        if (!top_comma && inner.rfind("const ", 0) == 0)
            return inner.substr(6);
    }
    return t;
}

inline std::string applyParamConstDefault(const std::string& type, bool is_mutable) {
    if (is_mutable || type.empty()) return type;
    // Already carries const? Don't double-qualify — preserves explicit
    // `const T^` (full-const) and `(const T)^` (already reference-to-const)
    // declarations as written.
    if (type.find("const ") != std::string::npos) return type;
    std::string suffix;
    std::string base = type;
    if (base.size() >= 2 && base.substr(base.size() - 2) == "[]") {
        suffix = "[]";
        base = base.substr(0, base.size() - 2);
    } else if (!base.empty() && base.back() == '^') {
        suffix = "^";
        base.pop_back();
    } else {
        return type;
    }
    return "(const " + base + ")" + suffix;
}

// Build the canonical param-type vector from a (params, param_mutable) pair,
// applying default-const-on-unmarked-indirect-params. Used at every signature
// registration site (free fn, nested fn, method, external method, template
// instantiation) so the caller view, the body view, and the mangling layer
// all agree on the type string per slot.
inline std::vector<std::string> buildParamTypes(
    const std::vector<std::pair<std::string, std::string>>& params,
    const std::vector<bool>& param_mutable)
{
    std::vector<std::string> out;
    out.reserve(params.size());
    for (int i = 0; i < (int)params.size(); i++) {
        bool is_mut = (i < (int)param_mutable.size()) && param_mutable[i];
        out.push_back(applyParamConstDefault(params[i].first, is_mut));
    }
    return out;
}

// After unwrapping a deref/iter-index from "(const T)^" or "(const T)[]",
// the leaf string is "(const T)" — wrap parens around a non-tuple. Strip
// them so downstream code (isAnonTupleType, scalar matching) sees "const T".
// Leaves real anon-tuple types like "(int,int)" alone.
inline std::string stripRedundantConstParens(const std::string& s) {
    if (s.size() < 2 || s.front() != '(' || s.back() != ')') return s;
    int depth = 0;
    for (size_t i = 1; i + 1 < s.size(); i++) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') depth--;
        else if (s[i] == ',' && depth == 0) return s; // real tuple
    }
    std::string inner = s.substr(1, s.size() - 2);
    if (inner.rfind("const ", 0) == 0) return inner;
    return s;
}

// --- Expressions ---

struct Expr {
    int file_id = 0;
    int tok = 0;            // index into SourceMap.at(file_id).tokens
    virtual ~Expr() = default;
};

struct StringLiteralExpr : Expr {
    std::string value;
    StringLiteralExpr(std::string v) : value(std::move(v)) {}
};

struct IntLiteralExpr : Expr {
    int64_t value;
    bool is_char_literal = false;
    bool is_nondecimal = false;  // hex/binary/octal — infers uint/uint64
    bool is_bool = false;        // the `true`/`false` literal — typed `bool`
    IntLiteralExpr(int64_t v, bool is_char = false, bool is_nondec = false)
        : value(v), is_char_literal(is_char), is_nondecimal(is_nondec) {}
};

struct VarExpr : Expr {
    std::string name;
    VarExpr(std::string n) : name(std::move(n)) {}
};

struct BinaryExpr : Expr {
    std::string op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    BinaryExpr(std::string op, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r)
        : op(std::move(op)), left(std::move(l)), right(std::move(r)) {}
};

struct CallExpr : Expr {
    std::string callee;
    std::vector<std::unique_ptr<Expr>> args;
    std::vector<std::string> type_args; // non-empty for template calls: add<int>(...)
    std::string qualifier;              // "" for bare; slid name for `Name:fn()`; "::" for `::fn()`
    bool args_padded = false;           // codegen: default-value args appended once
    // For indirect calls through a function-pointer expression: `callee` is "" and
    // `callee_expr` holds the producing expression. Set by parsePostfix when
    // `<expr>(args)` chains on a non-name expression (e.g. `compare^(a, b)`).
    std::unique_ptr<Expr> callee_expr;
    CallExpr(std::string callee, std::vector<std::unique_ptr<Expr>> args)
        : callee(std::move(callee)), args(std::move(args)) {}
};

struct UnaryExpr : Expr {
    std::string op;
    std::unique_ptr<Expr> operand;
    UnaryExpr(std::string op, std::unique_ptr<Expr> operand)
        : op(std::move(op)), operand(std::move(operand)) {}
};

// field access: obj.field_
struct FieldAccessExpr : Expr {
    std::unique_ptr<Expr> object;
    std::string field;
    FieldAccessExpr(std::unique_ptr<Expr> obj, std::string field)
        : object(std::move(obj)), field(std::move(field)) {}
};

// method call: obj.method(args)
struct MethodCallExpr : Expr {
    std::unique_ptr<Expr> object;
    std::string method;
    std::vector<std::unique_ptr<Expr>> args;
    bool args_padded = false;           // codegen: default-value args appended once
    MethodCallExpr(std::unique_ptr<Expr> obj, std::string method,
                   std::vector<std::unique_ptr<Expr>> args)
        : object(std::move(obj)), method(std::move(method)), args(std::move(args)) {}
};

// array index: arr[i] or arr[i][j]
struct ArrayIndexExpr : Expr {
    std::unique_ptr<Expr> base;
    std::unique_ptr<Expr> index;
    ArrayIndexExpr(std::unique_ptr<Expr> base, std::unique_ptr<Expr> idx)
        : base(std::move(base)), index(std::move(idx)) {}
};

// slice: base[start..end]
struct SliceExpr : Expr {
    std::unique_ptr<Expr> base;
    std::unique_ptr<Expr> start;
    std::unique_ptr<Expr> end;
    SliceExpr(std::unique_ptr<Expr> base, std::unique_ptr<Expr> start, std::unique_ptr<Expr> end)
        : base(std::move(base)), start(std::move(start)), end(std::move(end)) {}
};

// dereference: ptr^  (postfix)
struct DerefExpr : Expr {
    std::unique_ptr<Expr> operand;
    DerefExpr(std::unique_ptr<Expr> op) : operand(std::move(op)) {}
};

// take address: ^x  (prefix)
struct AddrOfExpr : Expr {
    std::unique_ptr<Expr> operand;
    AddrOfExpr(std::unique_ptr<Expr> op) : operand(std::move(op)) {}
};

// nullptr literal — null pointer constant
struct NullptrExpr : Expr {};

// float literal — always double precision internally
struct FloatLiteralExpr : Expr {
    double value;
    FloatLiteralExpr(double v) : value(v) {}
};

// type conversion expression: (type=expr) — converts value; int↔int, int↔float, float↔float
struct TypeConvExpr : Expr {
    std::string target_type;
    std::unique_ptr<Expr> operand;
    TypeConvExpr(std::string t, std::unique_ptr<Expr> op)
        : target_type(std::move(t)), operand(std::move(op)) {}
};

// pointer reinterpret cast: <Type^> expr or <Type[]> expr or <intptr> expr
struct PtrCastExpr : Expr {
    std::string target_type;
    std::unique_ptr<Expr> operand;
    PtrCastExpr(std::string t, std::unique_ptr<Expr> op)
        : target_type(std::move(t)), operand(std::move(op)) {}
};

// qualifier-only cast: <const> expr  or  <mutable> expr — applies to any value.
// codegen lowers to the operand value verbatim; inferSlidType applies the
// qualifier to the operand's reported type (no semantic enforcement).
struct QualifierCastExpr : Expr {
    std::string qualifier;             // "const" or "mutable"
    std::unique_ptr<Expr> operand;
    QualifierCastExpr(std::string q, std::unique_ptr<Expr> op)
        : qualifier(std::move(q)), operand(std::move(op)) {}
};

// tuple literal: (expr, expr, ...) — used in return statements
struct TupleExpr : Expr {
    std::vector<std::unique_ptr<Expr>> values;
};

// new T[n] — heap array allocation, returns ptr
struct NewExpr : Expr {
    std::string elem_type;
    std::unique_ptr<Expr> count;
    NewExpr(std::string t, std::unique_ptr<Expr> n)
        : elem_type(std::move(t)), count(std::move(n)) {}
};

// new T(args) — heap scalar allocation, returns ptr
struct NewScalarExpr : Expr {
    std::string elem_type;
    std::vector<std::unique_ptr<Expr>> args;
    NewScalarExpr(std::string t, std::vector<std::unique_ptr<Expr>> a)
        : elem_type(std::move(t)), args(std::move(a)) {}
};

// new(addr) T(args) — placement new: construct T at given address, returns T^
struct PlacementNewExpr : Expr {
    std::unique_ptr<Expr> addr;
    std::string elem_type;
    std::vector<std::unique_ptr<Expr>> args;
    PlacementNewExpr(std::unique_ptr<Expr> a, std::string t, std::vector<std::unique_ptr<Expr>> args)
        : addr(std::move(a)), elem_type(std::move(t)), args(std::move(args)) {}
};

// sizeof(TypeName) or sizeof(expr) — returns intptr
struct SizeofExpr : Expr {
    std::unique_ptr<Expr> operand;
};

// stringification: ##name(x), ##type(x), ##line, ##file, ##func, ##date, ##time
struct StringifyExpr : Expr {
    std::string kind;                // "name"|"type"|"line"|"file"|"func"|"date"|"time"
    std::unique_ptr<Expr> operand;   // non-null for type; null for name and others
    std::string text;                // for kind "name": the literal source text
    StringifyExpr(std::string k, std::unique_ptr<Expr> op = nullptr)
        : kind(std::move(k)), operand(std::move(op)) {}
};

// --- Statements ---

struct Stmt {
    int file_id = 0;
    int tok = 0;            // index into SourceMap.at(file_id).tokens
    virtual ~Stmt() = default;
};

struct VarDeclStmt : Stmt {
    std::string type;
    std::string name;
    std::unique_ptr<Expr> init;           // null for default construction
    std::vector<std::unique_ptr<Expr>> ctor_args; // for Counter c(5)
    bool is_move = false;                 // true for Type name <- expr
    bool is_loop_var = false;             // synthesized loop var in a for-iterator desugar
    VarDeclStmt(std::string type, std::string name,
                std::unique_ptr<Expr> init,
                std::vector<std::unique_ptr<Expr>> ctor_args = {},
                bool is_move = false)
        : type(std::move(type)), name(std::move(name)),
          init(std::move(init)), ctor_args(std::move(ctor_args)), is_move(is_move) {}
};

struct AssignStmt : Stmt {
    std::string name;
    std::unique_ptr<Expr> value;
    bool is_move = false;                 // true for name <- expr
    AssignStmt(std::string name, std::unique_ptr<Expr> value, bool is_move = false)
        : name(std::move(name)), value(std::move(value)), is_move(is_move) {}
};

// field assignment: obj.field_ = expr
struct FieldAssignStmt : Stmt {
    std::unique_ptr<Expr> object;
    std::string field;
    std::unique_ptr<Expr> value;
    bool is_move = false;                 // true for obj.field_ <- expr
    FieldAssignStmt(std::unique_ptr<Expr> obj, std::string field,
                    std::unique_ptr<Expr> value, bool is_move = false)
        : object(std::move(obj)), field(std::move(field)), value(std::move(value)),
          is_move(is_move) {}
};

struct ReturnStmt : Stmt {
    std::unique_ptr<Expr> value; // may be null for void return
    ReturnStmt(std::unique_ptr<Expr> v) : value(std::move(v)) {}
};

struct CallStmt : Stmt {
    std::string callee;
    std::vector<std::unique_ptr<Expr>> args;
    std::vector<std::string> type_args; // non-empty for template calls: add<int>(...)
    bool args_padded = false;           // codegen: default-value args appended once
    CallStmt(std::string callee, std::vector<std::unique_ptr<Expr>> args)
        : callee(std::move(callee)), args(std::move(args)) {}
};

// expression used as a statement for its side effects: ++ptr; x++; etc.
struct ExprStmt : Stmt {
    std::unique_ptr<Expr> expr;
    ExprStmt(std::unique_ptr<Expr> e) : expr(std::move(e)) {}
};

// method call as statement: obj.method(args);
struct MethodCallStmt : Stmt {
    std::unique_ptr<Expr> object;
    std::string method;
    std::vector<std::unique_ptr<Expr>> args;
    bool args_padded = false;           // codegen: default-value args appended once
    MethodCallStmt(std::unique_ptr<Expr> obj, std::string method,
                   std::vector<std::unique_ptr<Expr>> args)
        : object(std::move(obj)), method(std::move(method)), args(std::move(args)) {}
};

// fixed-size array declaration: Type name[d0][d1] = ((...), ...)
struct ArrayDeclStmt : Stmt {
    std::string elem_type;
    std::string name;
    std::vector<int> dims;  // e.g. {8, 8} for [8][8]
    // initializer: flat list of exprs in row-major order
    std::vector<std::unique_ptr<Expr>> init_values;
};

// tuple destructure: (type name, type name, ...) = expr;
struct TupleDestructureStmt : Stmt {
    std::vector<std::pair<std::string, std::string>> fields; // (type, name) pairs
    std::unique_ptr<Expr> init;
};

// index assign: base[index] = value  (pointer-type arrays and fields)
struct IndexAssignStmt : Stmt {
    std::unique_ptr<Expr> base;
    std::unique_ptr<Expr> index;
    std::unique_ptr<Expr> value;
    bool is_move = false;                 // true for base[index] <- value
    IndexAssignStmt(std::unique_ptr<Expr> b, std::unique_ptr<Expr> i, std::unique_ptr<Expr> v,
                    bool is_move = false)
        : base(std::move(b)), index(std::move(i)), value(std::move(v)), is_move(is_move) {}
};

// delete ptr — free heap allocation
struct DeleteStmt : Stmt {
    std::unique_ptr<Expr> operand;
    DeleteStmt(std::unique_ptr<Expr> op) : operand(std::move(op)) {}
};

// swap: lhs <-> rhs  — exchange values at two lvalue locations
struct SwapStmt : Stmt {
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    SwapStmt(std::unique_ptr<Expr> l, std::unique_ptr<Expr> r)
        : lhs(std::move(l)), rhs(std::move(r)) {}
};

// deref assign: ptr^ = expr  or  ptr^.field = expr handled via FieldAssignStmt on DerefExpr
struct DerefAssignStmt : Stmt {
    std::unique_ptr<Expr> ptr;   // the pointer expression
    std::unique_ptr<Expr> value;
    bool is_move = false;
    DerefAssignStmt(std::unique_ptr<Expr> ptr, std::unique_ptr<Expr> val, bool m = false)
        : ptr(std::move(ptr)), value(std::move(val)), is_move(m) {}
};

// lvalue op= rhs — compound assign with single LHS evaluation.
// LHS shape preserved so codegen can emit address-once / load / op / store
// (or dispatch slid op<op>= directly when the LHS is a slid type).
struct CompoundAssignStmt : Stmt {
    std::unique_ptr<Expr> lhs;
    std::string op;              // "+", "-", "*", "/", "%", "&", "|", "^",
                                 // "<<", ">>", "&&", "||", "^^"
    std::unique_ptr<Expr> rhs;
    CompoundAssignStmt(std::unique_ptr<Expr> l, std::string o, std::unique_ptr<Expr> r)
        : lhs(std::move(l)), op(std::move(o)), rhs(std::move(r)) {}
};

struct BlockStmt : Stmt {
    std::vector<std::unique_ptr<Stmt>> stmts;
};

struct IfStmt : Stmt {
    std::unique_ptr<Expr> cond;
    std::unique_ptr<BlockStmt> then_block;
    std::unique_ptr<BlockStmt> else_block; // may be null
    std::string block_label; // optional :name after }
};

struct WhileStmt : Stmt {
    std::unique_ptr<Expr> cond;
    std::unique_ptr<BlockStmt> body;
    std::string block_label;    // optional :name after }
    bool bottom_condition = false; // true for while { } (cond); form
};

// long for: for (init) (cond) { update } { body } :label;
// init_stmts: per-slot VarDeclStmt or AssignStmt (empty slots produce no entry)
// cond: nullptr means empty () => true
struct ForLongStmt : Stmt {
    std::vector<std::unique_ptr<Stmt>> init_stmts;
    std::unique_ptr<Expr> cond;
    std::unique_ptr<BlockStmt> update_block;
    std::unique_ptr<BlockStmt> body;
    std::string block_label;
};

// `global;` — lifetime statement that opens the global-instantiation scope.
// Marker node with no payload; codegen materializes the synthetic `global` slid
// whose dtor calls `__$global_dtor_all`. Must appear inside main's body; the
// parser auto-inserts one at the top of main when absent.
struct GlobalLifetimeStmt : Stmt {};

struct BreakStmt : Stmt {
    std::string label;  // empty = naked break
    int number = 0;     // 0 = not numbered
};
struct ContinueStmt : Stmt {
    std::string label;  // empty = naked continue
    int number = 0;     // 0 = not numbered
};

struct SwitchCase {
    std::unique_ptr<Expr> value;        // null = default
    std::vector<std::unique_ptr<Stmt>> stmts;
};

struct SwitchStmt : Stmt {
    std::unique_ptr<Expr> expr;
    std::vector<SwitchCase> cases;
    std::string block_label;            // optional :label after }
};

// --- Constants ---

// Build the slids-level name of a namespaced declaration: `ns:name` when it
// lives inside a namespace block, the bare `name` otherwise. Free functions
// and consts are keyed by this throughout codegen.
inline std::string qualifiedName(const std::string& ns, const std::string& name) {
    return ns.empty() ? name : ns + ":" + name;
}

// const [type] name = expr;
//   declared_type empty when inferred from rhs.
//   substitution-only: never emits storage or a link-time symbol.
struct ConstDef {
    std::string name;
    std::string declared_type;   // empty when inferred
    std::unique_ptr<Expr> rhs;
    std::string namespace_name;  // non-empty when declared inside a namespace block
    int file_id = 0;
    int tok = 0;                 // token index of the name (for diagnostics)
};

// const decl as a statement (block / function-body scope)
struct ConstDeclStmt : Stmt {
    ConstDef def;
};

// --- Enums ---

struct EnumDef {
    std::string name;
    std::vector<std::string> values; // ordered list of enumerator names
    int file_id = 0;
    int tok = 0;                     // token index of the enum name
};

// --- Top-level ---

struct FieldDef {
    std::string type;
    std::string name;
    std::unique_ptr<Expr> default_val; // may be null
    int file_id = 0;                   // for diagnostics that point at the field declaration
    int tok = 0;                       // token index of the field name (for diagnostics)
};

struct MethodDef {
    std::string return_type;
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;
    std::vector<bool> param_mutable;  // parallel to params; true if 'mutable' on that param
    std::vector<int> param_mut_toks;  // parallel to params; tok of the 'mutable' keyword for diagnostic notes
    std::vector<int> param_toks;      // parallel to params; tok of the parameter name (for diagnostics)
    std::vector<std::unique_ptr<Expr>> param_defaults; // parallel to params; null when no default
    std::unique_ptr<BlockStmt> body;
    int file_id = 0;
    int tok = 0;               // token index of the method name (for diagnostics)
    bool is_virtual = false;   // `virtual` keyword present on the declaration
    bool is_delete = false;    // `= delete;` — pure virtual when no ancestor match;
                               //               removes inherited method when a same-sig ancestor exists
    bool is_default = false;   // `= default;` — derived inherits base impl with no-shadow contract
    bool is_const_method = false; // `T const Class:method()` — marker only this scope, no enforcement
    bool has_explicit_return = false; // true when the return type came from a parsed type, not an elision
};

// File-scope plain type alias (`alias Name = TypeExpr;`). The rhs is parsed
// once at definition into the canonical type-string `body`; consumers see
// the alias name resolve to `body` via `lookupAlias`. Propagates across .slh
// imports so `import dump;` makes dump.slh's aliases visible. Also used by
// SlidDef.aliases for class-scope aliases.
struct TypeAliasDef {
    std::string name;
    std::string body;
    int file_id = 0;
    int tok = 0;
};

struct SlidDef {
    std::string name;
    int name_file_id = 0;                 // location of the class name token (for diagnostics)
    int name_tok = 0;
    int explicit_ctor_file_id = 0;        // location of the first-defined ctor (for "first defined here" notes)
    int explicit_ctor_tok = 0;
    int explicit_dtor_file_id = 0;        // location of the first-defined dtor
    int explicit_dtor_tok = 0;
    std::string base_name;                // non-empty when defined as `Base : Derived(...) { ... }`
    std::vector<std::string> type_params; // non-empty for template slids: Vector<T>
    std::vector<FieldDef> fields;
    // pure-syntactic positional flags. set by parseSlidDef; AND'd / OR'd by
    // mergeReopens. trailing=true means "more reopens may still follow"
    // (class is open). lone `(...)` is disambiguated via seen_classes_:
    // first occurrence ⇒ trailing-only (open); subsequent ⇒ leading-only (close).
    bool has_leading_ellipsis = false;
    bool has_trailing_ellipsis = false;
    bool has_explicit_ctor_decl = false; // _() was declared (with or without body)
    bool has_explicit_dtor_decl = false; // ~() was declared (with or without body)
    bool is_const_ctor = false;          // `const _()` — marker only, no enforcement yet
    bool is_const_dtor = false;          // `const ~()` — marker only, no enforcement yet
    bool dtor_is_virtual = false;        // `virtual ~()` — required when class is virtual
    bool is_transport_impl = false;      // this slid emits __$pinit for the consumer
    int public_field_count = 0;          // number of public fields before private ones (for __$pinit)
    bool is_local = true;                // false when template body loaded from an imported .sl file
    std::string impl_module;             // module name of the impl file (when !is_local)
    std::unique_ptr<BlockStmt> ctor_body;          // loose code (implicit ctor), or null
    std::unique_ptr<BlockStmt> explicit_ctor_body; // _() { ... }, or null
    std::unique_ptr<BlockStmt> dtor_body;          // ~() { ... }, or null
    std::vector<MethodDef> methods;
    std::vector<SlidDef> nested_slids;             // slid defs declared inside this slid's body
    std::vector<EnumDef> nested_enums;             // enum defs declared inside this slid's body (class-scoped)
    std::vector<ConstDef> consts;                  // class-scope const decls (no storage)
    std::vector<TypeAliasDef> aliases;              // class-scope `alias Name = TypeExpr;` decls — propagate cross-TU via .slh
    // Classes declared inside this (template) class's method bodies. Carried
    // with the template and re-instantiated per type-arg — see codegen_template.
    std::vector<SlidDef> local_classes;
};

// nested function defined inside a parent function body
struct NestedFunctionDef {
    std::string return_type; // empty when tuple_return_fields is non-empty
    std::vector<std::pair<std::string, std::string>> tuple_return_fields;
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;
    std::vector<bool> param_mutable;  // parallel to params
    std::vector<int> param_mut_toks;  // parallel to params; tok of the 'mutable' keyword for diagnostic notes
    std::vector<int> param_toks;      // parallel to params; tok of the parameter name (for diagnostics)
    std::vector<std::unique_ptr<Expr>> param_defaults; // parallel to params; null when no default
    std::unique_ptr<BlockStmt> body;
};

// nested function definition appearing inside a block
struct NestedFunctionDefStmt : Stmt {
    NestedFunctionDef def;
};

struct FunctionDef {
    std::string return_type; // empty when tuple_return_fields is non-empty
    std::vector<std::pair<std::string, std::string>> tuple_return_fields;
    std::string name;        // emit-time symbol name (mangled for template instantiations)
    std::string user_name;   // unmangled, source-level name (used for ##func, diagnostics)
    std::vector<std::pair<std::string, std::string>> params;
    std::vector<bool> param_mutable;  // parallel to params
    std::vector<int> param_mut_toks;  // parallel to params; tok of the 'mutable' keyword for diagnostic notes
    std::vector<int> param_toks;      // parallel to params; tok of the parameter name (for diagnostics)
    std::vector<std::unique_ptr<Expr>> param_defaults; // parallel to params; null when no default
    std::vector<bool> param_auto_promoted;  // parallel to params; true when class-T value→ref auto-promoted at template instantiation
    std::unique_ptr<BlockStmt> body;
    int file_id = 0;
    int tok = 0;                 // token index of the function name (for diagnostics)
    std::vector<std::string> type_params; // non-empty for template functions: T add<T>(T a, T b)
    bool is_local = true;        // false when body loaded from a separate impl file
    std::string impl_module;     // module name of the impl file (when !is_local)
    bool is_foreign = false;     // `= import;` — a C function: bare symbol, no slids body
    std::string namespace_name;  // non-empty when declared inside a namespace block
    // Classes declared inside this (template) function's body. Carried with
    // the template and re-instantiated per type-arg — see codegen_template.
    std::vector<SlidDef> local_classes;
};

// method defined outside the class body: void String:clear() { ... }
// method_name is "_" for ctor, "~" for dtor, else the method name
struct ExternalMethodDef {
    std::string slid_name;
    std::string return_type;
    std::string method_name;
    std::vector<std::pair<std::string, std::string>> params;
    std::vector<bool> param_mutable;  // parallel to params
    std::vector<int> param_mut_toks;  // parallel to params; tok of the 'mutable' keyword for diagnostic notes
    std::vector<int> param_toks;      // parallel to params; tok of the parameter name (for diagnostics)
    std::vector<std::unique_ptr<Expr>> param_defaults; // parallel to params; null when no default
    std::unique_ptr<BlockStmt> body;
    int file_id = 0;
    int tok = 0;                       // token index of method_name (for diagnostics)
    bool is_virtual = false;
    bool is_delete = false;
    bool is_default = false;
    bool is_const_method = false; // `T const Class:method()` — marker only this scope, no enforcement
    bool has_explicit_return = false; // true when the return type came from a parsed type, not an elision
};

// global slid declaration: `global [Name] (field_list) { _() {} ~() {} }`.
// Each declaration produces an independent unit. Co-named declarations stack
// in the same `namespace_name` but never merge — they share only access lookup.
// `is_lazy()` ⇔ ctor_body || dtor_body (pair rule enforced by parser).
// Static-allocated globals (no ctor/dtor) emit module-level LLVM globals;
// lazy globals get a per-slid `i1` sentinel and `__$ensure_<id>()` glue.
struct GlobalDef {
    std::string namespace_name;            // "" for unnamed namespace; "simple", "Box:lid", "foo" otherwise
    std::vector<FieldDef> fields;
    std::unique_ptr<BlockStmt> ctor_body;  // null when no body in this TU (may still be declared)
    std::unique_ptr<BlockStmt> dtor_body;  // null when no body in this TU (may still be declared)
    // True when `_()` / `~()` were declared in this TU's body — with or
    // without a body. Forward declarations (`_();`) set these but leave the
    // body pointers null; the defining TU provides the body. The pair rule
    // is enforced on these flags so a forward-only declaration still has to
    // declare both halves.
    bool has_ctor_decl = false;
    bool has_dtor_decl = false;
    int file_id = 0;
    int tok = 0;                           // location of the `global` keyword
    // when non-empty, this global is only visible inside the named function
    // (function/method-internal short form). Bare-name access from that
    // function's body falls through to this global after locals fail.
    std::string visible_in_function;
    // Non-empty when this entry came from an imported `.slh`. The TU declares
    // the symbol; storage and bodies live in <impl_module>. Codegen emits
    // `external global` for the field, skips lazy helpers, and the sidecar
    // pass leaves the entry out of the program-wide globals registry.
    std::string impl_module;
    bool is_lazy() const { return has_ctor_decl || has_dtor_decl; }
};

// A namespace declaration — `Name { ... }` (no `()`). A namespace is not a
// class: no fields, no `self`. Its members (functions, consts) are carried in
// program.functions / program.consts tagged with `namespace_name`; this record
// just notes that the namespace was declared. Reopening adds another record.
struct NamespaceDef {
    std::string name;
    std::vector<FunctionDef> functions;  // the namespace's own functions
    std::vector<ConstDef> consts;        // the namespace's own consts
    int file_id = 0;
    int tok = 0;
};

// A function alias — `alias <name> = <target>;`. Additive: `name` gains
// `target`'s overload(s) without removing `target` itself. Resolved in codegen
// after function signatures are collected (the rhs may be forward-declared).
// `namespace_name` is the enclosing namespace ("" at file scope); both `name`
// and `target` are looked up qualified by it.
struct AliasDef {
    std::string name;            // lhs — the alias
    std::string target;          // rhs — the aliased function
    std::string namespace_name;  // enclosing namespace, empty at file scope
    int file_id = 0;
    int tok = 0;                 // token index of the alias name (diagnostics)
};

// (TypeAliasDef moved earlier in the file so SlidDef can carry class-scope
// aliases — see definition just before SlidDef.)

// File-scope template type alias (`alias Name<T,U,...> = TypeExpr;`). The
// body keeps each type-param identifier literal (`T^`, `(char[], T^)`, etc.);
// substitution at the use site replaces `type_params[i]` with `args[i]`.
struct TypeAliasTemplate {
    std::string name;
    std::vector<std::string> type_params;
    std::string body;
    int file_id = 0;
    int tok = 0;
};

// Substitute identifier-shaped names in a slids type-string with their
// replacements. Walks the string token-by-token: each maximal `[A-Za-z_]
// [A-Za-z0-9_]*` run is looked up in `subst` and replaced if matched;
// punctuation (`^`, `[`, `]`, `(`, `)`, `,`, `.`) flows through. Used by the
// template-alias substitutor at the use site.
inline std::string substituteTypeIdents(
    const std::string& body,
    const std::map<std::string, std::string>& subst)
{
    auto is_id_start = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    auto is_id_cont = [&](char c) {
        return is_id_start(c) || (c >= '0' && c <= '9');
    };
    std::string out;
    out.reserve(body.size());
    size_t i = 0;
    while (i < body.size()) {
        if (is_id_start(body[i])) {
            size_t j = i;
            while (j < body.size() && is_id_cont(body[j])) j++;
            std::string id = body.substr(i, j - i);
            auto it = subst.find(id);
            out += (it != subst.end()) ? it->second : id;
            i = j;
        } else {
            out.push_back(body[i]);
            i++;
        }
    }
    return out;
}

// A bare file-scope `Name;` — an unnamed global instance. It has no name to
// trigger lazy construction, so it is constructed eagerly + unconditionally at
// main's `global;` statement and destructed at the close of that block.
struct UnnamedGlobal {
    std::string type_name;
    int file_id = 0;
    int tok = 0;
};

struct Program {
    std::vector<EnumDef> enums;
    std::vector<SlidDef> classes;
    std::vector<FunctionDef> functions;
    std::vector<ExternalMethodDef> external_methods;
    std::vector<ConstDef> consts;                   // program-scope const decls (no storage)
    std::vector<GlobalDef> globals;                 // global slid declarations (file/class/function-internal)
    std::vector<NamespaceDef> namespaces;           // declared namespace blocks
    std::vector<AliasDef> aliases;                   // function aliases (alias x = y;)
    std::vector<TypeAliasDef> type_aliases;          // file-scope plain type aliases — propagate through .slh import
    std::vector<TypeAliasTemplate> type_alias_templates; // file-scope template type aliases — propagate through .slh import
    std::vector<UnnamedGlobal> unnamed_globals;       // bare file-scope `Name;` instances

    std::vector<std::string> imported_headers; // resolved .slh paths, for -MF dep output
    std::map<std::string, std::string> slid_modules; // slid name -> module that provides it
    struct InstantiateRequest {
        std::string func_name;
        std::vector<std::string> type_args;
        std::vector<std::string> param_types; // post-substitution param types — selects overload
    };
    std::vector<InstantiateRequest> instantiations; // explicit instantiate statements
};

// --- Parser ---

class SourceMap;

class Parser {
public:
    Parser(SourceMap& sm,
           int file_id,
           std::vector<Token> tokens,
           std::string source_dir = "",
           std::vector<std::string> import_paths = {},
           std::shared_ptr<std::set<std::string>> imported_once = nullptr,
           bool is_header = false);
    Program parse();

    // Seed this parser's file-scope alias frames with type aliases parsed
    // elsewhere. Used by the .slh import arm to hand its `hdr`'s aliases to
    // the impl_parser before the impl-parse runs — impl_parser's own
    // `import dump;` is suppressed by impl_cache (to avoid recursion), so
    // without seeding, the impl's function defs that reference the alias
    // would mis-resolve. Entries already present in the front frame are
    // left untouched.
    void seedAliasesFrom(const std::vector<TypeAliasDef>& tas,
                         const std::vector<TypeAliasTemplate>& tats);

private:
    SourceMap& sm_;
    int file_id_;
    std::vector<Token> tokens_;
    int pos_;
    std::string source_dir_;
    std::vector<std::string> import_paths_; // --import-path dirs (searched for .slh headers)
    std::shared_ptr<std::set<std::string>> imported_once_; // shared across nested parsers; import-once guard
    // True when this parse is for a `.slh` header file. Used to reject
    // constructs that have no coherent semantics in a header — currently
    // file-scope unnamed-global declarations (`Vex1;`), which name nothing
    // and so can't be shared between TUs.
    bool is_header_ = false;

    Token& peek();
    Token& advance();
    Token& expect(TokenType type, const std::string& msg);
    [[noreturn]] void errorHere(const std::string& msg);
    // After a parseExpr inside a paren-list loop, expect either kComma
    // (consume) or kRParen (don't consume; loop ends). Anything else is a
    // missing-comma error — surfaces a focused diagnostic at the wrong
    // token instead of silently re-parsing it as the next argument.
    void expectArgSeparator();
    [[noreturn]] void errorAt(int t, const std::string& msg);
    int currentLine();    // line of the current token (used by ##line and friends)

    template<typename T, typename... Args>
    std::unique_ptr<T> make(int t, Args&&... args) {
        auto p = std::make_unique<T>(std::forward<Args>(args)...);
        p->file_id = file_id_;
        p->tok = t;
        return p;
    }
    bool isTypeName(const Token& t) const;
    bool isUserTypeName(const Token& t) const;
    std::string parseTypeName();
    // Parse a parameter list body (between '(' and ')', not consuming either):
    // `[mutable] type name [= constexpr]`, comma-separated. Fills the four
    // parallel vectors. Enforces trailing-only defaults. Shared by every
    // function/method/nested-function/external-method head parser.
    void parseParamList(std::vector<std::pair<std::string, std::string>>& params,
                        std::vector<int>& param_toks,
                        std::vector<bool>& param_mutable,
                        std::vector<int>& param_mut_toks,
                        std::vector<std::unique_ptr<Expr>>& param_defaults);

    // LocalInfo migrated to LocalVarEntry (see phase-2 scaffolding below).
    // Visibility (entry present in frame_entries_) drives decl-vs-assign
    // disambiguation; the LocalVarEntry payload feeds shape-aware downstream
    // logic (short-form for, etc).
    void declareVar(const std::string& name, int name_tok);
    bool isInScope(const std::string& name) const;
    int arrayCountInScope(const std::string& name) const; // 0 if not a fixed-size array local
    int arrayRankInScope(const std::string& name) const;  // 0 if not a fixed-size array local
    int tupleSizeInScope(const std::string& name) const;  // 0 if not a tuple local
    std::string typeInScope(const std::string& name) const; // "" if not recorded
    struct LocalVarEntry;
    LocalVarEntry* findLocal(const std::string& name);    // innermost frame; nullptr if not in scope

    // Per-class info populated as top-level slid defs are parsed (recordSlidMethods).
    // method_names mirrors sigs.keys for fast presence checks; sigs carries the
    // full per-method return type and parameter type list, used by the for-iter
    // protocol classifier (single-overload assumption per spec).
    struct MethodSig {
        std::string return_type;
        std::vector<std::string> param_types;
    };
    struct ClassInfo {
        std::set<std::string> method_names;
        std::map<std::string, MethodSig> sigs;
        std::vector<std::string> type_params; // non-empty for template slids
    };
    std::map<std::string, ClassInfo> class_info_;
    void recordSlidMethods(const SlidDef& s);

    // For-iterator protocol classifier. Each protocol (op[]/size for by-value,
    // begin/end/next for by-reference) is one of three states; the for-iter arm
    // dispatches Good and reports Bad with a focused reason.
    enum class ProtocolStatus { Absent, Good, Bad };
    struct ProtocolDiag {
        ProtocolStatus status = ProtocolStatus::Absent;
        std::string reason;        // populated when status == Bad
        std::string return_type;   // populated when status == Good (for tie-break)
    };
    ProtocolDiag classifyByValue(const ClassInfo& ci, const std::string& class_name) const;
    ProtocolDiag classifyByRef(const ClassInfo& ci, const std::string& class_name) const;

    // field names of the slid currently being parsed → declaration site (file_id, tok).
    // Membership prevents field assignments being inferred as declarations; the location
    // feeds "field declared here" notes on shadowing diagnostics.
    struct FieldRef { int file_id = 0; int tok = 0; };
    std::map<std::string, FieldRef> current_slid_fields_;
    // all parsed slid field names, keyed by slid name (used for external method blocks)
    std::map<std::string, std::map<std::string, FieldRef>> all_slid_fields_;
    // Enums migrated to EnumEntry; see lookupEnum / appendEnumEntry below.
    // monotonic counter for parser-synthesized loop locals (__$end_<n>,
    // __$step_<n>, __$idx_<n>, __$tup_<n>). Names must not collide between
    // nested loops in the same scope.
    int synthetic_counter_ = 0;
    // class names seen so far in this TU — disambiguates lone `(...)` between
    // first-occurrence (open) and subsequent (closing). populated by tuple-form
    // decls and bare-block reopens. Value is the first declaration's location,
    // used for the class/namespace name-collision note.
    std::map<std::string, FieldRef> seen_classes_;
    // namespace names seen so far in this TU. Value is the first declaration's
    // location. A name cannot be both a class and a namespace.
    std::map<std::string, FieldRef> seen_namespaces_;
    // class names that have been closed in this TU — further tuple-form
    // reopens are an error; bare-block reopens are still allowed. Value is
    // the location of the closing `}` for "completed here" notes.
    std::map<std::string, FieldRef> closed_classes_;
    // short-name → canonical-name aliases for nested slids in the current outer's body
    // (e.g. "Inner" → "Outer.Inner") — applied by parseTypeName
    std::map<std::string, std::string> nested_alias_;

    // chain of class names currently being parsed — outermost first. Pushed at
    // parseSlidDef entry, popped at exit. Drives the shadow check that rejects
    // a hoisted class whose name equals any transitive enclosing class.
    struct EnclosingClass { std::string name; int file_id = 0; int tok = 0; };
    std::vector<EnclosingClass> enclosing_class_names_;

    // Name of the function/method whose body is currently being parsed.
    // Empty at file scope, set by parseFunctionDef / parseMethodDef /
    // parseExternalMethodDef before parsing the body, restored after.
    // Drives the namespace_prefix used for function-internal `global` decls.
    std::string current_function_name_;

    // Globals collected during the current top-level declaration (file-scope
    // long form, class-internal, function-internal). Drained into
    // `program.globals` at the end of each loop iteration in `parse()`.
    std::vector<GlobalDef> pending_globals_;

    // External methods declared inline inside a class body — shape
    // `RetType Class:method(...) { body }`. Drained into
    // `program.external_methods` alongside pending_globals_.
    std::vector<ExternalMethodDef> pending_external_methods_;

    // Local classes (slids defined inside a code block). Collected during the
    // current top-level declaration, drained into `program.classes` alongside
    // pending_globals_. Each is renamed to a unique internal canonical name
    // `<funcpath>.<n>.<ClassName>` before collection — see local_slid_counter_.
    std::vector<SlidDef> pending_slids_;

    // Monotonic counter giving each local class declaration a unique id. The
    // id becomes a numeric dot-component of the canonical name, so the name
    // can never be spelled by the author (parseTypeName only accepts
    // colon-separated identifiers, never a bare integer).
    int local_slid_counter_ = 0;

    // True while parsing inside a template function/class body. Local classes
    // declared here can't be concrete slids yet (a type param is unbound) —
    // they are collected into pending_local_classes_ and carried with the
    // enclosing template for per-instantiation materialization.
    bool in_template_ = false;
    std::vector<SlidDef> pending_local_classes_;

    // Per-block short-name → canonical-name map for local classes.
    // parseTypeName resolves a bare type name's base component through it
    // (without finalizing — colon suffixes still apply).
    std::string lookupLocalClass(const std::string& name) const;

    // Per-block short-name → canonical-name map for nested functions. A
    // nested function is visible only within its declaring block (and its
    // sub-scopes via the rbegin walk in lookupNestedFunc); on block close
    // the frame pops and the name becomes unresolvable, so the codegen
    // `Unknown function` error fires. Canonical name format:
    // `<funcpath>.<n>.<short>`, sharing the local_slid_counter_ namespace
    // so a fn and a class can never collide.
    std::string lookupNestedFunc(const std::string& name) const;

    // user-declared type aliases (alias Name = TypeExpr;). innermost frame is
    // current block; bottom frame is file-scope. resolved type strings are
    // already in canonical form (e.g. "int^", "Class.Hoisted", "Template__int")
    // and substituted by parseTypeName when a bare ident matches.
    struct AliasInfo { std::string resolved; int tok = 0; };
    void declareAlias(const std::string& name, const std::string& resolved, int name_tok);
    std::string lookupAlias(const std::string& name) const;

    // Per-class alias registry for class-path-qualified type-name resolution
    // (e.g. `GsA:intA`, `Outer:Inner:intX`). Populated alongside the AliasEntry
    // master-list write when an alias declaration occurs inside a class body.
    // Keyed by canonical class name (dot form: "Outer.Inner") → alias-name →
    // resolved type.
    std::map<std::string, std::map<std::string, AliasInfo>> class_aliases_;
    // Stack of enclosing-class nested_alias_ snapshots — pushed on parseSlidDef
    // entry (just before the clear) and popped at exit. Lets parseTypeName
    // resolve a sibling-class short name from inside a nested class's method
    // body, where the immediate nested_alias_ frame has been cleared.
    std::vector<std::map<std::string, std::string>> outer_nested_aliases_;
    // Canonical-class → canonical-base mapping migrated to
    // ClassEntry.base_class_name (see lookupClassBase below).
    std::string lookupClassBase(const std::string& class_name) const;
    // Resolve a path-qualified alias reference `<class-path>.<member>` (dot
    // form, post-canonicalization). Walks the class's own aliases first, then
    // its base chain via class_base_name_. Applies nested_alias_ to the
    // leading short class-name if present. Returns the resolved type string
    // on hit, empty on miss.
    std::string lookupClassAlias(const std::string& class_path,
                                 const std::string& member) const;

    // Template type aliases (`alias Name<T,...> = TypeExpr;`). Stored as
    // AliasEntry with non-empty type_params. File-scope and nested-block
    // entries live in master_list_ / frame_entries_ alongside plain aliases.
    struct AliasEntry;
    void declareAliasTemplate(const std::string& name,
                              std::vector<std::string> type_params,
                              const std::string& body,
                              int name_tok);
    const AliasEntry* lookupAliasTemplate(const std::string& name) const;

    // Phase-2 successor scaffolding. See project memory
    // project-frame-based-parser-rewrite.
    enum class FrameKind { Block, For, Class, Function };
    enum class EntryKind {
        LocalVar, Alias, LocalClass, NestedFunc, Enum, Class,
        ImportedHeader, UnnamedGlobal, FunctionAlias, Const,
        ExternalMethod, SlidModule, Global, Namespace, Function,
        Instantiation, ClassDef,
    };

    // Per-block scope. parseBlock calls pushFrame/popFrame at the symmetric
    // site; for-scope sites (short and long form) also use pushFrame;
    // class-body and external-method sites push directly with their own
    // seeding via appendAliasEntry et al.
    // FrameKind defaults to Block; class-body pushes pass Class so popFrame
    // snapshots the scope's entries into reopen_cache_ for stage C splice.
    void pushFrame(FrameKind kind = FrameKind::Block);
    void popFrame();

    struct FrameBase {
        int enclosing_frame_id = -1;
        int own_frame_id = -1;             // -1 unless this entry opens a scope
        std::string base_name;             // as lexed, NOT canonical
        int file_id = -1;
        int tok = 0;
        EntryKind entry_kind = EntryKind::LocalVar;
        virtual ~FrameBase() = default;
    };

    struct LocalVarEntry : FrameBase {
        bool is_array = false;
        int  array_count = 0;
        int  array_rank = 0;
        bool is_tuple = false;
        int  tuple_count = 0;
        std::string type;
    };

    // Plain alias when type_params is empty; template alias otherwise.
    // is_seed marks entries added by seedAliasesFrom — they exist for
    // resolution during an impl_parser's parse but don't belong to that
    // parser's Program (the consumer's parser already published them).
    struct AliasEntry : FrameBase {
        std::string body;
        std::vector<std::string> type_params;
        bool is_seed = false;
    };

    // base_name = short name, canonical = full canonical (func-path-prefixed).
    struct LocalClassEntry : FrameBase {
        std::string canonical;
    };

    struct NestedFuncEntry : FrameBase {
        std::string canonical;
    };

    // base_name is the fully-qualified colon-form key (e.g. "Outer:Inner:E"
    // for nested enums, "E" for file-scope). Resolves enum-type names at
    // parseTypeName and supplies iteration count for the short-form for loop.
    // values is populated for file-scope entries (no ':' in base_name) so
    // emitEnumsIntoProgram can round-trip the full EnumDef; class-scope
    // path entries leave it empty (only their existence and count matter).
    struct EnumEntry : FrameBase {
        int value_count = 0;
        std::vector<std::string> values;
    };

    // Scope-opener entry for a class declaration. Lives in the enclosing
    // frame; FrameBase::own_frame_id holds the class body's frame id, so
    // subsequent class-body entries (aliases, etc) point back at it via
    // their enclosing_frame_id. base_name = canonical dot-form class name.
    // base_class_name = canonical name of this class's base (empty for
    // non-derived). May still be a short name during parse; post-merge
    // fixup canonicalizes. Stage C splice unification consumes this.
    struct ClassEntry : FrameBase {
        std::string base_class_name;
    };

    // base_name = resolved header / impl path. Order in master_list_ is the
    // order in which paths were registered with Program; emit walks in
    // append order.
    struct ImportedHeaderEntry : FrameBase {};

    // base_name = type-name spelling. Round-trips to UnnamedGlobal.
    struct UnnamedGlobalEntry : FrameBase {};

    // Function alias `alias name = target;` inside a namespace block.
    // base_name = lhs name; target = rhs name; namespace_name from the
    // enclosing namespace block (empty at file scope, which is currently
    // disallowed by the parser but kept for symmetry).
    struct FunctionAliasEntry : FrameBase {
        std::string target;
        std::string namespace_name;
    };

    // Wrap-def form for heavy structs — FrameBase mirrors the lookup-key
    // fields (base_name/tok/file_id), the inner def holds the full
    // Program-side payload. Append helpers copy lookup keys into FrameBase
    // and move the def in; emit moves the def out into Program.
    struct ConstEntry            : FrameBase { ConstDef                     def; };
    struct GlobalEntry           : FrameBase { GlobalDef                    def; };
    struct NamespaceEntry        : FrameBase { NamespaceDef                 def; };
    struct ExternalMethodEntry   : FrameBase { ExternalMethodDef            def; };
    struct FunctionEntry         : FrameBase { FunctionDef                  def; };
    struct InstantiationEntry    : FrameBase { Program::InstantiateRequest  def; };
    // ClassDefEntry is the value-carrying entry for class declarations
    // (corresponds to one entry in program.classes). Distinct from the
    // existing ClassEntry, which is the lighter scope-opener marker used
    // by stage C splice unification.
    struct ClassDefEntry          : FrameBase { SlidDef                     def; };

    // base_name = slid (class) name, module = providing .slh module name.
    // Translator dedups on emit (first writer wins — matches today's
    // map::emplace semantics).
    struct SlidModuleEntry : FrameBase {
        std::string module;
    };

    // Phase-2 master list and frame-id stack. Live for the LocalVar and
    // Alias kinds; remaining lanes (local_classes, nested_funcs) still on
    // the legacy Frame above.
    std::vector<std::unique_ptr<FrameBase>> master_list_;
    std::vector<std::pair<int, std::size_t>> frame_ids_;   // (own_frame_id, entries_start)
    std::vector<FrameKind> frame_kinds_;                   // parallel to frame_ids_
    std::vector<FrameBase*> frame_entries_;
    std::map<int, std::vector<FrameBase*>> reopen_cache_;  // keyed by own_frame_id
    int next_frame_id_ = 0;

    // Alias-entry helpers. findAliasInFrame returns the entry if one with
    // this name exists at the given frame_id (any kind — plain or template);
    // nullptr otherwise. appendAliasEntry adds a new entry under the given
    // frame_id without checking for duplicates (caller decides policy).
    const AliasEntry* findAliasInFrame(int frame_id,
                                       const std::string& name) const;
    // file_id < 0 → use the parser's current file_id_. Callers crossing TU
    // boundaries (e.g. .slh import arms) pass the source header's file_id
    // explicitly so the entry preserves origin for diagnostics and dump.
    void appendAliasEntry(int frame_id,
                          const std::string& name,
                          const std::string& body,
                          std::vector<std::string> type_params,
                          int tok,
                          int file_id = -1);

    // LocalClass-entry helpers. Same frame-scoped semantics as the alias
    // pair. findLocalClassInFrame returns the entry if one exists at the
    // given frame_id; appendLocalClassEntry adds without duplicate check.
    const LocalClassEntry* findLocalClassInFrame(int frame_id,
                                                  const std::string& name) const;
    void appendLocalClassEntry(int frame_id,
                                const std::string& name,
                                const std::string& canonical,
                                int tok);

    const NestedFuncEntry* findNestedFuncInFrame(int frame_id,
                                                  const std::string& name) const;
    void appendNestedFuncEntry(int frame_id,
                                const std::string& name,
                                const std::string& canonical,
                                int tok);

    // Enum-entry helpers. Keys are colon-form for nested enums, bare for
    // file-scope. appendEnumEntry adds without dedup; rbegin-walk semantics
    // in lookupEnum mean a later write shadows an earlier same-name entry
    // (matches the legacy map-upsert behavior for the forward-decl-then-fill
    // flow in .slh class bodies). values populated for file-scope (used by
    // emitEnumsIntoProgram); class-scope path entries leave it empty.
    // file_id < 0 → use the parser's current file_id_.
    const EnumEntry* lookupEnum(const std::string& name) const;
    void appendEnumEntry(const std::string& name, int value_count, int tok,
                         std::vector<std::string> values = {},
                         int file_id = -1);

    // Stage E translators. Walk master_list_, emit Program fields. Sole
    // producers of the affected fields; called at end of parse().
    // - emitAliasesIntoProgram: type_aliases + type_alias_templates;
    //   skips is_seed AliasEntries.
    // - emitEnumsIntoProgram: program.enums (file-scope EnumEntries only,
    //   discriminated by absence of ':' in base_name).
    // - emit{ImportedHeaders,UnnamedGlobals,FunctionAliases,Consts}: round-
    //   trip the corresponding Program vector. Consts emit moves rhs out
    //   of ConstEntry (mutating self) — hence non-const.
    void emitAliasesIntoProgram(Program& program) const;
    void emitEnumsIntoProgram(Program& program) const;
    void emitImportedHeadersIntoProgram(Program& program) const;
    void emitUnnamedGlobalsIntoProgram(Program& program) const;
    void emitFunctionAliasesIntoProgram(Program& program) const;
    void emitConstsIntoProgram(Program& program);
    void emitExternalMethodsIntoProgram(Program& program);
    void emitSlidModulesIntoProgram(Program& program) const;
    void emitGlobalsIntoProgram(Program& program);
    void emitNamespacesIntoProgram(Program& program);
    void emitFunctionsIntoProgram(Program& program);
    void emitInstantiationsIntoProgram(Program& program);
    void emitClassDefsIntoProgram(Program& program);

    // Append helpers for the new entry kinds. All add at file-scope frame.
    void appendImportedHeaderEntry(const std::string& path);
    void appendUnnamedGlobalEntry(const std::string& type_name, int tok);
    void appendFunctionAliasEntry(const std::string& name,
                                  const std::string& target,
                                  const std::string& namespace_name,
                                  int tok, int file_id = -1);
    void appendConstEntry(ConstDef def);
    void appendExternalMethodEntry(ExternalMethodDef def);
    void appendSlidModuleEntry(const std::string& slid_name,
                                const std::string& module);
    void appendGlobalEntry(GlobalDef def);
    NamespaceEntry* appendNamespaceEntry(NamespaceDef def);
    NamespaceEntry* findNamespaceEntry(const std::string& name);
    void appendFunctionEntry(FunctionDef def);
    void appendInstantiationEntry(Program::InstantiateRequest req);
    void appendClassDefEntry(SlidDef def);
    ClassDefEntry* findClassDefEntry(const std::string& name, bool is_template);

    // Pass `program` from file-scope callers so file-scope aliases also flow
    // into Program (cross-TU propagation through .slh imports). Block-scope
    // callers pass nullptr. Class-scope callers pass `slid` so the alias is
    // captured into SlidDef.aliases for cross-TU propagation via .slh.
    void parseAliasDecl(Program* program = nullptr, SlidDef* slid = nullptr);

    // const decl parser: assumes `const` is the current token. Returns the parsed
    // ConstDef. The kind of statement-or-decl context (top-level vs class vs block)
    // decides where the result is stored.
    ConstDef parseConstDef();

    // when > 0, ':' terminates the current expression — used in contexts like
    // `case <expr>:` to keep the namespace-call lookahead from eating the label colon.
    int colon_terminates_expr_ = 0;

    // lookahead: pos_ is at '<'; returns true if this is a template type-arg list followed by '('
    bool isTemplateCallLookahead() const;
    // lookahead: pos_ is at '<'; returns true if this is a template type-arg list (not a comparison)
    bool isTemplateTypeArgLookahead() const;
    // lookahead: pos_ is at identifier, pos_+1 is '<'; returns true if Name<Types>; pattern
    bool isInstantiationLookahead() const;
    // lookahead: pos_ is at identifier used as a type name; returns true if a var-name identifier follows
    bool isVarDeclLookahead() const;
    // lookahead: tokens_[name_idx] is a class-name identifier; returns true if
    // `[<...>] (...) {` follows it — the matching ')' followed by '{'.
    bool slidBodyFollows(int name_idx) const;
    // lookahead: returns true if pos_ begins a slid definition — `Identifier
    // [<...>] (...) {`. Distinguishes a class def from a ctor-call statement
    // (ends in ';') and a method/nested function (return type before the name).
    bool isSlidDeclLookahead() const;
    // lookahead: returns true if pos_ begins a derived slid definition —
    // `Base : Derived [<...>] (...) {`.
    bool isDerivedSlidDeclLookahead() const;
    // Rename a just-parsed local slid to a unique canonical name, register its
    // short name in the current block, and collect it into pending_slids_.
    void collectLocalClass(SlidDef slid, const std::string& short_name, int name_tok);
    // Block-level two-pass: walk the current block's tokens at depth 0, pre-
    // register every local class def's short→canonical name into the current
    // frame's local_classes lane before statements parse. Same-block duplicate
    // class names error here. pos_ is restored.
    void prescanLocalClasses();

    // op-symbol recognition: returns canonical "<sym>" (e.g. "+=", "<-", "[]") if
    // the token(s) at pos_+offset (peek) or pos_ (consume) form an overloadable op symbol.
    std::optional<std::string> peekOpSymbolAt(int offset);
    std::optional<std::string> consumeOpSymbol();
    // Validates the explicit-parameter count of an in-class op<sym> method.
    // Errors at op_tok if the count doesn't match the spec for the named op.
    // No-op for non-op names (regular methods).
    void checkOpArity(const std::string& op_name, int actual, int op_tok,
                      const std::vector<std::unique_ptr<Expr>>& param_defaults);
    void checkOpMutable(const std::string& op_name,
                        const std::vector<std::pair<std::string,std::string>>& params,
                        const std::vector<bool>& param_mutable,
                        const std::vector<int>& param_mut_toks,
                        int op_tok);

    SlidDef parseSlidDef(const std::string& base_name = "");
    // Rejects a hoisted class whose short name equals any transitive enclosing
    // class on the current parse chain. Walks enclosing_class_names_ and throws
    // a CompileError with a note pointing at the shadowed enclosing's
    // declaration.
    void rejectShadowOfEnclosing(const std::string& inner_name, int inner_file_id, int inner_tok);
    // Consumes `Ident (: Ident)* ':'` from a `Base : Derived(...)` header and
    // returns the canonical (dot-joined) base name. Leaves pos_ at the derived
    // identifier. Precondition: isDerivedSlidDeclLookahead() returned true.
    std::string consumeDerivedBasePrefix();
    // Dot-joined canonical of the currently-parsing class chain (outermost
    // first). Empty at file scope, "Outer" inside Outer's body,
    // "Outer.Inner" inside Outer's Inner, and so on.
    std::string enclosingClassPath() const;
    // Parses a single global slid declaration. Caller has already verified that
    // `pos_` points at the `global` keyword and ruled out the `global;` lifetime
    // statement shape. `namespace_prefix` is "" at file scope, the enclosing
    // class name for class-internal globals, or the enclosing function name for
    // function-internal globals. `visible_in_function` mirrors the function-name
    // prefix and is empty for file/class-scope globals.
    GlobalDef parseGlobalDef(const std::string& namespace_prefix,
                             const std::string& visible_in_function);
    // Parses the bare file-scope short form `TYPE NAME = EXPR;` (no leading
    // `global` keyword) into the unnamed namespace. Caller has confirmed the
    // shape via lookahead.
    GlobalDef parseBareGlobalShortForm();
    // collapse multiple SlidDef entries with the same class name into a single
    // merged entry. multiple entries arise from reopens; codegen expects one
    // logical class per name. fields, methods, and ctor/dtor bodies are
    // concatenated/picked; open-state flags are AND'd; private-suffix flags
    // are OR'd.
    void mergeReopens(Program& program);
    EnumDef parseEnumDef();
    // `class_name` is the enclosing class. When non-empty the method body
    // parses with `current_function_name_ = "Class:method"`, so any
    // function-internal `global` declared in the body lands in the method's
    // qualified namespace (and stays invisible outside that method).
    MethodDef parseMethodDef(const std::string& class_name = "");
    ExternalMethodDef parseExternalMethodDef();
    void parseNamespace(Program& program);           // Name { ... }  /  Name import { ... }
    NestedFunctionDef parseNestedFunctionDef();
    FunctionDef parseFunctionDef();
    std::unique_ptr<BlockStmt> parseBlock(std::vector<std::string> predeclare = {});
    std::unique_ptr<Stmt> parseStmt();
    // Lvalue-led tail: lhs has been parsed; consume '=' / '<-' / '<->' or ';'
    // and build the matching specialized stmt node.
    std::unique_ptr<Stmt> parseLvalueTail(std::unique_ptr<Expr> lhs);
    std::unique_ptr<Stmt> buildAssignFromLhs(std::unique_ptr<Expr> lhs,
                                             std::unique_ptr<Expr> rhs, bool is_move,
                                             int op_tok);
    std::unique_ptr<Stmt> buildSwapFromLhs(std::unique_ptr<Expr> lhs,
                                            std::unique_ptr<Expr> rhs);
    std::unique_ptr<Stmt> buildCompoundAssignFromLhs(std::unique_ptr<Expr> lhs,
                                                      const std::string& op,
                                                      std::unique_ptr<Expr> rhs,
                                                      int op_tok);
    std::unique_ptr<SwitchStmt> parseSwitchStmt();

    // expression precedence levels
    std::unique_ptr<Expr> parseExpr();
    std::unique_ptr<Expr> parseLogicalAnd();
    std::unique_ptr<Expr> parseBitOr();
    std::unique_ptr<Expr> parseBitXor();
    std::unique_ptr<Expr> parseBitAnd();
    std::unique_ptr<Expr> parseEquality();
    std::unique_ptr<Expr> parseRelational();
    std::unique_ptr<Expr> parseShift();
    std::unique_ptr<Expr> parseAddSub();
    std::unique_ptr<Expr> parseMulDiv();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parsePrimary();
    std::unique_ptr<Expr> parsePostfix(std::unique_ptr<Expr> base);
};
