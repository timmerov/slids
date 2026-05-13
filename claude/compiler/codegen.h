#pragma once
#include "parser.h"
#include <string>
#include <ostream>
#include <map>
#include <set>
#include <unordered_set>
#include <vector>
#include <functional>

struct SlidInfo {
    std::string name;
    int name_file_id = 0;          // location of the class name token (copied from SlidDef for diagnostic notes)
    int name_tok = 0;
    std::map<std::string, int> field_index;
    std::vector<std::string> field_types;
    bool has_explicit_ctor = false;
    bool has_dtor = false;
    // Set by synthesizeCtorNeeds() after has_dtor / has_explicit_ctor settle.
    // True iff this class needs ctor work — either a __$ctor function or an
    // inline walk at the construction site. Computed from: user-declared
    // _()/~(), transport impl, virtual, base needs ctor, any own slid field
    // needs ctor, any anon-tuple field has a slid slot needing ctor.
    bool needs_ctor_fn = false;
    // Set by synthesizeCtorNeeds() = !has_explicit_ctor && needs_ctor_fn.
    // True iff the class has compiler-generated ctor/dtor only (no user `_()`
    // / `~()` anywhere). These classes are tuple-like: no __$ctor / __$dtor
    // symbol is emitted; the synthesized walk is inlined at every site.
    bool must_inline_ctor = false;
    bool has_private_suffix = false;  // class has private fields after `...` (consumer view: layout opaque, alloca size queried via __$sizeof)
    bool is_transport_impl = false; // impl side: emit __$pinit, treat as complete locally
    bool is_empty = false;         // class with () but zero fields: methods/ctor/dtor take no self
    bool is_namespace = false;     // declared as `Name { ... }` only — non-instantiable, called as Name:fn()
    int public_field_count = 0;    // number of public fields before private ones (for __$pinit)
    int64_t sizeof_override = 0;   // >0: use this value for sizeof()
    int64_t padding_bytes = 0;     // extra opaque bytes appended after known fields in struct type
    // inheritance: derived = `Base : Derived(...) { ... }`. After resolveSlidInheritance(),
    // field_types/field_index hold the flat concat: base's flat fields prefix, then own fields.
    std::string base_name;         // name of immediate base; empty if not derived
    SlidInfo* base_info = nullptr; // resolved pointer to base SlidInfo
    int own_field_count = 0;       // count of this class's own fields (the suffix in field_types)
    int base_field_count = 0;      // count of inherited fields (the prefix in field_types)
    bool inheritance_resolved = false;
    // virtual class support. Set in classifyVirtualClasses(). vtable built in buildVtables().
    bool locally_virtual = false;     // class declares its own virtual member or virtual ~
    bool is_virtual_class = false;    // locally_virtual or any virtual ancestor
    bool dtor_is_virtual = false;     // own dtor (or auto-generated) is virtual
    // vtable slot. Built once per class. Walks base→derived, accumulating slots
    // base-first then this class's new additions; overrides replace defining_class.
    struct VtableSlot {
        std::string method_name;             // user-level name (e.g. "hello")
        std::vector<std::string> param_types;
        std::string return_type;
        std::string defining_class;          // class that supplies the body (or "" if pure)
        std::string mangled;                 // mangled function symbol — empty if pure
        bool is_pure = true;
    };
    std::vector<VtableSlot> vtable;
    // Methods declared `= default` or `= delete` in this class (regardless of
    // virtual). Records the contract; the chain is walked at body-redef sites
    // and at call sites to enforce no-shadow / no-call across all descendants.
    struct MethodMark {
        std::string method_name;
        std::vector<std::string> param_types;
        bool is_delete = false;   // remove inherited from this class onward
        bool is_default = false;  // inherits base impl, no shadow allowed
        int file_id = 0;          // location of the `= default` / `= delete` declaration (for diagnostic notes)
        int tok = 0;
    };
    std::vector<MethodMark> method_marks;
    // After validateAndExportable(), true when the class name appears in a
    // .slh header (cross-TU visible). End-of-TU pure-slot validation skips these.
    bool is_importable = false;
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

class SourceMap;

class Codegen {
public:
    Codegen(const Program& program, std::ostream& out, SourceMap& sm, std::string source_file = "");
    void emit();
    void writeSliFile(std::ostream& out) const;

private:
    const Program& program_;
    std::ostream& out_;
    SourceMap& sm_;
    int str_counter_;
    int tmp_counter_;
    int label_counter_;

    // Per-local state, unified from two parallel maps. `reg` is the alloca
    // register name (e.g. "%var_x"); `type` is the slids type string (slid
    // name, "int^", "(int,int)", etc.). All declarations co-populate both
    // fields; reads pick the field they need.
    struct LocalInfo {
        std::string reg;
        std::string type;
        int file_id = 0;        // location of the variable's declaration (for diagnostic notes); 0 when unknown
        int tok = 0;
    };
    std::map<std::string, LocalInfo> locals_;

    // Scope stack — one frame per nested block. Push on entering a block,
    // pop on exit. Pop emits dtors registered during the block in reverse
    // order, then restores the locals_ snapshot. Matches the parser's
    // `scope_stack_` shape (per-frame map of LocalInfo). Three pieces of
    // bookkeeping previously parallel (dtor_mark + saved_locals + saved_local_types)
    // collapsed into one frame struct.
    struct ScopeFrame {
        size_t dtor_mark;
        std::map<std::string, LocalInfo> saved_locals;
    };
    std::vector<ScopeFrame> scope_stack_;
    void pushScope();
    void popScope();
    std::set<std::string> emitted_alloca_regs_;      // all alloca register names emitted in current function
    std::map<std::string, std::string> func_return_types_;
    std::map<std::string, std::vector<std::string>> func_param_types_; // func name -> param types
    std::map<std::string, std::vector<std::pair<std::string,std::string>>> func_tuple_fields_; // func -> [(type,name)]
    std::string current_func_return_type_; // LLVM return type of the function being emitted
    std::string current_func_slids_return_type_; // Slids-form return type of the function being emitted
    std::vector<std::pair<std::string,std::string>> current_func_tuple_fields_; // Slids-form tuple return fields (type,name) for element-wise dispatch; empty if not a tuple return
    // overload-table entry: <mangled, param_types, param_mutable, param_mut_toks, file_id>.
    // param_mutable / param_mut_toks are parallel to param_types; the only matcher
    // distinction const/mutable makes is const-arg → mutable-param rejection at
    // call sites. file_id pairs with the corresponding mut-tok for diagnostic notes.
    using OverloadEntry = std::tuple<
        std::string,                 // 0: mangled
        std::vector<std::string>,    // 1: param_types
        std::vector<bool>,           // 2: param_mutable
        std::vector<int>,            // 3: param_mut_toks
        int>;                        // 4: file_id of the declaration
    // overload table: base_mangled -> [OverloadEntry]
    std::map<std::string, std::vector<OverloadEntry>> method_overloads_;
    // free function overload table: base_name -> [OverloadEntry]
    std::map<std::string, std::vector<OverloadEntry>> free_func_overloads_;
    // mangled names of const-marked methods — looked up at MethodCallExpr/Stmt
    // emit to reject non-const method calls on const receivers.
    std::set<std::string> const_methods_;
    std::map<std::string, SlidInfo>    slid_info_;

    // Folded substitution constant. slid_type is the declared (or inferred)
    // type; int_value / float_value carry the folded value per is_float.
    // No storage, no symbol — looked up at every VarExpr / FieldAccessExpr.
    struct ConstEntry {
        std::string slid_type;
        int64_t int_value = 0;
        double float_value = 0.0;
        bool is_float = false;
        int file_id = 0;
        int tok = 0;
    };
    std::map<std::string, std::map<std::string, ConstEntry>> slid_consts_;
    // block_const_stack_[0] is the file/global scope (pushed once at emit() entry).
    // Each function-body emit pushes a frame on top via pushScope; pops on exit.
    // Block stmts (if/while/for/switch/etc.) push/pop their own frames.
    std::vector<std::map<std::string, ConstEntry>> block_const_stack_;

    void collectAndFoldConsts();
    // Fold a single const def. `slid_scope` is the enclosing class (empty for
    // globals). cycle_set tracks decls currently being resolved.
    void foldConstDef(const ConstDef& cd, const std::string& slid_scope,
                      std::set<std::string>& cycle_set);
    // Fold an Expr against the in-scope const tables. Throws on non-foldable
    // expressions or unresolved references. Returns the folded entry's value
    // and inferred type.
    ConstEntry foldConstExpr(const Expr& e, const std::string& slid_scope,
                             std::set<std::string>& cycle_set);
    // Apply declared_type check and widening to a folded value. Returns the
    // final entry stamped with the declared type. Throws on narrowing.
    ConstEntry applyConstDeclaredType(const ConstDef& cd, const ConstEntry& folded);

    // Pre-check the shape of a const initializer expression. Returns true
    // iff every leaf is a literal, a prior const reference, or one of the
    // foldable operator forms. Used at block scope to decide between
    // substitution (true) and runtime alloca (false) without try/catch.
    bool isFoldableConstShape(const Expr& e, const std::string& slid_scope) const;

    const ConstEntry* lookupConst(const std::string& name) const;
    const ConstEntry* lookupSlidConst(const std::string& slid_name,
                                      const std::string& member) const;
    std::string emitConstValue(const ConstEntry& e) const;

    // Strip leading `const `/`mutable ` qualifiers and unwrap paren-qualified
    // forms like `(const T)X` to `TX`. Used at every type-string consumer that
    // doesn't care about the qualifier (LLVM mapping, slid_info_ lookup, ...).
    std::string canonType(std::string t) const;

    std::vector<std::pair<std::string, std::string>> string_constants_;
    // Register a string constant on-demand from emit-time. Generates a fresh
    // `@.strN` label, records it for end-of-module emission, and returns the
    // label. Replaces the pre-collection scan, which couldn't see template
    // instantiation bodies.
    std::string registerStringConstant(const std::string& value);

    std::string source_file_;        // base filename of the source being compiled (for ##file)
    std::string current_func_name_;  // user-visible name of the function being emitted (for ##func)

    // emission location stack — pushed at major AST traversal boundaries
    // (emitStmt, emitExpr, top-level def emit). Throws read the top to
    // attribute to the current source location.
    std::vector<std::pair<int,int>> emit_stack_;
public:
    void pushEmitLoc(int file_id, int tok) { emit_stack_.emplace_back(file_id, tok); }
    void popEmitLoc() { emit_stack_.pop_back(); }

    struct EmitGuard {
        Codegen& cg;
        EmitGuard(Codegen& c, int f, int t) : cg(c) { cg.pushEmitLoc(f, t); }
        ~EmitGuard() { cg.popEmitLoc(); }
        EmitGuard(const EmitGuard&) = delete;
        EmitGuard& operator=(const EmitGuard&) = delete;
    };

private:
    [[noreturn]] void error(const std::string& msg);
    [[noreturn]] void errorAtNode(const Stmt& s, const std::string& msg);
    [[noreturn]] void errorAtNode(const Expr& e, const std::string& msg);
    // Throw with a single attached "see also" note. Match the existing
    // primary-message conventions (auto-period via finalizeErrorMsg).
    [[noreturn]] void errorWithNote(const std::string& msg,
                                    int note_file, int note_tok,
                                    const std::string& note_msg);

    // Reject const-argument → mutable-param at a resolved call site. Walks
    // args parallel to the param_mutable vector; for any i where
    // param_mutable[i] && the arg type carries `const`, throws "Cannot pass
    // const argument to mutable parameter of '<name>'." with a note pointing
    // at the `mutable` keyword on the param declaration. Caller has already
    // matched the overload; this fires on a successful selection.
    void rejectConstToMutable(
        const std::string& display_name,
        const std::vector<std::unique_ptr<Expr>>& args,
        const std::vector<bool>& param_mutable,
        const std::vector<int>& param_mut_toks,
        int param_file_id);
    // Convenience overload reading mutable/toks/file_id from the OverloadEntry.
    void rejectConstToMutable(
        const std::string& display_name,
        const std::vector<std::unique_ptr<Expr>>& args,
        const OverloadEntry& entry);
    // Post-resolve check for a free-function call. Looks up the chosen overload
    // by mangled name in free_func_overloads_ and fires the rejection.
    void checkResolvedFreeFunction(
        const std::string& callee,
        const std::string& mangled,
        const std::vector<std::unique_ptr<Expr>>& args);
    // Reject a non-const method call on a const receiver. dtor `~` is exempt
    // (it's a destruction window). Method is const iff its mangled name is
    // in const_methods_. Receiver is const iff exprType(object) carries const.
    void checkConstReceiver(
        const Expr& object,
        const std::string& method_name,
        const std::string& mangled);
    [[noreturn]] void errorAtNodeWithNote(const Stmt& s, const std::string& msg,
                                          int note_file, int note_tok,
                                          const std::string& note_msg);
    [[noreturn]] void errorAtNodeWithNote(const Expr& e, const std::string& msg,
                                          int note_file, int note_tok,
                                          const std::string& note_msg);

    std::string break_label_;
    std::string continue_label_;
    std::string current_slid_;
    // `self` has dual representation. `locals_["self"]` carries the type
    // (current_slid_) for type-aware queries (inferSlidType, op-method
    // dispatch, field lookup) and is set once at method/ctor/dtor entry.
    // `self_ptr_` carries the live LLVM ptr value, which can be REMAPPED
    // mid-emit during inline-ctor-body emission for `new T[n]` (see
    // codegen_expr.cpp where self_ptr_ is reassigned to the per-iteration
    // alloca). Code that needs the address reads self_ptr_ (with "%self"
    // fallback for the entry case); code that needs the type reads
    // locals_["self"].type. Several `ve->name == "self"` checks throughout
    // the codegen sit in this niche — they exist because the regular
    // locals_-driven paths can't see the self_ptr_ remap.
    std::string self_ptr_;
    bool block_terminated_ = false; // true after br/ret, reset at each new label
    bool current_func_uses_sret_ = false; // true when current function uses sret for return

    // named/numbered break+continue support
    // each entry: {block_label, break_target, continue_target, stack_ptr_reg, is_switch}
    // continue_target is "" for non-loop blocks
    struct LoopFrame {
        std::string block_label;   // user label, or "" if unnamed
        std::string break_target;
        std::string continue_target; // "" for switch/plain blocks
        std::string stack_ptr_reg;  // @llvm.stacksave result for this loop body, or ""
        bool is_switch = false;     // true for switch frames; numbered break/continue skip these
        // Set during ForLong update-block emission. Named/numbered break/continue
        // resolution skips frames flagged hidden — the for-loop whose update slot
        // is currently being emitted is not a valid break/continue target.
        // (Naked break/continue are blocked separately by clearing
        // break_label_ / continue_label_ during update emission.)
        bool is_hidden = false;
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

    // template function support — multiple overloads can share a base name
    struct TemplateFuncEntry {
        const FunctionDef* def;     // template definition (not owned)
        std::string impl_module;    // module name for cross-TU loading; empty if local
        bool is_local;              // body present in this TU? always inline.
    };
    std::map<std::string, std::vector<TemplateFuncEntry>> template_funcs_;
    std::set<std::string> emitted_templates_;  // mangled names already emitted
    std::vector<FunctionDef> pending_instantiations_; // concrete fns to emit after main functions
    std::vector<FunctionDef> pending_declares_;        // imported templates: declare at module scope

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
    struct SliInstantiation {
        std::string func_name;
        std::vector<std::string> type_args;
        std::vector<std::string> param_types; // post-substitution param types — selects overload
    };
    std::vector<SliInstantiation> sli_instantiations_;
    std::set<std::string> sli_instantiation_set_;

    std::set<std::string> exported_symbols_; // mangled names with external linkage

    void collectStringConstants();
    void collectFunctionSignatures();
    void collectSlids();
    // Resolve `Base : Derived(...)` chains: link base_info pointers, build flat
    // field_index/field_types as base prefix + own suffix, validate F2 collisions.
    void resolveSlidInheritance();
    void resolveSlidInheritanceFor(SlidInfo& info);
    // Walks each class, sets locally_virtual (any own virtual member), then
    // propagates is_virtual_class along the inheritance chain. Validates that
    // every virtual class's base is also virtual (ancestor-dtor-virtuality).
    void classifyVirtualClasses();
    // Builds per-class vtable. Inherits base's slots; this class's additions:
    // new virtual methods may only be added at original declaration (slid.methods),
    // not in reopens (external_methods). Override replaces base's defining_class.
    // Validates: signature exact match on override; `virtual` keyword required to
    // override; non-virtual cannot shadow base virtual.
    void buildVtables();
    // For every method tagged `= default` or `= delete`, validate against the
    // ancestor chain and record a MethodMark on the class. Rejects:
    //   - `= default` / `= delete` on `_` / `~`.
    //   - `= default` with no ancestor match.
    //   - `= delete` non-virtual with no ancestor match.
    // Pure-virtual introduction (`= delete` virtual, no ancestor) is allowed
    // and handled by buildVtables. Must run before buildVtables so the latter
    // can skip vtable updates for marked methods.
    void validateDefaultDelete();
    // Returns true if any ancestor of `class_name` (proper ancestor, in any
    // reopen) declares a method with the given name and parameter types.
    bool ancestorHasMethod(const std::string& class_name,
                           const std::string& method_name,
                           const std::vector<std::string>& pts) const;
    // Walks the inheritance chain (self + ancestors) and returns the first
    // MethodMark matching name+params; returns nullptr and leaves originating
    // class empty if none.
    const SlidInfo::MethodMark* findMethodMark(const std::string& class_name,
                                               const std::string& method_name,
                                               const std::vector<std::string>& pts,
                                               std::string& originating_class) const;
    // After inheritance is resolved, any class whose own fields include a
    // slid-typed field needs a dtor to destroy those fields at scope/heap exit.
    // Sets info.has_dtor accordingly so emitDtors and emitSlidCtorDtor pick it up.
    void synthesizeFieldDtors();
    // Computes info.needs_ctor_fn for every class via fixed-point iteration.
    // Must run after has_dtor / has_explicit_ctor / inheritance are settled.
    // A class needs __$ctor iff: user wrote _()/~(), transport impl, virtual,
    // its base needs __$ctor, or any own slid field (or anon-tuple slot) needs __$ctor.
    void synthesizeCtorNeeds();
    // Mark which classes are importable (declared in any .slh imported by this
    // TU, or whose impl side has a `(...)` transport prefix in .slh). Used to
    // gate end-of-TU pure-slot validation.
    void markImportableClasses();
    // End-of-TU validation (F2 (C)): every concrete non-importable virtual class
    // must have all vtable slots filled.
    void validatePureSlots();
    // Returns true if `derived` has `base` anywhere in its ancestor chain (proper ancestor).
    bool isAncestor(const std::string& base, const std::string& derived);
    // Returns the inheritance chain in base→derived order, including the slid itself.
    std::vector<SlidInfo*> chainOf(const std::string& slid_name);
    // True if any class in the chain (self or ancestor) has a dtor. Used to decide whether
    // a local needs registering for destructor emit.
    bool hasDtorInChain(const std::string& slid_name);
    bool isExported(const std::string& mangled) const;
    std::string instantiateSlidTemplate(const std::string& name,
                                        const std::vector<std::string>& type_args,
                                        bool force = false);
    void ensureSlidInstantiated(const std::string& type);
    void scanForSlidTemplateUses();
    // template instantiation: builds substituted FunctionDef, registers signatures.
    // Caller has already disambiguated the overload via resolveTemplateOverload.
    // force=true: always emit define (for explicit instantiate statements).
    // force=false: inline if local template, else emit declare + record .sli entry.
    // returns mangled name including the post-substitution param-token suffix.
    std::string instantiateTemplate(const TemplateFuncEntry& entry,
                                    const std::vector<std::string>& type_args,
                                    bool force = false);
    void emitTemplateDeclare(const FunctionDef& fn);
    void recordSliEntry(const std::string& func_name,
                        const std::vector<std::string>& type_args,
                        const std::vector<std::string>& param_types,
                        const std::string& impl_module);
    // infer type args from actual call arguments when no explicit <T> is given
    std::vector<std::string> inferTypeArgs(const FunctionDef& tmpl,
                                           const std::vector<std::unique_ptr<Expr>>& args);
    // Pick the template overload (entry + concrete type-args) that matches the call.
    // Returns {nullptr, {}} if `name` is not a template or no overload matches.
    // Throws on ambiguity.
    struct TemplateResolution {
        const TemplateFuncEntry* entry;
        std::vector<std::string> type_args;
    };
    TemplateResolution resolveTemplateOverload(
        const std::string& name,
        const std::vector<std::string>& explicit_type_args,
        const std::vector<std::unique_ptr<Expr>>& args);
    void analyzeNestedFunctions(const FunctionDef& fn);
    std::set<std::string> collectCaptures(
        const BlockStmt& body,
        const std::set<std::string>& parent_locals,
        const std::set<std::string>& own_params);

    void emitFrameStruct(const FunctionDef& fn);
    void emitSlidCtorDtor(const SlidDef& slid);
    void emitSlidMethod(const SlidDef& slid,
                        const std::string& method_user_name,
                        const std::string& full_mangled,
                        const std::string& return_type,
                        const std::vector<std::pair<std::string,std::string>>& params,
                        const std::vector<bool>& param_mutable,
                        const BlockStmt& body,
                        bool is_const_method);
    void emitSlidMethods(const SlidDef& slid);
    std::string resolveMethodMangledName(const std::string& slid_name,
                                         const std::string& method_name,
                                         const std::vector<std::pair<std::string,std::string>>& params);
    // Resolve a free-function call's mangled symbol name. Returns "" if the name
    // is not a known free function. Tries the bare name first (single-overload
    // fast path where mangled == base), then falls back to picking an entry
    // from free_func_overloads_ that matches the argument count.
    std::string resolveFreeFunctionMangledName(const std::string& name,
                                               size_t arg_count) const;
    // Mangling helpers — single source of truth. Mangling is unconditional:
    // every free function gets a __<paramToken> suffix per parameter, every
    // method gets <Slid>__<method>__<paramToken>...  Carve-outs:
    //   - main: emitted bare so the C runtime can find it.
    //   - lifecycle hooks ($ctor/$dtor/$pinit/$sizeof) and libc symbols
    //     (printf/malloc/free) don't go through these helpers at all.
    std::string mangleFreeFunction(const std::string& name,
                                   const std::vector<std::string>& ptypes) const;
    std::string mangleMethod(const std::string& slid_name,
                             const std::string& method_name,
                             const std::vector<std::string>& ptypes) const;
    std::string resolveOverloadForCall(const std::string& base_mangled,
                                       const std::vector<std::unique_ptr<Expr>>& args);
    bool isPointerExpr(const Expr& expr);
    bool isUnsignedExpr(const Expr& expr);
    std::string resolveOperatorOverload(const std::string& op,
                                        const Expr& left, const Expr& right);
    // Returns the mangled name of `<left_slid>__op<op>=(right)` if it exists AND
    // `op` is compoundable. Empty otherwise. Used by binary-op codegen as both
    // a "defer to fuse path" signal and the actual call target for the fuse.
    std::string resolveCompoundFuse(const std::string& op,
                                    const Expr& left, const Expr& right);
    // True if `mangled` is registered as a class method (i.e., its first
    // call-site argument is the implicit `self`). False for free functions
    // and unknown names.
    bool isMethodMangled(const std::string& mangled) const;
    // Best-fit op[] overload for `slid_name` indexed by `index`. Returns the
    // resolved mangled name, or the first-declared op[] overload if no clean
    // match (preserves legacy first-overload behavior). Empty iff `slid_name`
    // has no op[] defined.
    std::string resolveSlidIndex(const std::string& slid_name, const Expr& index);
    // Mangled name of the arity-0 op^ overload on `slid_name`, or "" if none.
    // Used by DerefExpr emit/resolveLvalue/exprType arms to dispatch `x^`
    // when x is a class instance with `op^()` defined (returns a reference
    // to a contained object). Filtering by empty param list distinguishes
    // it from the arity-2 binary-xor overload sharing the same name bucket.
    std::string resolveDerefOverload(const std::string& slid_name);
    std::string emitArgForParam(const Expr& arg, const std::string& param_type);
    // PPID per-`,` flush: wraps emitArgForParam with push/flush so that the
    // arg's post-inc/dec side effects fire at the comma boundary before the
    // next arg starts evaluating. Use ONLY at source-level comma loops
    // (CallExpr / MethodCallExpr / TupleExpr / ctor args). Internal
    // op-method dispatches use plain emitArgForParam — those have no source
    // comma between operands.
    std::string emitPhraseArg(const Expr& arg, const std::string& param_type);
    std::string resolveSingleArgOverload(const std::string& base, const Expr& arg);
    // If `de` dereferences a slid pointer/reference, return the slid type name.
    // Empty if not a slid pointer or the operand can't be resolved.
    std::string derefSlidName(const DerefExpr& de);
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
    // Per-field swap between two same-typed objects. Pointer/iterator fields
    // are exchanged without nullification (both sides keep valid data).
    // Embedded slid / anon-tuple fields recurse via emitSlidSlotSwap.
    // Inline-array fields are not yet supported and throw.
    void emitSlidSwap(const std::string& struct_type,
                      const std::string& a_ptr, const std::string& b_ptr);
    // Swap a single slid (or anon-tuple) slot. For slid types, prefers user
    // op<-> taking Type^ when defined; otherwise falls back to emitSlidSwap.
    // For anon-tuple slots, always falls back (anon-tuples have no user ops).
    void emitSlidSlotSwap(const std::string& elem_type,
                          const std::string& a_ptr, const std::string& b_ptr);
    // Elementwise arithmetic/bitwise op on same-typed anon-tuple operands at pointer
    // level. For each slot of `ttype`: scalar → load+op+store; slid → user
    // op<op>(Elem^,Elem^) call; nested anon-tuple → recurse. `op` is a scalar
    // operator string ("+","-","*","/","%","&","|","^","<<",">>").
    void emitElementwiseAtPtr(const std::string& ttype,
                              const std::string& l_ptr, const std::string& r_ptr,
                              const std::string& res_ptr, const std::string& op);
    // Broadcast a scalar over an anon-tuple, slot-by-slot, at pointer level.
    // Recurses into nested anon-tuple slots; for slid slots requires user
    // `Elem__op<op>(Elem^, scalar_slids)` overload. `scalar_on_left` flips
    // operand order for non-commutative ops.
    void emitTupleScalarBroadcastAtPtr(const std::string& ttype,
                                       const std::string& tup_ptr,
                                       const std::string& scalar_val,
                                       const std::string& scalar_slids,
                                       const std::string& res_ptr,
                                       const std::string& op,
                                       bool scalar_on_left);
    std::string emitSlidAlloca(const std::string& slid_name); // alloca + default-init fields + ctor
    std::string emitRawSlidAlloca(const std::string& slid_name); // alloca only, no init, no dtor
    void emitConstructAt(const std::string& stype, const std::string& ptr,
                         const std::vector<std::unique_ptr<Expr>>& args,
                         const std::vector<std::unique_ptr<Expr>>& overrides = {}); // init fields + ctor at ptr; overrides[i] wins over args[i]
    void emitConstructAtPtrs(const std::string& stype, const std::string& ptr,
                             const std::vector<const Expr*>& args,
                             const std::vector<const Expr*>& overrides); // same, but with raw ptrs (used for recursion)
    // Emit the construct call for a slid. Dispatch:
    //   must_inline_ctor=true  → inline the synthesized walk (emitInlineCtorWalk)
    //   has_explicit_ctor=true → call @<class>__$ctor (or __$ctor_body in
    //                            the closing TU)
    //   otherwise              → no-op (no work to do)
    // Single source of truth shared with emitConstructAtPtrs and the
    // open-coded ctor sites.
    void emitCtorCall(const std::string& class_name, const std::string& ptr);
    // Inline the synthesized ctor walk: vptr (if any) + base call + own
    // slid-field ctor calls (recursing into implicit slid fields). Used for
    // implicit classes (must_inline_ctor=true) that have no __$ctor symbol.
    void emitInlineCtorWalk(const std::string& class_name, const std::string& ptr);
    // Inline the synthesized dtor walk: own slid-field dtors in reverse +
    // base dtor call. Mirror of emitInlineCtorWalk for the destruction side.
    void emitInlineDtorWalk(const std::string& class_name, const std::string& ptr);
    // Field-init half of construction: walk fields and store caller-routed
    // values, recursing into slid sub-fields for primitive routing. Does NOT
    // emit any __$ctor call. The site uses this for sub-slid recursion (where
    // the outer __$ctor_body will call the field's __$ctor); the top-level
    // emitConstructAtPtrs combines this with a single trailing __$ctor call.
    void emitInitFieldsAtPtrs(const std::string& stype, const std::string& ptr,
                              const std::vector<const Expr*>& args,
                              const std::vector<const Expr*>& overrides);
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
    // Walk the inheritance chain derived→base and emit a dtor call for each
    // class that has a dtor. Single source of truth for "destroy this object".
    void emitDtorChainCall(const std::string& slid_type, const std::string& target);
    // Explicit `.~()` call. For primitive / pointer / anon-tuple static types,
    // emit nothing (genuine no-op — useful for generic template code that calls
    // .~() uniformly). For slid types, walk the dtor chain.
    void emitExplicitDtor(const std::string& static_type, const std::string& obj_ptr);
    void emitStackRestore(int to_frame); // emit stackrestore for frames [top..to_frame]
    std::string emitExpr(const Expr& expr);
    std::string emitCondBool(const Expr& expr); // emit expr then icmp ne <type> val, 0 -> i1
    // coerce a value of any built-in type to i1 truthy: ptr → != null,
    // float/double → fcmp une <T> X, 0.0, integer → icmp ne <T> X, 0.
    std::string emitToBool(const std::string& val, const std::string& llvm_type);
    std::string exprLlvmType(const Expr& expr); // infer LLVM type without emitting IR
    void requirePtrInit(const std::string& dst_type, const Expr& src); // dst is ^ or [] -> src must be ptr w/ compatible pointee
    // Reject "primitive lhs ← slid rhs" — there is no implicit slid-to-primitive
    // conversion. Returns early when dst is itself indirect or a slid (those
    // paths are handled by op= dispatch and requirePtrInit).
    void requireCompatibleInit(const std::string& dst_type, const Expr& src);
    // Pointer ([]) / reference (^) restrictions on a binary op:
    //   - cross (ptr × ref) — only `==` and `!=` allowed
    //   - reference-only — arithmetic forbidden, ordered comparison forbidden,
    //     `==` / `!=` require same base type
    // Idempotent and side-effect-free when the operand shapes are valid.
    // Called by both emitExpr(BinaryExpr) (for live emission) and exprLlvmType
    // / inferSlidType (so static type probes can't outrun the validator).
    void validateBinaryPtrRefRestrictions(const BinaryExpr& b);
    // Emit init/assign value with nullptr-compatibility check. nullptr → ^/[] returns
    // "null"; nullptr → intptr returns "0" (i64 zero); nullptr → any other
    // primitive errors. Otherwise delegates to emitExpr.
    std::string valOrNullptrCheck(const std::string& dst_type, const Expr& src);
    std::string inferSlidType(const Expr& expr); // infer Slids type string for type-inferred declarations
    std::string emitFieldPtr(const std::string& obj_name, const std::string& field);
    // If `base` is a VarExpr naming an inline-array local, emit a GEP to element
    // [index] and return {gep_register, slids_elem_type}. Otherwise {"", ""}.
    std::pair<std::string, std::string>
    emitInlineArrayElemPtr(const Expr& base, const Expr& index);

    // Resolved lvalue: an LLVM ptr register holding the address of an
    // addressable expression, plus the slids-form type at that address.
    // Single recursive walker, intended to replace per-context resolvers
    // (DeleteStmt nullify, SwapStmt slot, FieldAssignStmt object, etc.).
    // Errors on non-addressable shapes — call sites that may have non-lvalue
    // operands must dispatch on AST kind first.
    struct Lvalue {
        std::string addr;   // LLVM ptr register
        std::string type;   // slids type at that address
    };
    Lvalue resolveLvalue(const Expr& e);

    // Per-layer subscript driller. Each iteration reclassifies `type`:
    // anon-tuple → struct-field GEP; slid with op[] → call dispatch; inline
    // array → array GEP; ptr/iter → load + GEP. Used by the AIE arm of
    // resolveLvalue and by emitExpr's AIE wrapper after they produce the
    // first (addr, type).
    Lvalue drillIndexChain(std::string addr, std::string type,
        const std::vector<const Expr*>& indices, int from, const Expr& err_site);

    // Type-only mirror of drillIndexChain: walks indices against `type`
    // without emitting IR. Used by inferSlidType for AIE chains.
    std::string drillIndexedType(std::string type,
        const std::vector<const Expr*>& indices, int from);

    // Per-phrase post-inc/dec schedule. PPID model: post-inc/dec defers its
    // side effect to the next terminator (`,`, `;`, or closing `)` of a
    // condition / argument list). Each "phrase" — between two consecutive
    // terminators — has its own queue frame on this stack.
    //
    // Lifecycle: pushPostIncQueue at phrase entry; emitPrePass to fire pre
    // advances; emit operands; flushPostIncQueue at exit (drains posts, pops).
    struct PendingAdvance {
        enum Kind { Pointer, Int, Float } kind;
        std::string addr;       // ptr to the storage (operand alloca / GEP)
        std::string llvm_type;  // for Pointer: pointee element type (GEP stride)
                                // for Int / Float: storage value type
        int step;               // +1 for ++, -1 for --
    };
    std::vector<std::vector<PendingAdvance>> post_inc_stack_;
    // Pre-pass: marks UnaryExpr(pre++/pre--) nodes whose advance has already
    // fired at phrase entry, so the eval site loads the stored (advanced)
    // value rather than re-emitting the advance. Parallel stack to
    // post_inc_stack_; pushed/popped together.
    std::vector<std::unordered_set<const UnaryExpr*>> pre_done_stack_;
    void pushPostIncQueue();
    void flushPostIncQueue();
    void schedulePostInc(PendingAdvance::Kind kind, const std::string& addr,
                         const std::string& llvm_type, int step);
    void emitPrePass(const Expr& root);
    void emitPrePass(const Stmt& root);
    bool preAlreadyDone(const UnaryExpr* u);
    std::string newTmp();
    std::string newLabel(const std::string& prefix);
    std::string uniqueAllocaReg(const std::string& var_name);
    std::string llvmType(const std::string& slids_type);

    // Construct an AST node and propagate source location. Mirrors Parser::make<>.
    // Used by codegen-side desugaring (e.g. compound-assign rewrite) so that any
    // diagnostic emitted on the synthesized node points at the original source
    // token instead of token 0 / file 0.
    template<typename T, typename... Args>
    std::unique_ptr<T> synthAt(int file_id, int tok, Args&&... args) {
        auto p = std::make_unique<T>(std::forward<Args>(args)...);
        p->file_id = file_id;
        p->tok = tok;
        return p;
    }
};
