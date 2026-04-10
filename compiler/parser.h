#pragma once
#include "token.h"
#include <string>
#include <vector>
#include <memory>

// --- Expressions ---

struct Expr {
    virtual ~Expr() = default;
};

struct StringLiteralExpr : Expr {
    std::string value;
    StringLiteralExpr(std::string v) : value(std::move(v)) {}
};

struct IntLiteralExpr : Expr {
    int value;
    IntLiteralExpr(int v) : value(v) {}
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

// new T[n] — heap allocation, returns ptr
struct NewExpr : Expr {
    std::string elem_type;
    std::unique_ptr<Expr> count;
    NewExpr(std::string t, std::unique_ptr<Expr> n)
        : elem_type(std::move(t)), count(std::move(n)) {}
};

// --- Statements ---

struct Stmt {
    virtual ~Stmt() = default;
};

struct VarDeclStmt : Stmt {
    std::string type;
    std::string name;
    std::unique_ptr<Expr> init;           // null for default construction
    std::vector<std::unique_ptr<Expr>> ctor_args; // for Counter c(5)
    VarDeclStmt(std::string type, std::string name,
                std::unique_ptr<Expr> init,
                std::vector<std::unique_ptr<Expr>> ctor_args = {})
        : type(std::move(type)), name(std::move(name)),
          init(std::move(init)), ctor_args(std::move(ctor_args)) {}
};

struct AssignStmt : Stmt {
    std::string name;
    std::unique_ptr<Expr> value;
    AssignStmt(std::string name, std::unique_ptr<Expr> value)
        : name(std::move(name)), value(std::move(value)) {}
};

// field assignment: obj.field_ = expr
struct FieldAssignStmt : Stmt {
    std::unique_ptr<Expr> object;
    std::string field;
    std::unique_ptr<Expr> value;
    FieldAssignStmt(std::unique_ptr<Expr> obj, std::string field,
                    std::unique_ptr<Expr> value)
        : object(std::move(obj)), field(std::move(field)), value(std::move(value)) {}
};

struct ReturnStmt : Stmt {
    std::unique_ptr<Expr> value; // may be null for void return
    ReturnStmt(std::unique_ptr<Expr> v) : value(std::move(v)) {}
};

struct CallStmt : Stmt {
    std::string callee;
    std::vector<std::unique_ptr<Expr>> args;
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

// delete ptr — free heap allocation
struct DeleteStmt : Stmt {
    std::unique_ptr<Expr> operand;
    DeleteStmt(std::unique_ptr<Expr> op) : operand(std::move(op)) {}
};

// deref assign: ptr^ = expr  or  ptr^.field = expr handled via FieldAssignStmt on DerefExpr
struct DerefAssignStmt : Stmt {
    std::unique_ptr<Expr> ptr;   // the pointer expression
    std::unique_ptr<Expr> value;
    DerefAssignStmt(std::unique_ptr<Expr> ptr, std::unique_ptr<Expr> val)
        : ptr(std::move(ptr)), value(std::move(val)) {}
};

// ptr++^ = expr  — store val at current ptr, then advance ptr
struct PostIncDerefAssignStmt : Stmt {
    std::unique_ptr<Expr> ptr;   // the pointer variable
    std::string op;              // "++" or "--"
    std::unique_ptr<Expr> value;
    PostIncDerefAssignStmt(std::unique_ptr<Expr> ptr, std::string op, std::unique_ptr<Expr> val)
        : ptr(std::move(ptr)), op(std::move(op)), value(std::move(val)) {}
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

struct ForRangeStmt : Stmt {
    std::string var_type;   // empty = use existing variable
    std::string var_name;
    std::unique_ptr<Expr> range_start;
    std::unique_ptr<Expr> range_end;
    std::unique_ptr<BlockStmt> body;
    std::string block_label; // optional :name after }
};

// for EnumType var in EnumType { ... }
struct ForEnumStmt : Stmt {
    std::string var_type;   // the enum type name
    std::string var_name;
    std::string enum_name;  // the enum being iterated
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
};

struct MethodDef {
    std::string return_type;
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;
    std::unique_ptr<BlockStmt> body;
};

struct SlidDef {
    std::string name;
    std::vector<FieldDef> fields;
    std::unique_ptr<BlockStmt> ctor_body;          // loose code (implicit ctor), or null
    std::unique_ptr<BlockStmt> explicit_ctor_body; // _() { ... }, or null
    std::unique_ptr<BlockStmt> dtor_body;          // ~() { ... }, or null
    std::vector<MethodDef> methods;
};

// nested function defined inside a parent function body
struct NestedFunctionDef {
    std::string return_type;
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;
    std::unique_ptr<BlockStmt> body;
};

// nested function definition appearing inside a block
struct NestedFunctionDefStmt : Stmt {
    NestedFunctionDef def;
};

struct FunctionDef {
    std::string return_type;
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;
    std::unique_ptr<BlockStmt> body;
};

struct Program {
    std::vector<EnumDef> enums;
    std::vector<SlidDef> slids;
    std::vector<FunctionDef> functions;
};

// --- Parser ---

class Parser {
public:
    Parser(std::vector<Token> tokens);
    Program parse();

private:
    std::vector<Token> tokens_;
    int pos_;

    Token& peek();
    Token& advance();
    Token& expect(TokenType type, const std::string& msg);
    bool isTypeName(const Token& t);
    bool isUserTypeName(const Token& t);
    std::string parseTypeName();

    SlidDef parseSlidDef();
    EnumDef parseEnumDef();
    MethodDef parseMethodDef();
    NestedFunctionDef parseNestedFunctionDef();
    FunctionDef parseFunctionDef();
    std::unique_ptr<BlockStmt> parseBlock();
    std::unique_ptr<Stmt> parseStmt();
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
