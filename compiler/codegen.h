#pragma once
#include "parser.h"
#include <string>
#include <ostream>
#include <map>
#include <vector>
#include <functional>

class Codegen {
public:
    Codegen(const Program& program, std::ostream& out);
    void emit();

private:
    const Program& program_;
    std::ostream& out_;
    int str_counter_;
    int tmp_counter_;
    int label_counter_;

    std::map<std::string, std::string> locals_;
    std::vector<std::pair<std::string, std::string>> string_constants_;

    // function name -> return type
    std::map<std::string, std::string> func_return_types_;

    std::string break_label_;
    std::string continue_label_;

    void collectStringConstants();
    void collectFunctionSignatures();
    void emitFunction(const FunctionDef& fn);
    void emitBlock(const BlockStmt& block);
    void emitStmt(const Stmt& stmt);
    std::string emitExpr(const Expr& expr);
    std::string newTmp();
    std::string newLabel(const std::string& prefix);
    std::string llvmType(const std::string& slids_type);
};
