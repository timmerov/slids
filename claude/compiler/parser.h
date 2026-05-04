#pragma once
#include "token.h"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <optional>
#include <set>

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

// ptr++^  — post-increment pointer then dereference the OLD address (rvalue)
// or used as lvalue: ptr++^ = val  — store val at current ptr, then increment ptr
struct PostIncDerefExpr : Expr {
    std::unique_ptr<Expr> operand; // the pointer variable
    std::string op; // "++" or "--"
    PostIncDerefExpr(std::unique_ptr<Expr> op_expr, std::string op)
        : operand(std::move(op_expr)), op(std::move(op)) {}
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
    std::unique_ptr<Expr> operand;   // non-null for name/type; null for others
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

// ptr++^ = expr  — store val at current ptr, then advance ptr
struct PostIncDerefAssignStmt : Stmt {
    std::unique_ptr<Expr> ptr;   // the pointer variable
    std::string op;              // "++" or "--"
    std::unique_ptr<Expr> value;
    bool is_move = false;
    PostIncDerefAssignStmt(std::unique_ptr<Expr> ptr, std::string op, std::unique_ptr<Expr> val,
                           bool m = false)
        : ptr(std::move(ptr)), op(std::move(op)), value(std::move(val)), is_move(m) {}
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
    int tok = 0;                       // token index of the field name (for diagnostics)
};

struct MethodDef {
    std::string return_type;
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;
    std::vector<bool> param_mutable;  // parallel to params; true if 'mutable' on that param
    std::unique_ptr<BlockStmt> body;
    bool is_virtual = false;  // `virtual` keyword present on the declaration
    bool is_pure = false;     // body replaced by `= delete;` — slot exists, no impl
};

struct SlidDef {
    std::string name;
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
    bool dtor_is_virtual = false;        // `virtual ~()` — required when class is virtual
    bool is_transport_impl = false;      // this slid emits __$pinit for the consumer
    int public_field_count = 0;          // number of public fields before private ones (for __$pinit)
    bool is_local = true;                // false when template body loaded from an imported .sl file
    std::string impl_module;             // module name of the impl file (when !is_local)
    bool is_namespace = false;           // declared as `Name { ... }` only — no `()` data block ever
    std::unique_ptr<BlockStmt> ctor_body;          // loose code (implicit ctor), or null
    std::unique_ptr<BlockStmt> explicit_ctor_body; // _() { ... }, or null
    std::unique_ptr<BlockStmt> dtor_body;          // ~() { ... }, or null
    std::vector<MethodDef> methods;
    std::vector<SlidDef> nested_slids;             // slid defs declared inside this slid's body
};

// nested function defined inside a parent function body
struct NestedFunctionDef {
    std::string return_type; // empty when tuple_return_fields is non-empty
    std::vector<std::pair<std::string, std::string>> tuple_return_fields;
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;
    std::vector<bool> param_mutable;  // parallel to params
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
    std::unique_ptr<BlockStmt> body;
    std::vector<std::string> type_params; // non-empty for template functions: T add<T>(T a, T b)
    bool is_local = true;        // false when body loaded from a separate impl file
    std::string impl_module;     // module name of the impl file (when !is_local)
};

// method defined outside the class body: void String:clear() { ... }
// method_name is "_" for ctor, "~" for dtor, else the method name
struct ExternalMethodDef {
    std::string slid_name;
    std::string return_type;
    std::string method_name;
    std::vector<std::pair<std::string, std::string>> params;
    std::vector<bool> param_mutable;  // parallel to params
    std::unique_ptr<BlockStmt> body;
    bool is_virtual = false;
    bool is_pure = false;
};

struct Program {
    std::vector<EnumDef> enums;
    std::vector<SlidDef> slids;
    std::vector<FunctionDef> functions;
    std::vector<ExternalMethodDef> external_methods;

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

    // Per-scope record for each declared local. Visibility (the name being in
    // the map at all) drives decl-vs-assign disambiguation; the optional
    // properties feed shape-aware downstream logic (short-form for, etc).
    struct LocalInfo {
        bool is_array = false;
        int  array_count = 0;   // outer dim
        int  array_rank = 0;    // number of dims; >1 rejected by short-form for
        bool is_tuple = false;
        int  tuple_count = 0;   // anon-tuple element count
    };
    std::vector<std::map<std::string, LocalInfo>> scope_stack_;
    void declareVar(const std::string& name, int name_tok);
    bool isInScope(const std::string& name) const;
    int arrayCountInScope(const std::string& name) const; // 0 if not a fixed-size array local
    int arrayRankInScope(const std::string& name) const;  // 0 if not a fixed-size array local
    int tupleSizeInScope(const std::string& name) const;  // 0 if not a tuple local
    LocalInfo* findLocal(const std::string& name);        // innermost frame; nullptr if not in scope

    // field names of the slid currently being parsed (prevents field assignments being inferred as declarations)
    std::set<std::string> current_slid_fields_;
    // all parsed slid field names, keyed by slid name (used for external method blocks)
    std::map<std::string, std::set<std::string>> all_slid_fields_;
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
    // decls and bare-block reopens.
    std::set<std::string> seen_classes_;
    // class names that have been closed in this TU — further tuple-form
    // reopens are an error; bare-block reopens are still allowed.
    std::set<std::string> closed_classes_;
    // short-name → canonical-name aliases for nested slids in the current outer's body
    // (e.g. "Inner" → "Outer.Inner") — applied by parseTypeName
    std::map<std::string, std::string> nested_alias_;

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

    // op-symbol recognition: returns canonical "<sym>" (e.g. "+=", "<-", "[]=", "[]") if
    // the token(s) at pos_+offset (peek) or pos_ (consume) form an overloadable op symbol.
    std::optional<std::string> peekOpSymbolAt(int offset);
    std::optional<std::string> consumeOpSymbol();
    // Validates the explicit-parameter count of an in-class op<sym> method.
    // Errors at op_tok if the count doesn't match the spec for the named op.
    // No-op for non-op names (regular methods).
    void checkOpArity(const std::string& op_name, int actual, int op_tok);
    void checkOpMutable(const std::string& op_name,
                        const std::vector<std::pair<std::string,std::string>>& params,
                        const std::vector<bool>& param_mutable,
                        int op_tok);

    SlidDef parseSlidDef();
    // collapse multiple SlidDef entries with the same class name into a single
    // merged entry. multiple entries arise from reopens; codegen expects one
    // logical class per name. fields, methods, and ctor/dtor bodies are
    // concatenated/picked; open-state flags are AND'd; private-suffix flags
    // are OR'd.
    void mergeReopens(Program& program);
    EnumDef parseEnumDef();
    MethodDef parseMethodDef();
    ExternalMethodDef parseExternalMethodDef();
    void parseExternalMethodBlock(Program& program); // TypeName { method() {...} ... }
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
