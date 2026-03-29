#pragma once
#include "parser.h"
#include <string>
#include <ostream>
#include <map>

class Codegen {
public:
    Codegen(const Program& program, std::ostream& out);
    void emit();

private:
    const Program& program_;
    std::ostream& out_;
    int str_counter_;
    int tmp_counter_;

    // local variable name -> alloca register name
    std::map<std::string, std::string> locals_;

    std::vector<std::pair<std::string, std::string>> string_constants_;

    void collectStringConstants();
    void emitFunction(const FunctionDef& fn);
    void emitStmt(const Stmt& stmt);
    std::string emitExpr(const Expr& expr);
    std::string newTmp();
};
