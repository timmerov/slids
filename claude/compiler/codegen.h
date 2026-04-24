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
    bool is_transport_impl = false; // impl side: emit __pinit, treat as complete locally
    int public_field_count = 0;    // number of public fields before private ones (for __pinit)
    int64_t sizeof_override = 0;   // >0: use this value for sizeof()
    int64_t padding_bytes = 0;     // extra opaque bytes appended after known fields in struct type
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
    void writeSliFile(std::ostream& out) const;

private:
    const Program& program_;
    std::ostream& out_;
    int str_counter_;
    int tmp_counter_;
    int label_counter_;

    std::map<std::string, std::string> locals_;      // var name -> alloca reg
    std::map<std::string, std::string> local_types_; // var name -> declared type (slid name or "int^" etc)
    std::set<std::string> emitted_alloca_regs_;      // all alloca register names emitted in current function
    std::map<std::string, std::string> func_return_types_;
    std::map<std::string, std::vector<std::string>> func_param_types_; // func name -> param types
    std::map<std::string, std::vector<std::pair<std::string,std::string>>> func_tuple_fields_; // func -> [(type,name)]
    std::string current_func_return_type_; // LLVM return type of the function being emitted
    std::vector<std::pair<std::string,std::string>> current_func_tuple_fields_; // Slids-form tuple return fields (type,name) for element-wise dispatch; empty if not a tuple return
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
        std::string stack_ptr_reg;  // @llvm.stacksave result for this loop body, or ""
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

    // dtor tracking: ordered list for locals (and tuple elements) with dtors
    // in declaration order — dtors called in reverse on return.
    // tuple_index >= 0 means: dtor target is GEP of tuple var at that index.
    struct DtorVar {
        std::string var_name;
        std::string slid_type;
        int tuple_index = -1;
    };
    std::vector<DtorVar> dtor_vars_;

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
    std::vector<FunctionDef> pending_declares_;        // imported templates: declare at module scope
    std::set<std::string> local_template_names_;       // templates defined in this TU (inline always)
    std::map<std::string, std::string> template_func_modules_; // imported template name -> module

    // template slid (class template) support
    std::map<std::string, const SlidDef*> template_slids_;       // base name -> template def (not owned)
    std::set<std::string> emitted_slid_templates_;               // mangled names already processed
    std::map<std::string, SlidDef> concrete_slid_template_defs_; // mangled name -> owned concrete SlidDef
    std::vector<SlidDef*> pending_slid_instantiations_;          // local instantiations: emit full bodies
    std::vector<SlidDef*> pending_slid_declares_;                // imported instantiations: emit struct + declares only
    std::set<std::string> local_slid_template_names_;            // template class names defined in this TU
    std::map<std::string, std::string> slid_template_modules_;   // imported template class name -> module

    // .sli tracking (for imported template instantiations)
    struct SliImport { std::string module; bool is_template; };
    std::vector<SliImport> sli_imports_;
    std::set<std::string> sli_import_set_;
    struct SliInstantiation { std::string func_name; std::vector<std::string> type_args; };
    std::vector<SliInstantiation> sli_instantiations_;
    std::set<std::string> sli_instantiation_set_;

    std::set<std::string> exported_symbols_; // mangled names with external linkage

    void collectStringConstants();
    void collectFunctionSignatures();
    void collectSlids();
    bool isExported(const std::string& mangled) const;
    std::string instantiateSlidTemplate(const std::string& name,
                                        const std::vector<std::string>& type_args,
                                        bool force = false);
    void ensureSlidInstantiated(const std::string& type);
    void scanForSlidTemplateUses();
    // template instantiation: builds substituted FunctionDef, registers signatures.
    // force=true: always emit define (for explicit instantiate statements).
    // force=false: inline if local template, else emit declare + record .sli entry.
    // returns mangled name (e.g. "add__int")
    std::string instantiateTemplate(const std::string& name,
                                    const std::vector<std::string>& type_args,
                                    bool force = false);
    void emitTemplateDeclare(const FunctionDef& fn);
    void recordSliEntry(const std::string& func_name, const std::vector<std::string>& type_args);
    // infer type args from actual call arguments when no explicit <T> is given
    std::vector<std::string> inferTypeArgs(const FunctionDef& tmpl,
                                           const std::vector<std::unique_ptr<Expr>>& args);
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
    std::string resolveOpEq(const std::string& base, const Expr& arg);
    // Element-type list for a struct-like type (slid name or anon tuple).
    std::vector<std::string> fieldTypesOf(const std::string& struct_type);
    // Emit a GEP selecting field `i` of a struct-like type. Returns the register.
    std::string emitFieldGep(const std::string& struct_type,
                             const std::string& ptr, int i);
    // Per-field copy or move from src to dst. For `is_move`, pointer/iterator
    // fields are nulled in the source after transfer, and embedded slid fields
    // recurse. Copy and move are identical for value fields.
    // `struct_type` may be a slid name or an anon tuple.
    // `is_init` means the destination is a freshly-alloca'd slot that hasn't
    // been constructed yet. When true, the slot is default-constructed
    // (emitConstructAtPtrs with no args) before the copy/move, so every object
    // that will later be dtor'd at scope exit has a matching ctor call.
    // Default false (reassignment — target is already a live object).
    void emitSlidAssign(const std::string& struct_type, const std::string& dst_ptr,
                        const std::string& src_ptr, bool is_move, bool is_init = false);
    // Assign a single slid (or anon-tuple) slot. For slid types, prefers user
    // op<- (move) or op= (copy) taking Type^ when defined; otherwise falls
    // back to emitSlidAssign (default field-by-field walk). For anon-tuple
    // types, always falls back (anon-tuples have no user ops). This is the
    // desugar-rule entry point: wherever an operation lands on a slid-typed
    // slot, go through this helper so the user's op is invoked.
    void emitSlidSlotAssign(const std::string& elem_type, const std::string& dst_ptr,
                            const std::string& src_ptr, bool is_move, bool is_init = false);
    // Elementwise arithmetic/bitwise op on same-typed anon-tuple operands at pointer
    // level. For each slot of `ttype`: scalar → load+op+store; slid → user
    // op<op>(Elem^,Elem^) call; nested anon-tuple → recurse. `op` is a scalar
    // operator string ("+","-","*","/","%","&","|","^","<<",">>").
    void emitElementwiseAtPtr(const std::string& ttype,
                              const std::string& l_ptr, const std::string& r_ptr,
                              const std::string& res_ptr, const std::string& op);
    std::string emitSlidAlloca(const std::string& slid_name); // alloca + default-init fields + ctor
    std::string emitRawSlidAlloca(const std::string& slid_name); // alloca only, no init, no dtor
    void emitConstructAt(const std::string& stype, const std::string& ptr,
                         const std::vector<std::unique_ptr<Expr>>& args,
                         const std::vector<std::unique_ptr<Expr>>& overrides = {}); // init fields + ctor at ptr; overrides[i] wins over args[i]
    void emitConstructAtPtrs(const std::string& stype, const std::string& ptr,
                             const std::vector<const Expr*>& args,
                             const std::vector<const Expr*>& overrides); // same, but with raw ptrs (used for recursion)
    // Destructure `init` into `targets` (type,name pairs — empty name skips that slot).
    // Dispatches on source shape: tuple literal, VarExpr of anon-tuple type, or generic.
    // Slid-typed slots in the anon-tuple-var path are moved element-wise via emitSlidAssign.
    void emitDestructure(const std::vector<std::pair<std::string,std::string>>& targets,
                         const Expr& init);
    bool isFreshSlidTemp(const Expr& expr); // true if expr produces a fresh temp alloca we can mutate
    std::string exprSlidType(const Expr& expr); // return slid type name if expr produces a slid value
    std::string exprType(const Expr& expr);     // return full Slids type string of expr, or ""
    void emitFunction(const FunctionDef& fn);
    void emitNestedFunction(const NestedFunctionDef& fn,
                            const std::string& parent_name,
                            const NestedFuncInfo& info);
    void emitBlock(const BlockStmt& block);
    void emitStmt(const Stmt& stmt);
    void emitDtors(); // call dtors for all in-scope slid vars that have one
    void emitStackRestore(int to_frame); // emit stackrestore for frames [top..to_frame]
    std::string emitExpr(const Expr& expr);
    std::string emitCondBool(const Expr& expr); // emit expr then icmp ne <type> val, 0 -> i1
    std::string exprLlvmType(const Expr& expr); // infer LLVM type without emitting IR
    std::string inferSlidType(const Expr& expr); // infer Slids type string for type-inferred declarations
    std::string emitFieldPtr(const std::string& obj_name, const std::string& field);
    std::string newTmp();
    std::string newLabel(const std::string& prefix);
    std::string uniqueAllocaReg(const std::string& var_name);
    std::string llvmType(const std::string& slids_type);
};
