#pragma once

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
};

struct Tree {
    std::vector<std::unique_ptr<Node>> nodes;
    // Class kSlid types carried from parse, so codegen can emit each class's
    // `<Name>__$sizeof()` helper (GEP-null/ptrtoint — LLVM owns the layout).
    std::vector<widen::TypeRef> classes;
};

}  // namespace ast
