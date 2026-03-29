#pragma once
#include "parser.h"
#include <string>
#include <ostream>

class Codegen {
public:
    Codegen(const Program& program, std::ostream& out);
    void emit();

private:
    const Program& program_;
    std::ostream& out_;
    int str_counter_;

    std::string emitStringConstant(const std::string& value);
    void emitFunction(const FunctionDef& fn);
    std::string emitExpr(const Expr& expr);
    void emitStmt(const Stmt& stmt);

    // track string constants already emitted
    std::vector<std::pair<std::string, std::string>> string_constants_; // label, value
};
