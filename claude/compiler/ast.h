#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "widen.h"   // widen::TypeRef — the structured type handle

namespace ast {

enum class Kind {
    kProgram,
    kFunctionDef,
    kFunctionDecl,
    kVarDeclStmt,
    kAssignStmt,
    kAugAssignStmt,
    kStoreStmt,     // store through an lvalue expr; children[0]=lvalue, [1]=rhs.
    kMoveStmt,      // `a <-- b;` transient — desugar lowers to copy-assign + null
    kSwapStmt,      // `a <--> b;` transient — desugar lowers to temp + 3 assigns
    kDestructureStmt,// `(a, b, ) = tuple;` children[0]=rhs, [1..]=target lvalues
                    // (a NULL child is a skipped slot).
    kDeleteStmt,    // delete p; — free + null the pointer. children[0]=lvalue var.
    kDtorCallStmt,  // lvalue.~(); — explicit destructor call (no free). [0]=lvalue.
    kCallStmt,
    kCallExpr,
    kExprStmt,
    kReturnStmt,
    kBlockStmt,     // { stmts } — a nested lexical scope; children = statements.
    kIfStmt,        // if (cond) then [else else]; children[0] = condition,
                    // [1] = then-branch, [2] = optional else-branch.
    kWhileStmt,     // while (cond) body; children[0] = condition, [1] = body.
    kDoWhileStmt,   // post-condition while; children[0] = condition, [1] = body.
    kForLongStmt,   // long-form for; children[0]=cond, [1]=update, [2]=body,
                    // [3..]=varlist decls. The canonical for node.
    kBreakStmt,     // break;
    kContinueStmt,  // continue;
    kGlobalScopeStmt, // `global;` — opens the global lifetime; at the enclosing
                    // scope's exit the lazy-global dtor registry (__$global_dtor_all)
                    // runs. Auto-inserted at the top of `main` when absent.
    kSwitchStmt,    // switch (value) { clauses }; children[0]=scrutinee,
                    // [1..]=kCaseClause (source order).
    kCaseClause,    // a label-list + body block: children[0..n-2]=labels (null=
                    // default), children.back()=body block. text=="continue" =>
                    // trailing fall-through into the next clause.
    kStringLiteral,
    kIntLiteral,
    kUintLiteral,
    kCharLiteral,
    kBoolLiteral,
    kFloatLiteral,
    kNullptrLiteral,// `nullptr` — typeless null pointer; lowers to `ptr null`.
    kIdentExpr,
    kUnaryExpr,
    kBinaryExpr,
    kPreIncExpr,    // survives the parse->ast copy; lowered away by desugar's PPID pass
    kPostIncExpr,
    kAddrOfExpr,    // prefix `^lvalue` — address-of; children[0]=operand lvalue.
    kDerefExpr,     // postfix `lvalue^` — dereference; children[0]=operand.
    kIndexExpr,     // postfix `base[index]` — array subscript; children[0]=base,
                    // [1]=index. `a[x][y]` nests ((a[x])[y]).
    kTupleExpr,     // tuple literal `(e0, e1, ...)`; children = slot exprs.
    kNewExpr,       // new T / new T[n] / new(addr) T[n]. return_type=element T;
                    // children[0]=array-size (or null), [1]=placement-addr (or
                    // null). Yields T^ (single) or T[] (array).
    kCastExpr,      // prefix `<Type^> operand` — pointer reinterpret cast.
                    // inferred_type = target; children[0]=operand. Codegen emits a
                    // ptrtoint/inttoptr only at the intptr boundary; ptr↔ptr is a
                    // no-op (opaque `ptr`).
    kConvertExpr,   // `(Type=expr)` value conversion. inferred_type = target value
                    // type; children[0]=operand. Codegen changes the bits via the
                    // full conversion grid (widen::convertExplicit) or a pointer
                    // lowering (ptr->intptr / ptr->bool non-null test).

    kSeqExpr,       // synthesized by desugar: children evaluated in order; value_index
                    // names the result child, the rest are bumps run for effect
    kBumpExpr,      // synthesized by desugar: resolved_entry_id + inferred_type + text
                    // ("++"/"--") — a `x = x ± 1` effect on a scalar variable
    kParam,
};

struct Node {
    Kind kind;
    std::string name;
    std::string text;
    widen::TypeRef return_type = widen::kNoType;
    widen::TypeRef nominal_type = widen::kNoType;   // literal nodes: from constfold
    widen::TypeRef inferred_type = widen::kNoType;  // expr nodes: in-context, from classify
    widen::TypeRef op_type = widen::kNoType;        // binary's computational type
    int file_id = -1;            // source file of the construct
    int tok = -1;                // index into token::List::tokens for error attribution
    int name_tok = -1;           // ident token for named constructs
    int resolved_entry_id = -1;  // ident / lhs / callee -> parse::Tree::entries index
    int value_index = -1;        // kSeqExpr: which child supplies the result value
    int loop_levels = -1;        // kBreakStmt/kContinueStmt: hops outward in the
                                 // loop/switch context stack to the resolved target
                                 // (0 = innermost), stamped by resolve.
    bool is_const = false;       // kVarDeclStmt: declared with leading `const`
    bool is_construction = false; // kCallExpr: a `Class(args)` nameless class
                                 // construction (children[0] = the construction
                                 // tuple). desugar lifts it into a synthetic
                                 // kVarDeclStmt temp (destroyed at statement end).
    bool ctor_no_args = false;   // kCallExpr[is_construction]: no ctor arguments were named
                                 // (`Class` / `Class()`) — a DEFAULT construction. The chain
                                 // lowering seeds a default accumulator with `op=`; one built
                                 // WITH args must use `op<OP>=` (op= would discard the args).
    bool class_op_chain = false; // kBinaryExpr / kUnaryExpr: a class-PRODUCING operator,
                                 // RESOLVED and STAMPED by classify but not lowered.
                                 // inferred_type = the result class; the LAST child is that
                                 // class's default field-init tuple — children[2] on a binary
                                 // [lhs, rhs, tuple], children[1] on a UNARY [operand, tuple].
                                 // desugar builds the accumulator + the op calls.
    bool field_transfer = false; // kConvertExpr: a class FIELD initialized from an OBJECT —
                                 // children = [the field's DEFAULT field list, the SOURCE].
                                 // codegen's construction WALK builds the field, runs its
                                 // ctor, THEN copies the source in (op=), so the enclosing
                                 // ctor body sees the copied value. See parse.h.
    bool op_collapse_head = false; // kBinaryExpr[class_op_chain]: the head CONSTRUCTION IS the
                                 // accumulator (built into it, never an operand). Stamped by
                                 // classify — the chain lowering OBEYS it rather than
                                 // re-deriving it from lhs.is_construction, because classify
                                 // DECLINES the collapse when it does not fit (see parse.h).
    int op_bin_eid = -1;         // 2-arg `op<OP>(lhs, rhs)`  — the head pair in one call
    int op_un_eid = -1;          // 1-arg `op<OP>(operand)`   — an arity-1 UNARY produce-self
    int op_aug_eid = -1;         // 1-arg `op<OP>=(rhs)`      — the fuse
    int op_eq_lhs_eid = -1;      // 1-arg `op=(lhs)`          — the seed of a decomposed head
    int op_eq_rhs_eid = -1;      // 1-arg `op=(rhs)`          — the seed of a COLLAPSED head
    int op_move_eid = -1;        // 1-arg `op<--(Self^)`      — the move INTO a live target
    bool class_conversion = false; // kConvertExpr: a `(Class = src)` conversion to a
                                 // class — children are [default-construct `_$cret`
                                 // decl, `_$cret.op=(src)` call]; resolved_entry_id is
                                 // the `_$cret` id. desugar lifts both into a temp.
    bool agg_conv_spill = false; // kConvertExpr: an aggregate conversion with a spilled
                                 // side-effecting source — children = [spill decl, the
                                 // per-slot tuple]. desugar hoists the spill, yields the
                                 // tuple.
    bool default_move_init = false;      // kVarDeclStmt: `<--` default-move-init (desugar nulls leaves)
    bool nrvo = false;           // sret NRVO: a kVarDeclStmt whose local IS the
                                 // return slot (built in place, not dtor'd here), and
                                 // each kReturnStmt that returns it (a no-op return).
    bool non_completing = false; // while/do-while/for-long: a constant-true loop
                                 // with no escaping break — its exit block is
                                 // unreachable (emit `unreachable`) and the loop
                                 // is a return-terminator.
    std::vector<std::unique_ptr<Node>> children;
    std::vector<std::unique_ptr<Node>> params;   // kFunctionDef/Decl: kParam nodes
    std::vector<widen::TypeRef> param_types;     // kCallStmt/kCallExpr: resolved fn's param types
    std::vector<int> captures;                   // nested fn + its calls: captured host entry ids
    std::vector<widen::TypeRef> capture_types;   // nested fn: each capture's slids type
    int self_entry_id = -1;                      // method/ctor/dtor: the `self` local
                                                 // (address = `_$recv^`); -1 otherwise
    int vtable_slot = -1;                        // kCallExpr: >=0 means a VIRTUAL dispatch
                                                 // call — codegen loads the vptr at
                                                 // offset 0 of children[0] (the receiver),
                                                 // GEPs this slot, and calls indirect
                                                 // instead of `@name`. -1 = static call.
};

// A per-class vtable: the class's symbol and, per slot, the implementing method's
// symbol (the most-derived override visible to that class). desugar builds these
// (base slots first, overrides reuse the slot, new virtuals append); codegen emits
// `@<class_symbol>__$vtable = ... constant [N x ptr] [ptr @slot0, ...]` and stamps it
// into offset 0 in each virtual class's constructor.
struct Vtable {
    std::string class_symbol;
    std::vector<std::string> slot_symbols;
};

// A file-local global variable, lowered by desugar for codegen. `symbol` is its
// `@`-name (emitted as `@<symbol> = internal global <llvm(type)> <init>`); `init`
// is a literal Node carrying the folded static initializer (codegen emits it as
// the LLVM constant). A use site (kIdentExpr whose resolved_entry_id is a key of
// `Tree::globals`) loads/stores through `@<symbol>` instead of a SymTab alloca.
struct GlobalVar {
    std::string symbol;
    widen::TypeRef type = widen::kNoType;
    std::unique_ptr<Node> init;      // a literal node (may be null → zero-init)
    std::string touch_symbol;        // "" = static; else the lazy group's touch thunk,
                                     // called before any access (first-touch ctor gate)
};

// A LAZY global group (ctor/dtor present): its members are constructed on first
// access. `touch_symbol` names the once-only gate: it checks `sentinel`, and on the
// first call sets it, registers `dtor_symbol` with the runtime LIFO dtor list, then
// runs `ctor_symbol`. codegen emits the sentinel + touch thunk; the ctor/dtor
// themselves are ordinary lifted namespace-member functions.
struct GlobalGroup {
    std::string touch_symbol;
    std::string sentinel_symbol;
    // The SYNTHESIZED group ctor/dtor thunks codegen emits (named from touch_symbol).
    // The touch thunk registers dtor_symbol then calls ctor_symbol. dtor_symbol is ""
    // when the group needs no teardown (no class member, no user dtor).
    std::string ctor_symbol;
    std::string dtor_symbol;
    // A NAMED or ANON group's compound members, in declaration order (keys into
    // `globals`). The ctor thunk constructs each in order then calls user_ctor_symbol;
    // the dtor thunk calls user_dtor_symbol then destructs each in REVERSE. Empty for a
    // bare compound global (see synth_global_id).
    std::vector<int> member_ids;
    // The user-written group `_()`/`~()` (empty if the group has no such hook), called
    // after member construction / before member destruction.
    std::string user_ctor_symbol;
    std::string user_dtor_symbol;
    // >=0: a lone COMPOUND global (array / tuple / class) NOT in a group, whose ctor/dtor
    // are SYNTHESIZED to construct/destruct this global's `@symbol` in place. Keyed by the
    // global's id (into `globals`). Mutually exclusive with member_ids.
    int synth_global_id = -1;
};

struct Tree {
    std::vector<std::unique_ptr<Node>> nodes;
    // Class kSlid types carried from parse, so codegen can emit each class's
    // `<Name>__$sizeof()` helper (GEP-null/ptrtoint — LLVM owns the layout).
    std::vector<widen::TypeRef> classes;
    std::vector<Vtable> vtables;
    // File-local globals, keyed by resolved_entry_id (a use site looks itself up
    // here when it is not a SymTab local). Emitted as `internal @`-globals.
    std::map<int, GlobalVar> globals;
    // Lazy global groups (ctor/dtor). codegen emits a sentinel + touch thunk per
    // group and the shared runtime dtor registry sized to global_groups.size().
    std::vector<GlobalGroup> global_groups;
};

}  // namespace ast
