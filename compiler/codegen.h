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
    bool has_pinit = false;        // consumer: call __pinit instead of __ctor
    bool is_transport_impl = false; // transport impl: emit __pinit, treat as complete locally
    int public_field_count = 0;    // transport side: number of public (known) fields before private ones
    int64_t sizeof_override = 0; // >0: use this value for sizeof() and pad the struct definition
    int64_t padding_bytes = 0;   // extra opaque bytes appended after known fields in struct type
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
    std::map<std::string, std::vector<std::pair<std::string,std::string>>> func_tuple_fields_; // func -> [(type,name)]
    std::string current_func_return_type_; // LLVM return type of the function being emitted
    // overload table: base_mangled -> [(full_mangled, param_types)]
    std::map<std::string, std::vector<std::pair<std::string, std::vector<std::string>>>> method_overloads_;
    // free function overload table: base_name -> [(mangled_name, param_types)]
    std::map<std::string, std::vector<std::pair<std::string, std::vector<std::string>>>> free_func_overloads_;
    std::map<std::string, SlidInfo>    slid_info_;
    std::vector<std::pair<std::string, std::string>> string_constants_;

    std::string break_label_;
    std::string continue_label_;
    std::string current_slid_;
    std::string self_ptr_;
    bool block_terminated_ = false; // true after br/ret, reset at each new label
    bool current_func_uses_sret_ = false; // true when current function uses sret for return

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
    std::map<std::string, ArrayInfo> parent_array_info_; // array info from parent, used in nested functions

    // dtor tracking: ordered list of (var_name, slid_type) for locals with dtors
    // in declaration order — dtors called in reverse on return
    std::vector<std::pair<std::string,std::string>> dtor_vars_;

    // temporaries created during expression evaluation (e.g. implicit String from literal)
    // flushed at end of each statement in emitBlock
    std::vector<std::pair<std::string,std::string>> pending_temp_dtors_; // (alloca_reg, type_name)

    // nested function support
    std::map<std::string, NestedFuncInfo> nested_info_; // mangled -> info
    std::string current_parent_;   // mangled name of current parent function
    std::string frame_ptr_reg_;    // %frame ptr inside a nested function

    // template function support
    std::map<std::string, const FunctionDef*> template_funcs_; // name -> template def (not owned)
    std::set<std::string> emitted_templates_;  // mangled names already emitted
    std::vector<FunctionDef> pending_instantiations_; // concrete fns to emit after main functions

    void collectStringConstants();
    void collectFunctionSignatures();
    void collectSlids();
    // template instantiation: builds substituted FunctionDef, registers signatures,
    // adds to pending_instantiations_; returns mangled name (e.g. "add__int")
    std::string instantiateTemplate(const std::string& name,
                                    const std::vector<std::string>& type_args);
    void analyzeNestedFunctions(const FunctionDef& fn);
    std::set<std::string> collectCaptures(
        const BlockStmt& body,
        const std::set<std::string>& parent_locals,
        const std::set<std::string>& own_params);

    void emitFrameStruct(const FunctionDef& fn);
    void emitSlidCtorDtor(const SlidDef& slid);
    void emitSlidMethod(const SlidDef& slid, const std::string& full_mangled,
                        const std::string& return_type,
                        const std::vector<std::pair<std::string,std::string>>& params,
                        const BlockStmt& body);
    void emitSlidMethods(const SlidDef& slid);
    std::string resolveMethodMangledName(const std::string& slid_name,
                                         const std::string& method_name,
                                         const std::vector<std::pair<std::string,std::string>>& params);
    std::string resolveOverloadForCall(const std::string& base_mangled,
                                       const std::vector<std::unique_ptr<Expr>>& args);
    bool isPointerExpr(const Expr& expr);
    bool isUnsignedExpr(const Expr& expr);
    std::string resolveOperatorOverload(const std::string& op,
                                        const Expr& left, const Expr& right);
    std::string emitArgForParam(const Expr& arg, const std::string& param_type);
    std::string exprSlidType(const Expr& expr); // return slid type name if expr produces a slid value
    void emitFunction(const FunctionDef& fn);
    void emitNestedFunction(const NestedFunctionDef& fn,
                            const std::string& parent_name,
                            const NestedFuncInfo& info);
    void emitBlock(const BlockStmt& block);
    void emitStmt(const Stmt& stmt);
    void emitDtors(); // call dtors for all in-scope slid vars that have one
    std::string emitExpr(const Expr& expr);
    std::string emitCondBool(const Expr& expr); // emit expr then icmp ne <type> val, 0 -> i1
    std::string exprLlvmType(const Expr& expr); // infer LLVM type without emitting IR
    std::string inferSlidType(const Expr& expr); // infer Slids type string for type-inferred declarations
    std::string emitFieldPtr(const std::string& obj_name, const std::string& field);
    std::string newTmp();
    std::string newLabel(const std::string& prefix);
    std::string llvmType(const std::string& slids_type);
};
