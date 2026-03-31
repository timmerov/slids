#pragma once
#include "parser.h"
#include <string>
#include <ostream>
#include <map>
#include <vector>
#include <functional>

struct SlidInfo {
    std::string name;
    std::map<std::string, int> field_index;  // field name -> index
    std::vector<std::string> field_types;    // in order
};

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

    std::map<std::string, std::string> locals_;      // var name -> alloca reg
    std::map<std::string, std::string> local_types_; // var name -> slid type name
    std::map<std::string, std::string> func_return_types_;
    std::map<std::string, SlidInfo>    slid_info_;
    std::vector<std::pair<std::string, std::string>> string_constants_;

    std::string break_label_;
    std::string continue_label_;
    std::string current_slid_;  // non-empty when inside a method
    std::string self_ptr_;      // ptr to self when emitting ctor body

    void collectStringConstants();
    void collectFunctionSignatures();
    void collectSlids();

    void emitSlidMethods(const SlidDef& slid);
    void emitFunction(const FunctionDef& fn);
    void emitBlock(const BlockStmt& block);
    void emitStmt(const Stmt& stmt);
    std::string emitExpr(const Expr& expr);
    std::string emitFieldPtr(const std::string& obj_name, const std::string& field);
    std::string newTmp();
    std::string newLabel(const std::string& prefix);
    std::string llvmType(const std::string& slids_type);
};
