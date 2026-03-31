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

// method call as statement: obj.method(args);
struct MethodCallStmt : Stmt {
    std::unique_ptr<Expr> object;
    std::string method;
    std::vector<std::unique_ptr<Expr>> args;
    MethodCallStmt(std::unique_ptr<Expr> obj, std::string method,
                   std::vector<std::unique_ptr<Expr>> args)
        : object(std::move(obj)), method(std::move(method)), args(std::move(args)) {}
};

struct BlockStmt : Stmt {
    std::vector<std::unique_ptr<Stmt>> stmts;
};

struct IfStmt : Stmt {
    std::unique_ptr<Expr> cond;
    std::unique_ptr<BlockStmt> then_block;
    std::unique_ptr<BlockStmt> else_block; // may be null
};

struct WhileStmt : Stmt {
    std::unique_ptr<Expr> cond;
    std::unique_ptr<BlockStmt> body;
};

struct ForRangeStmt : Stmt {
    std::string var_type;
    std::string var_name;
    std::unique_ptr<Expr> range_start;
    std::unique_ptr<Expr> range_end;
    std::unique_ptr<BlockStmt> body;
};

struct BreakStmt : Stmt {};
struct ContinueStmt : Stmt {};

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
    std::unique_ptr<BlockStmt> ctor_body; // may be null
    std::vector<MethodDef> methods;
};

struct FunctionDef {
    std::string return_type;
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;
    std::unique_ptr<BlockStmt> body;
};

struct Program {
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
    MethodDef parseMethodDef();
    FunctionDef parseFunctionDef();
    std::unique_ptr<BlockStmt> parseBlock();
    std::unique_ptr<Stmt> parseStmt();

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
