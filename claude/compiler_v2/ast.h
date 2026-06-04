#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ast {

enum class Kind {
    kProgram,
    kFunctionDef,
    kFunctionDecl,
    kVarDeclStmt,
    kAssignStmt,
    kAugAssignStmt,
    kStoreStmt,     // store through an lvalue expr; children[0]=lvalue, [1]=rhs.
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
    kCaseClause,    // children[0]=label const-expr (null=default), [1]=body block.
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
    kCastExpr,      // prefix `<Type^> operand` — pointer reinterpret cast.
                    // inferred_type = target; children[0]=operand. Codegen emits a
                    // ptrtoint/inttoptr only at the intptr boundary; ptr↔ptr is a
                    // no-op (opaque `ptr`).

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
    std::string return_type;
    std::string nominal_type;    // literal nodes: nominal type from constfold
    std::string inferred_type;   // expression nodes: in-context type from classify
    std::string op_type;         // binary's computational type from classify
    int file_id = -1;            // source file of the construct
    int tok = -1;                // index into token::List::tokens for error attribution
    int name_tok = -1;           // ident token for named constructs
    int resolved_entry_id = -1;  // ident / lhs / callee -> parse::Tree::entries index
    int value_index = -1;        // kSeqExpr: which child supplies the result value
    int loop_levels = -1;        // kBreakStmt/kContinueStmt: hops outward in the
                                 // loop/switch context stack to the resolved target
                                 // (0 = innermost), stamped by resolve.
    bool is_const = false;       // kVarDeclStmt: declared with leading `const`
    std::vector<std::unique_ptr<Node>> children;
    std::vector<std::unique_ptr<Node>> params;   // kFunctionDef/Decl: kParam nodes
    std::vector<std::string> param_types;        // kCallStmt/kCallExpr: resolved fn's param types
    std::vector<int> captures;                   // nested fn + its calls: captured host entry ids
    std::vector<std::string> capture_types;      // nested fn: each capture's slids type
};

struct Tree {
    std::vector<std::unique_ptr<Node>> nodes;
};

}  // namespace ast
