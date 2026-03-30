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

// --- Statements ---

struct Stmt {
    virtual ~Stmt() = default;
};

struct VarDeclStmt : Stmt {
    std::string type;
    std::string name;
    std::unique_ptr<Expr> init;
    VarDeclStmt(std::string type, std::string name, std::unique_ptr<Expr> init)
        : type(std::move(type)), name(std::move(name)), init(std::move(init)) {}
};

struct AssignStmt : Stmt {
    std::string name;
    std::unique_ptr<Expr> value;
    AssignStmt(std::string name, std::unique_ptr<Expr> value)
        : name(std::move(name)), value(std::move(value)) {}
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

struct FunctionDef {
    std::string return_type;
    std::string name;
    std::vector<std::pair<std::string, std::string>> params; // type, name
    std::unique_ptr<BlockStmt> body;
};

struct Program {
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
    std::string parseTypeName();

    FunctionDef parseFunctionDef();
    std::unique_ptr<BlockStmt> parseBlock();
    std::unique_ptr<Stmt> parseStmt();

    // expression precedence levels
    std::unique_ptr<Expr> parseExpr();        // logical or, xor
    std::unique_ptr<Expr> parseLogicalAnd();  // logical and
    std::unique_ptr<Expr> parseBitOr();       // bitwise or
    std::unique_ptr<Expr> parseBitXor();      // bitwise xor
    std::unique_ptr<Expr> parseBitAnd();      // bitwise and
    std::unique_ptr<Expr> parseEquality();    // == !=
    std::unique_ptr<Expr> parseRelational();  // < > <= >=
    std::unique_ptr<Expr> parseShift();       // << >>
    std::unique_ptr<Expr> parseAddSub();      // + -
    std::unique_ptr<Expr> parseMulDiv();      // * / %
    std::unique_ptr<Expr> parseUnary();       // ! ~ -
    std::unique_ptr<Expr> parsePrimary();
};
