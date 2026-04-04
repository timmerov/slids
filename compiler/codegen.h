#pragma once
#include "parser.h"
#include <string>
#include <ostream>
#include <map>
#include <set>
#include <vector>
#include <functional>

struct SlidInfo {
    std::string name;
    std::map<std::string, int> field_index;
    std::vector<std::string> field_types;
    bool has_explicit_ctor = false;
    bool has_dtor = false;
};

// info about a nested function's capture set
struct NestedFuncInfo {
    std::string mangled_name;       // @parent__nested
    std::set<std::string> captures; // parent vars accessed
    std::string parent_name;        // parent function name
    // calling convention:
    // 0 captures -> no hidden param
    // 1 capture  -> pass ptr to that single var
    // 2+ captures -> pass ptr to frame struct
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
    std::map<std::string, std::string> local_types_; // var name -> declared type (slid name or "int^" etc)
    std::map<std::string, std::string> func_return_types_;
    std::map<std::string, std::vector<std::string>> func_param_types_; // func name -> param types
    std::map<std::string, SlidInfo>    slid_info_;
    std::vector<std::pair<std::string, std::string>> string_constants_;

    std::string break_label_;
    std::string continue_label_;
    std::string current_slid_;
    std::string self_ptr_;
    bool block_terminated_ = false; // true after br/ret, reset at each new label

    // named/numbered break+continue support
    // each entry: {block_label, break_target, continue_target}
    // continue_target is "" for non-loop blocks
    struct LoopFrame {
        std::string block_label;   // user label, or "" if unnamed
        std::string break_target;
        std::string continue_target; // "" for switch/plain blocks
    };
    std::vector<LoopFrame> loop_stack_;

    // enum support: enum value name -> integer value
    std::map<std::string, int> enum_values_;  // enumerator name -> int value
    std::map<std::string, int> enum_sizes_;   // enum type name -> number of values

    // array support: var name -> {elem_type, dims, flat_alloca_reg}
    struct ArrayInfo {
        std::string elem_type;
        std::vector<int> dims;
        std::string alloca_reg; // ptr to flat [N x i32] alloca
    };
    std::map<std::string, ArrayInfo> array_info_;

    // dtor tracking: ordered list of (var_name, slid_type) for locals with dtors
    // in declaration order — dtors called in reverse on return
    std::vector<std::pair<std::string,std::string>> dtor_vars_;

    // nested function support
    std::map<std::string, NestedFuncInfo> nested_info_; // mangled -> info
    std::string current_parent_;   // mangled name of current parent function
    std::string frame_ptr_reg_;    // %frame ptr inside a nested function

    void collectStringConstants();
    void collectFunctionSignatures();
    void collectSlids();
    void analyzeNestedFunctions(const FunctionDef& fn);
    std::set<std::string> collectCaptures(
        const BlockStmt& body,
        const std::set<std::string>& parent_locals,
        const std::set<std::string>& own_params);

    void emitFrameStruct(const FunctionDef& fn);
    void emitSlidCtorDtor(const SlidDef& slid);
    void emitSlidMethods(const SlidDef& slid);
    void emitFunction(const FunctionDef& fn);
    void emitNestedFunction(const NestedFunctionDef& fn,
                            const std::string& parent_name,
                            const NestedFuncInfo& info);
    void emitBlock(const BlockStmt& block);
    void emitStmt(const Stmt& stmt);
    void emitDtors(); // call dtors for all in-scope slid vars that have one
    std::string emitExpr(const Expr& expr);
    std::string emitFieldPtr(const std::string& obj_name, const std::string& field);
    std::string newTmp();
    std::string newLabel(const std::string& prefix);
    std::string llvmType(const std::string& slids_type);
};
