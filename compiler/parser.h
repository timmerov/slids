#pragma once
#include "token.h"
#include <string>
#include <vector>
#include <memory>

// --- AST nodes ---

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

struct Stmt {
    virtual ~Stmt() = default;
};

struct ReturnStmt : Stmt {
    std::unique_ptr<Expr> value;
    ReturnStmt(std::unique_ptr<Expr> v) : value(std::move(v)) {}
};

struct CallStmt : Stmt {
    std::string callee;
    std::vector<std::unique_ptr<Expr>> args;
    CallStmt(std::string callee, std::vector<std::unique_ptr<Expr>> args)
        : callee(std::move(callee)), args(std::move(args)) {}
};

struct FunctionDef {
    std::string return_type;
    std::string name;
    std::vector<std::pair<std::string, std::string>> params; // type, name
    std::vector<std::unique_ptr<Stmt>> body;
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

    FunctionDef parseFunctionDef();
    std::unique_ptr<Stmt> parseStmt();
    std::unique_ptr<Expr> parseExpr();
};
