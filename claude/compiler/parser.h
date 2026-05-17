#pragma once
#include "token.h"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <optional>
#include <set>

// Strip every leading "const " qualifier substring from a type string.
// "const int" -> "int"; "const (const T)^" -> "T^"; non-pointer and pointer
// types are handled uniformly. The canonical form is used for overload-match,
// mangling, and dedup-key comparisons where const distinctions on by-value
// slots and pointee-const on pointer slots collapse together. Also strips
// redundant single-element paren wrappers — the "(const T)^" form produced
// by default-const-on-indirect-params collapses to "T^" after const-strip.
// Real anon-tuple types ("(t1,t2,...)") keep their parens.
inline std::string canonicalType(const std::string& s) {
    std::string t;
    t.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s.compare(i, 6, "const ") == 0) { i += 6; continue; }
        t.push_back(s[i++]);
    }
    // Strip single-element paren wrappers ("(X)..." where X has no top-level
    // comma — i.e. X is not itself a tuple). Repeats for nested wraps.
    while (t.size() >= 2 && t.front() == '(') {
        int depth = 0;
        size_t close = 0;
        bool has_top_comma = false;
        for (size_t k = 0; k < t.size(); k++) {
            if (t[k] == '(') depth++;
            else if (t[k] == ')') { depth--; if (depth == 0) { close = k; break; } }
            else if (t[k] == ',' && depth == 1) has_top_comma = true;
        }
        if (close == 0 || has_top_comma) break;
        t = t.substr(1, close - 1) + t.substr(close + 1);
    }
    return t;
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
    std::vector<ConstDef> consts;                  // class-scope const decls (no storage)
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

struct Program {
    std::vector<EnumDef> enums;
    std::vector<SlidDef> slids;
    std::vector<FunctionDef> functions;
    std::vector<ExternalMethodDef> external_methods;
    std::vector<ConstDef> consts;                   // program-scope const decls (no storage)
    std::vector<GlobalDef> globals;                 // global slid declarations (file/class/function-internal)
    std::vector<NamespaceDef> namespaces;           // declared namespace blocks
    std::vector<AliasDef> aliases;                   // function aliases (alias x = y;)

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
           std::shared_ptr<std::set<std::string>> imported_once = nullptr);
    Program parse();

private:
    SourceMap& sm_;
    int file_id_;
    std::vector<Token> tokens_;
    int pos_;
    std::string source_dir_;
    std::vector<std::string> import_paths_; // --import-path dirs (searched for .slh headers)
    std::shared_ptr<std::set<std::string>> imported_once_; // shared across nested parsers; import-once guard

    Token& peek();
    Token& advance();
    Token& expect(TokenType type, const std::string& msg);
    [[noreturn]] void errorHere(const std::string& msg);
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
                        std::vector<bool>& param_mutable,
                        std::vector<int>& param_mut_toks,
                        std::vector<std::unique_ptr<Expr>>& param_defaults);

    // Per-scope record for each declared local. Visibility (the name being in
    // the map at all) drives decl-vs-assign disambiguation; the optional
    // properties feed shape-aware downstream logic (short-form for, etc).
    struct LocalInfo {
        int  tok = 0;           // token of the name at its declaration site
        bool is_array = false;
        int  array_count = 0;   // outer dim
        int  array_rank = 0;    // number of dims; >1 rejected by short-form for
        bool is_tuple = false;
        int  tuple_count = 0;   // anon-tuple element count
        std::string type;       // declared type ("" when not tracked / inferred)
    };
    std::vector<std::map<std::string, LocalInfo>> scope_stack_;
    void declareVar(const std::string& name, int name_tok);
    bool isInScope(const std::string& name) const;
    int arrayCountInScope(const std::string& name) const; // 0 if not a fixed-size array local
    int arrayRankInScope(const std::string& name) const;  // 0 if not a fixed-size array local
    int tupleSizeInScope(const std::string& name) const;  // 0 if not a tuple local
    std::string typeInScope(const std::string& name) const; // "" if not recorded
    LocalInfo* findLocal(const std::string& name);        // innermost frame; nullptr if not in scope

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
    // enum type → value count; populated as enums are parsed. Used by the
    // short-form for loop to recognize `for (x : EnumName)` and supply the
    // iteration count for the desugared ForLongStmt.
    std::map<std::string, int> enum_sizes_;
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

    // Name of the function/method whose body is currently being parsed.
    // Empty at file scope, set by parseFunctionDef / parseMethodDef /
    // parseExternalMethodDef before parsing the body, restored after.
    // Drives the namespace_prefix used for function-internal `global` decls.
    std::string current_function_name_;

    // Globals collected during the current top-level declaration (file-scope
    // long form, class-internal, function-internal). Drained into
    // `program.globals` at the end of each loop iteration in `parse()`.
    std::vector<GlobalDef> pending_globals_;

    // Local classes (slids defined inside a code block). Collected during the
    // current top-level declaration, drained into `program.slids` alongside
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

    // Per-block short-name → canonical-name map for local classes. Innermost
    // frame is the current block; pushed/popped by parseBlock alongside
    // scope_stack_. parseTypeName resolves a bare type name's base component
    // through this (without finalizing — colon suffixes still apply).
    std::vector<std::map<std::string, std::string>> local_class_stack_;
    std::string lookupLocalClass(const std::string& name) const;

    // user-declared type aliases (alias Name = TypeExpr;). innermost frame is
    // current block; bottom frame is file-scope. resolved type strings are
    // already in canonical form (e.g. "int^", "Class.Hoisted", "Template__int")
    // and substituted by parseTypeName when a bare ident matches.
    struct AliasInfo { std::string resolved; int tok = 0; };
    std::vector<std::map<std::string, AliasInfo>> alias_stack_{1};
    void declareAlias(const std::string& name, const std::string& resolved, int name_tok);
    std::string lookupAlias(const std::string& name) const;
    void parseAliasDecl();

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

    SlidDef parseSlidDef();
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
