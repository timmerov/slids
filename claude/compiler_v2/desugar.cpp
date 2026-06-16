#include "desugar.h"

#include <cassert>
#include <memory>
#include <string>

#include "ast.h"
#include "diagnostic.h"
#include "parse.h"

namespace desugar {

namespace {

ast::Kind toAstKind(parse::Kind k) {
    switch (k) {
        case parse::Kind::kProgram:       return ast::Kind::kProgram;
        case parse::Kind::kFunctionDef:   return ast::Kind::kFunctionDef;
        case parse::Kind::kFunctionDecl:  return ast::Kind::kFunctionDecl;
        case parse::Kind::kVarDeclStmt:   return ast::Kind::kVarDeclStmt;
        case parse::Kind::kAssignStmt:    return ast::Kind::kAssignStmt;
        case parse::Kind::kAugAssignStmt: return ast::Kind::kAugAssignStmt;
        case parse::Kind::kStoreStmt:     return ast::Kind::kStoreStmt;
        case parse::Kind::kMoveStmt:      return ast::Kind::kMoveStmt;
        case parse::Kind::kSwapStmt:      return ast::Kind::kSwapStmt;
        case parse::Kind::kDestructureStmt: return ast::Kind::kDestructureStmt;
        case parse::Kind::kDeleteStmt:    return ast::Kind::kDeleteStmt;
        case parse::Kind::kDtorCallStmt:  return ast::Kind::kDtorCallStmt;
        case parse::Kind::kCallStmt:      return ast::Kind::kCallStmt;
        case parse::Kind::kCallExpr:      return ast::Kind::kCallExpr;
        case parse::Kind::kExprStmt:      return ast::Kind::kExprStmt;
        case parse::Kind::kAliasDecl:
            // Consumed by resolve (types substituted); never copied to the ast.
            assert(false && "toAstKind: alias should be dropped before copy");
            __builtin_unreachable();
        case parse::Kind::kNamespaceDecl:
            // Consumed by resolve; members are hoisted, the wrapper dropped.
            assert(false && "toAstKind: namespace should be dropped before copy");
            __builtin_unreachable();
        case parse::Kind::kClassDef:
            // Consumed by resolve (ClassInfo + the kSlid type); construction
            // lowers at the use site. The node is dropped on copy.
            assert(false && "toAstKind: class should be dropped before copy");
            __builtin_unreachable();
        case parse::Kind::kFieldExpr:
            // Intercepted by copyNode and lowered to a kIndexExpr over the field's
            // slot index; never reaches toAstKind.
            assert(false && "toAstKind: kFieldExpr should be lowered by copyNode");
            __builtin_unreachable();
        case parse::Kind::kMethodCallStmt:
            // Intercepted by copyNode and lowered to a kCallStmt; never reaches here.
            assert(false && "toAstKind: method call should be lowered by copyNode");
            __builtin_unreachable();
        case parse::Kind::kEnumDecl:
            // Consumed by resolve (lowered to alias+namespace+consts / bare
            // consts); members folded by constfold. The node is dropped on copy.
            assert(false && "toAstKind: enum should be dropped before copy");
            __builtin_unreachable();
        case parse::Kind::kReturnStmt:    return ast::Kind::kReturnStmt;
        case parse::Kind::kBlockStmt:     return ast::Kind::kBlockStmt;
        case parse::Kind::kIfStmt:        return ast::Kind::kIfStmt;
        case parse::Kind::kWhileStmt:     return ast::Kind::kWhileStmt;
        case parse::Kind::kDoWhileStmt:   return ast::Kind::kDoWhileStmt;
        case parse::Kind::kForLongStmt:   return ast::Kind::kForLongStmt;
        case parse::Kind::kForRangedStmt:
            // Intercepted by copyNode and lowered to a kForLongStmt; never reaches
            // toAstKind (which has no short-for ast kind to map to).
            assert(false && "toAstKind: kForRangedStmt should be lowered by copyNode");
        case parse::Kind::kForArrayStmt:
            // Intercepted by copyNode and lowered to a kForLongStmt; never reaches
            // toAstKind.
            assert(false && "toAstKind: kForArrayStmt should be lowered by copyNode");
        case parse::Kind::kForTupleStmt:
            // Intercepted by copyNode and lowered to a kForLongStmt; never reaches
            // toAstKind.
            assert(false && "toAstKind: kForTupleStmt should be lowered by copyNode");
        case parse::Kind::kForEnumStmt:
            // Rewritten to a kForLongStmt during resolve; never copied to the ast.
            assert(false && "toAstKind: kForEnumStmt should be lowered in resolve");
            __builtin_unreachable();
        case parse::Kind::kBreakStmt:     return ast::Kind::kBreakStmt;
        case parse::Kind::kContinueStmt:  return ast::Kind::kContinueStmt;
        case parse::Kind::kSwitchStmt:    return ast::Kind::kSwitchStmt;
        case parse::Kind::kCaseClause:    return ast::Kind::kCaseClause;
        case parse::Kind::kStringLiteral: return ast::Kind::kStringLiteral;
        case parse::Kind::kIntLiteral:    return ast::Kind::kIntLiteral;
        case parse::Kind::kUintLiteral:   return ast::Kind::kUintLiteral;
        case parse::Kind::kCharLiteral:   return ast::Kind::kCharLiteral;
        case parse::Kind::kBoolLiteral:   return ast::Kind::kBoolLiteral;
        case parse::Kind::kFloatLiteral:  return ast::Kind::kFloatLiteral;
        case parse::Kind::kNullptrLiteral:return ast::Kind::kNullptrLiteral;
        case parse::Kind::kIdentExpr:     return ast::Kind::kIdentExpr;
        case parse::Kind::kUnaryExpr:     return ast::Kind::kUnaryExpr;
        case parse::Kind::kBinaryExpr:    return ast::Kind::kBinaryExpr;
        case parse::Kind::kPreIncExpr:    return ast::Kind::kPreIncExpr;
        case parse::Kind::kPostIncExpr:   return ast::Kind::kPostIncExpr;
        case parse::Kind::kAddrOfExpr:    return ast::Kind::kAddrOfExpr;
        case parse::Kind::kDerefExpr:     return ast::Kind::kDerefExpr;
        case parse::Kind::kIndexExpr:     return ast::Kind::kIndexExpr;
        case parse::Kind::kTupleExpr:     return ast::Kind::kTupleExpr;
        case parse::Kind::kNewExpr:       return ast::Kind::kNewExpr;
        case parse::Kind::kCastExpr:      return ast::Kind::kCastExpr;
        case parse::Kind::kConvertExpr:   return ast::Kind::kConvertExpr;
        case parse::Kind::kSizeofExpr:
            // Rewritten to a kIntLiteral during classify; never copied to the ast.
            assert(false && "toAstKind: kSizeofExpr should be lowered in classify");
            __builtin_unreachable();
        case parse::Kind::kStringifyType:
            // Rewritten to a kStringLiteral during classify; never copied.
            assert(false && "toAstKind: kStringifyType should be lowered in classify");
            __builtin_unreachable();
        case parse::Kind::kParam:         return ast::Kind::kParam;
    }
    assert(false && "toAstKind: unhandled parse::Kind");
    __builtin_unreachable();
}

// Rewrite `lhs op= rhs;`. Only fires when `node` is a kAugAssignStmt.
// Classify stamped: node.return_type = lvalue type, node.inferred_type = opty,
// node.op_type = the op's compute type, node.text = the op.
//   Bare name   (children [rhs])           -> `lhs = lhs op rhs;` (kAssignStmt).
//   Complex lvalue (children [lvalue, rhs]) -> bind the leaf's ADDRESS once into a
//     hidden reference (so the lvalue's index / side effects evaluate a single
//     time), then read + store through the deref. Reuses the var-decl + deref-
//     store + binary machinery — no new codegen.
std::unique_ptr<ast::Node> tryDesugarAugAssign(ast::Node& node, int& next_id) {
    if (node.kind != ast::Kind::kAugAssignStmt) return nullptr;

    if (node.children.size() == 2) {
        widen::TypeRef leaf = node.return_type;            // the lvalue (leaf) type
        int lv_id = next_id++;
        int file = node.file_id;
        int tok = node.tok;
        // `_$lv` holds the leaf's ADDRESS, computed once. For an index/field
        // target that is `^lvalue` (a reference); for a deref target `ptr^` the
        // address IS `ptr` (don't take `^(ptr^)` — that isn't an addr-of operand).
        ast::Node& lv = *node.children[0];
        std::unique_ptr<ast::Node> addr;
        widen::TypeRef refT;
        if (lv.kind == ast::Kind::kDerefExpr) {
            refT = lv.children[0]->inferred_type;          // the pointer's type
            addr = std::move(lv.children[0]);
        } else {
            refT = widen::internPointer(leaf);             // a reference to the leaf
            addr = std::make_unique<ast::Node>();
            addr->kind = ast::Kind::kAddrOfExpr;
            addr->inferred_type = refT;
            addr->file_id = file; addr->tok = tok;
            addr->children.push_back(std::move(node.children[0]));
        }
        auto derefTmp = [&]() {
            auto id = std::make_unique<ast::Node>();
            id->kind = ast::Kind::kIdentExpr;
            id->name = "_$lv";
            id->resolved_entry_id = lv_id;
            id->inferred_type = refT;
            id->file_id = file; id->tok = tok;
            auto d = std::make_unique<ast::Node>();
            d->kind = ast::Kind::kDerefExpr;
            d->inferred_type = leaf;
            d->file_id = file; d->tok = tok;
            d->children.push_back(std::move(id));
            return d;
        };
        auto decl = std::make_unique<ast::Node>();
        decl->kind = ast::Kind::kVarDeclStmt;
        decl->name = "_$lv";
        decl->resolved_entry_id = lv_id;
        decl->return_type = refT;
        decl->file_id = file; decl->tok = tok; decl->name_tok = tok;
        decl->children.push_back(std::move(addr));
        // `_$lv^ = _$lv^ op rhs;`
        auto binop = std::make_unique<ast::Node>();
        binop->kind = ast::Kind::kBinaryExpr;
        binop->text = node.text;
        binop->inferred_type = node.inferred_type;
        binop->op_type = node.op_type;
        binop->file_id = file; binop->tok = tok;
        binop->children.push_back(derefTmp());
        binop->children.push_back(std::move(node.children[1]));
        auto store = std::make_unique<ast::Node>();
        store->kind = ast::Kind::kStoreStmt;
        store->file_id = file; store->tok = tok;
        store->children.push_back(derefTmp());
        store->children.push_back(std::move(binop));
        auto block = std::make_unique<ast::Node>();
        block->kind = ast::Kind::kBlockStmt;
        block->file_id = file; block->tok = tok;
        block->children.push_back(std::move(decl));
        block->children.push_back(std::move(store));
        return block;
    }

    assert(node.children.size() == 1
        && "tryDesugarAugAssign: AugAssignStmt needs 1 rhs child");

    auto lhs_ref = std::make_unique<ast::Node>();
    lhs_ref->kind = ast::Kind::kIdentExpr;
    lhs_ref->name = node.name;
    lhs_ref->inferred_type = node.return_type;
    lhs_ref->resolved_entry_id = node.resolved_entry_id;
    lhs_ref->file_id = node.file_id;
    lhs_ref->tok = node.tok;

    auto binop = std::make_unique<ast::Node>();
    binop->kind = ast::Kind::kBinaryExpr;
    binop->text = node.text;
    binop->inferred_type = node.inferred_type;
    binop->op_type = node.op_type;
    binop->file_id = node.file_id;
    binop->tok = node.tok;
    binop->children.push_back(std::move(lhs_ref));
    binop->children.push_back(std::move(node.children[0]));

    auto out = std::make_unique<ast::Node>();
    out->kind = ast::Kind::kAssignStmt;
    out->name = std::move(node.name);
    out->resolved_entry_id = node.resolved_entry_id;
    out->file_id = node.file_id;
    out->tok = node.tok;
    out->children.push_back(std::move(binop));
    return out;
}

// The LLVM symbol for a function reference. File-scope functions keep their bare
// name; a namespace member is disambiguated by its entry id (computed identically
// at the definition and every call site from the same resolved_entry_id — never
// stored as a canonical-name string). Two namespaces may both define `foo`; this
// keeps their symbols distinct without a scope-path string.
std::string functionSymbol(parse::Node const& p, parse::Tree const& tree) {
    if (p.resolved_entry_id < 0) return p.name;
    parse::Entry const& e = tree.entries[p.resolved_entry_id];
    if (e.kind != parse::EntryKind::kFunction) return p.name;
    // A namespace member is already disambiguated by its entry id; a NESTED
    // function (registered in a body frame, parent_frame_id != the global frame
    // 0) is lifted to a top-level symbol mangled the same way, so it can't
    // collide with a file-scope name.
    if (e.owner_ns_frame >= 0 || e.parent_frame_id != 0) {
        return e.name + "." + std::to_string(p.resolved_entry_id);
    }
    // A free function is mangled by entry id ONLY when its name is overloaded
    // (2+ free-function entries share it), so a single function — and main —
    // keeps its plain symbol. A call and its chosen definition share the entry
    // id, so they mangle identically.
    int count = 0;
    for (parse::Entry const& q : tree.entries) {
        if (q.kind == parse::EntryKind::kFunction && q.owner_ns_frame < 0
            && q.name == e.name) {
            count++;
        }
    }
    if (count > 1) return e.name + "." + std::to_string(p.resolved_entry_id);
    return p.name;
}

// next_id is a single program-wide counter, seeded by run() at the (frozen)
// parse::Tree::entries size, threaded through every copy so the helper locals a
// lowered short-for mints get globally-unique resolved_entry_ids that never
// collide across two loops.
std::unique_ptr<ast::Node> copyNode(parse::Node const& p, parse::Tree const& tree,
                                    int& next_id);
std::unique_ptr<ast::Node> lowerForRanged(parse::Node const& p,
                                          parse::Tree const& tree, int& next_id);
std::unique_ptr<ast::Node> lowerForArray(parse::Node const& p,
                                         parse::Tree const& tree, int& next_id);
std::unique_ptr<ast::Node> lowerForTuple(parse::Node const& p,
                                         parse::Tree const& tree, int& next_id);
std::unique_ptr<ast::Node> lowerFieldExpr(parse::Node const& p,
                                          parse::Tree const& tree, int& next_id);
std::unique_ptr<ast::Node> lowerMethodCall(parse::Node const& p,
                                           parse::Tree const& tree, int& next_id);

std::unique_ptr<ast::Node> copyNode(parse::Node const& p, parse::Tree const& tree,
                                    int& next_id) {
    // A short ranged / array / tuple for is lowered to the canonical kForLongStmt
    // here (toAstKind has no short-for ast kind); its helper locals get fresh ids.
    if (p.kind == parse::Kind::kForRangedStmt) {
        return lowerForRanged(p, tree, next_id);
    }
    if (p.kind == parse::Kind::kForArrayStmt) {
        return lowerForArray(p, tree, next_id);
    }
    if (p.kind == parse::Kind::kForTupleStmt) {
        return lowerForTuple(p, tree, next_id);
    }
    // `base.field` lowers to a slot index over the class's named tuple — a class
    // is a named tuple, so field access IS tuple-slot access.
    if (p.kind == parse::Kind::kFieldExpr) {
        return lowerFieldExpr(p, tree, next_id);
    }
    // `obj.method(args)` lowers to a normal call of the lifted <Class>__method with
    // the receiver's address prepended as `self`.
    if (p.kind == parse::Kind::kMethodCallStmt) {
        return lowerMethodCall(p, tree, next_id);
    }
    auto node = std::make_unique<ast::Node>();
    node->kind = toAstKind(p.kind);
    node->name = p.name;
    node->text = p.text;
    node->return_type = p.return_type;
    node->nominal_type = p.nominal_type;
    node->inferred_type = p.inferred_type;
    node->op_type = p.op_type;
    node->file_id = p.file_id;
    node->tok = p.tok;
    node->name_tok = p.name_tok;
    node->resolved_entry_id = p.resolved_entry_id;
    node->loop_levels = p.loop_levels;
    node->is_const = p.is_const;
    node->move_init = p.move_init;
    node->non_completing = p.non_completing;
    node->param_types = p.param_types;
    node->captures = p.captures;
    node->capture_types = p.capture_types;
    // Function definitions and calls (including qualified `Space:bar()`) resolve
    // to their entry-id-derived symbol; the qualifier is dropped (ast carries no
    // qualifier — a flat symbol replaces it).
    if (p.kind == parse::Kind::kFunctionDef
     || p.kind == parse::Kind::kFunctionDecl
     || p.kind == parse::Kind::kCallExpr
     || p.kind == parse::Kind::kCallStmt) {
        node->name = functionSymbol(p, tree);
    }
    for (auto const& c : p.children) {
        if (!c) { node->children.push_back(nullptr); continue; }  // preserve a
                          // null slot (a switch default clause's absent label)
        if (c->kind == parse::Kind::kAliasDecl) continue;      // resolve-only
        if (c->kind == parse::Kind::kNamespaceDecl) continue;  // members hoisted
        if (c->kind == parse::Kind::kClassDef) continue;       // resolve-recorded
        if (c->kind == parse::Kind::kEnumDecl) continue;       // resolve-lowered
        node->children.push_back(copyNode(*c, tree, next_id));
    }
    for (auto const& pp : p.params) {
        node->params.push_back(copyNode(*pp, tree, next_id));
    }
    if (auto rewritten = tryDesugarAugAssign(*node, next_id)) return rewritten;
    return node;
}

// Lower a kForRangedStmt to the canonical kForLongStmt. The loop-var decl, end
// and step clauses are copied (already resolved + typed by the earlier stages);
// the `_$end`/`_$step` bound and step locals are synthesized HERE with FRESH
// resolved_entry_ids drawn from next_id (past parse::Tree::entries — those ids
// have no backing entry). The cond and update are built and hand-stamped (the
// tryDesugarAugAssign discipline) because classify never sees them.
//
// INVARIANT: no stage downstream of resolve may index parse::Tree::entries by a
// LOCAL's id — these fresh ids have no entry. Sound today only because codegen
// is node-driven (its SymTab is keyed by id, reading return_type/inferred_type
// off the node) and optimize/layout are stubs. A future pass that does
// entries[node.resolved_entry_id] for a local would deref a non-existent entry.
std::unique_ptr<ast::Node> lowerForRanged(parse::Node const& p,
                                          parse::Tree const& tree, int& next_id) {
    // p.children: [0]=loop-var decl (init=start), [1]=end, [2]=step|null,
    // [3]=body. p.text = cmp, p.name = op.
    parse::Node const& lvp = *p.children[0];
    int lv_id = lvp.resolved_entry_id;
    // The loop var's type drives the bound/step allocas and the cond/update ops.
    // Read it off the entry (correct for a fresh decl AND a reuse, where the decl
    // is a kAssignStmt whose return_type isn't the loop var type). resolve always
    // resolves the loop var, and desugar runs only on classify-clean trees.
    assert(lv_id >= 0 && "lowerForRanged: loop var resolved");
    widen::TypeRef T = tree.entries[lv_id].slids_type;
    std::string vname = lvp.name;
    int file = p.file_id;
    int tok = (p.range_dotdot_tok >= 0) ? p.range_dotdot_tok : p.tok;

    auto ident = [&](std::string const& nm, int id) {
        auto n = std::make_unique<ast::Node>();
        n->kind = ast::Kind::kIdentExpr;
        n->name = nm; n->resolved_entry_id = id; n->inferred_type = T;
        n->file_id = file; n->tok = tok;
        return n;
    };
    auto helperDecl = [&](std::string const& nm, int id,
                          std::unique_ptr<ast::Node> init) {
        auto n = std::make_unique<ast::Node>();
        n->kind = ast::Kind::kVarDeclStmt;
        n->name = nm; n->resolved_entry_id = id; n->return_type = T;
        n->file_id = file; n->tok = tok; n->name_tok = tok;
        n->children.push_back(std::move(init));
        return n;
    };

    // varlist[0]: the loop var (copied — a reuse stays a kAssignStmt / store-only,
    // a fresh decl an alloca-backed kVarDeclStmt; copyNode handles both).
    auto lv = copyNode(lvp, tree, next_id);

    // varlist[1]: `_$end = <end>` — fresh id (evaluate before threading the init).
    int end_id = next_id++;
    auto end_init = copyNode(*p.children[1], tree, next_id);
    auto end_decl = helperDecl("_$end", end_id, std::move(end_init));

    // varlist[2] (optional): `_$step = <step>` — fresh id. A null step means +1.
    int step_id = -1;
    std::unique_ptr<ast::Node> step_decl;
    if (p.children[2]) {
        step_id = next_id++;
        auto step_init = copyNode(*p.children[2], tree, next_id);
        step_decl = helperDecl("_$step", step_id, std::move(step_init));
    }

    // cond: `var cmp _$end` — a bool result computed over the operand type T.
    auto cond = std::make_unique<ast::Node>();
    cond->kind = ast::Kind::kBinaryExpr;
    cond->text = p.text;
    cond->inferred_type = widen::intern("bool");
    cond->op_type = T;
    cond->file_id = file; cond->tok = tok;
    cond->children.push_back(ident(vname, lv_id));
    cond->children.push_back(ident("_$end", end_id));

    // update block: `{ var = var op step }`; step = _$step ident or the literal 1.
    std::unique_ptr<ast::Node> step_val;
    if (step_id >= 0) {
        step_val = ident("_$step", step_id);
    } else {
        step_val = std::make_unique<ast::Node>();
        step_val->kind = ast::Kind::kIntLiteral;
        step_val->text = "1";
        step_val->inferred_type = T;
        step_val->file_id = file; step_val->tok = tok;
    }
    auto bin = std::make_unique<ast::Node>();
    bin->kind = ast::Kind::kBinaryExpr;
    bin->text = p.name;
    bin->inferred_type = T;
    bin->op_type = T;
    bin->file_id = file; bin->tok = tok;
    bin->children.push_back(ident(vname, lv_id));
    bin->children.push_back(std::move(step_val));
    auto assign = std::make_unique<ast::Node>();
    assign->kind = ast::Kind::kAssignStmt;
    assign->name = vname;
    assign->resolved_entry_id = lv_id;
    assign->file_id = file; assign->tok = tok;
    assign->children.push_back(std::move(bin));
    auto update = std::make_unique<ast::Node>();
    update->kind = ast::Kind::kBlockStmt;
    update->file_id = file; update->tok = tok;
    update->children.push_back(std::move(assign));

    auto body = copyNode(*p.children[3], tree, next_id);

    // Assemble: [0]=cond, [1]=update, [2]=body, [3..]=varlist.
    auto out = std::make_unique<ast::Node>();
    out->kind = ast::Kind::kForLongStmt;
    out->file_id = p.file_id; out->tok = p.tok;
    out->children.push_back(std::move(cond));
    out->children.push_back(std::move(update));
    out->children.push_back(std::move(body));
    out->children.push_back(std::move(lv));
    out->children.push_back(std::move(end_decl));
    if (step_decl) out->children.push_back(std::move(step_decl));
    return out;
}

// Lower a kForArrayStmt to the canonical kForLongStmt: a fresh `_$idx` counter
// walks 0..length-1 and the loop var is (re)bound at the TOP of the body each
// iteration — by reference (`T^ v = ^arr[i]`), by value (`T v = arr[i]`), or
// typeless reuse (`v = arr[i]`). resolve already typed the loop var + validated
// the iterable; here we build the counter / index / binding and hand-stamp the
// types classify never saw. See lowerForRanged for the fresh-id INVARIANT.
std::unique_ptr<ast::Node> lowerForArray(parse::Node const& p,
                                         parse::Tree const& tree, int& next_id) {
    // p.children: [0]=loop-var decl (typed in resolve, no init; kind==kAssignStmt
    // flags a typeless REUSE), [1]=array ident, [2]=body.
    parse::Node const& lvp = *p.children[0];
    parse::Node const& arrp = *p.children[1];
    int v_id = lvp.resolved_entry_id;
    int arr_id = arrp.resolved_entry_id;
    widen::TypeRef arr_type = arrp.inferred_type;          // classify stamped it
    widen::TypeRef arrS = widen::strip(arr_type);
    std::vector<int> adims = widen::get(arrS).dims;
    // resolve validated a fixed-size array (>= 1 dim); desugar runs error-free.
    assert(!adims.empty() && "lowerForArray: array has at least one dimension");
    int size = adims.front();                              // the OUTER dim
    // Each element is the (N-1)-D sub-array — one subscript strips the first dim.
    widen::TypeRef elem = (adims.size() <= 1)
        ? widen::get(arrS).elem
        : widen::internArray(widen::get(arrS).elem,
                             std::vector<int>(adims.begin() + 1, adims.end()));
    // By reference iff the loop var is a reference WHOSE POINTEE is the element
    // (a declared `T^ v : T[]`, or the classify forced-by-ref). A typeless var, or
    // a `T^ v` over a POINTER-element array (`int^ v : int^[]`, where the pointee
    // `int` isn't the element `int^`), is by VALUE — it copies the pointer element.
    bool by_ref = lvp.return_type != widen::kNoType
        && widen::form(widen::strip(lvp.return_type)) == widen::Type::Form::kPointer
        && widen::deepStrip(widen::get(widen::strip(lvp.return_type)).pointee)
               == widen::deepStrip(elem);
    bool reuse = (lvp.kind == parse::Kind::kAssignStmt);
    // The binding's declared type: as-written (typed / by-ref) or the element
    // (typeless fresh, off the entry resolve typed — always resolved). Reuse needs
    // no type.
    assert(v_id >= 0 && "lowerForArray: loop var resolved");
    widen::TypeRef bind_type = (lvp.return_type != widen::kNoType)
        ? lvp.return_type
        : tree.entries[v_id].slids_type;

    int afile = arrp.file_id, atok = arrp.tok;
    int vfile = lvp.file_id, vtok = lvp.name_tok;
    widen::TypeRef intptr = widen::intern("intptr");

    auto ident = [&](std::string const& nm, int id, widen::TypeRef ty,
                     int file, int tok) {
        auto n = std::make_unique<ast::Node>();
        n->kind = ast::Kind::kIdentExpr;
        n->name = nm; n->resolved_entry_id = id; n->inferred_type = ty;
        n->file_id = file; n->tok = tok;
        return n;
    };
    auto intLit = [&](std::string const& v) {
        auto n = std::make_unique<ast::Node>();
        n->kind = ast::Kind::kIntLiteral; n->text = v;
        n->inferred_type = intptr; n->file_id = afile; n->tok = atok;
        return n;
    };

    // varlist[0]: intptr _$idx = 0 (fresh id).
    int idx_id = next_id++;
    auto idx_decl = std::make_unique<ast::Node>();
    idx_decl->kind = ast::Kind::kVarDeclStmt;
    idx_decl->name = "_$idx"; idx_decl->resolved_entry_id = idx_id;
    idx_decl->return_type = intptr;
    idx_decl->file_id = afile; idx_decl->tok = atok; idx_decl->name_tok = atok;
    idx_decl->children.push_back(intLit("0"));

    // cond: _$idx < size.
    auto cond = std::make_unique<ast::Node>();
    cond->kind = ast::Kind::kBinaryExpr; cond->text = "<";
    cond->inferred_type = widen::intern("bool"); cond->op_type = intptr;
    cond->file_id = afile; cond->tok = atok;
    cond->children.push_back(ident("_$idx", idx_id, intptr, afile, atok));
    cond->children.push_back(intLit(std::to_string(size)));

    // update: { _$idx = _$idx + 1 }.
    auto inc = std::make_unique<ast::Node>();
    inc->kind = ast::Kind::kBinaryExpr; inc->text = "+";
    inc->inferred_type = intptr; inc->op_type = intptr;
    inc->file_id = afile; inc->tok = atok;
    inc->children.push_back(ident("_$idx", idx_id, intptr, afile, atok));
    inc->children.push_back(intLit("1"));
    auto upd_assign = std::make_unique<ast::Node>();
    upd_assign->kind = ast::Kind::kAssignStmt;
    upd_assign->name = "_$idx"; upd_assign->resolved_entry_id = idx_id;
    upd_assign->file_id = afile; upd_assign->tok = atok;
    upd_assign->children.push_back(std::move(inc));
    auto update = std::make_unique<ast::Node>();
    update->kind = ast::Kind::kBlockStmt;
    update->file_id = afile; update->tok = atok;
    update->children.push_back(std::move(upd_assign));

    // arr[_$idx]. The base ident keeps the array's position; the kIndexExpr takes
    // the LOOP VARIABLE's position so an element/width error carets at the loop var.
    auto index_expr = std::make_unique<ast::Node>();
    index_expr->kind = ast::Kind::kIndexExpr;
    index_expr->inferred_type = elem;
    index_expr->file_id = vfile; index_expr->tok = vtok;
    index_expr->children.push_back(ident(arrp.name, arr_id, arr_type, afile, atok));
    index_expr->children.push_back(ident("_$idx", idx_id, intptr, afile, atok));

    // binding init: by-ref takes the element address (iterator demoting to ref),
    // otherwise the element value.
    std::unique_ptr<ast::Node> init;
    if (by_ref) {
        init = std::make_unique<ast::Node>();
        init->kind = ast::Kind::kAddrOfExpr;
        init->inferred_type = bind_type;          // T^
        init->file_id = afile; init->tok = atok;
        init->children.push_back(std::move(index_expr));
    } else {
        init = std::move(index_expr);
    }
    std::unique_ptr<ast::Node> binding;
    if (reuse) {
        binding = std::make_unique<ast::Node>();
        binding->kind = ast::Kind::kAssignStmt;
        binding->name = lvp.name; binding->resolved_entry_id = v_id;
        binding->file_id = vfile; binding->tok = vtok;
        binding->children.push_back(std::move(init));
    } else {
        binding = std::make_unique<ast::Node>();
        binding->kind = ast::Kind::kVarDeclStmt;
        binding->name = lvp.name; binding->resolved_entry_id = v_id;
        binding->return_type = bind_type;
        binding->file_id = vfile; binding->tok = vtok; binding->name_tok = vtok;
        binding->children.push_back(std::move(init));
    }

    // body: { binding; <user body statements> }.
    auto body = std::make_unique<ast::Node>();
    body->kind = ast::Kind::kBlockStmt;
    body->file_id = p.children[2]->file_id; body->tok = p.children[2]->tok;
    body->children.push_back(std::move(binding));
    auto user_body = copyNode(*p.children[2], tree, next_id);
    for (auto& st : user_body->children) body->children.push_back(std::move(st));

    // Assemble: [0]=cond, [1]=update, [2]=body, [3]=_$idx varlist.
    auto out = std::make_unique<ast::Node>();
    out->kind = ast::Kind::kForLongStmt;
    out->file_id = p.file_id; out->tok = p.tok;
    out->children.push_back(std::move(cond));
    out->children.push_back(std::move(update));
    out->children.push_back(std::move(body));
    out->children.push_back(std::move(idx_decl));
    return out;
}

// Lower a kForTupleStmt to the canonical kForLongStmt. A `_$idx` counter bounds
// the loop (< N slots) and a `_$iter` iterator WALKS the slots; the loop var is
// (re)bound from `_$iter` each iteration — by value `v = _$iter^`, by ref
// `v = _$iter` (iterator -> reference). A literal / rvalue iterable is SPILLED
// to a fresh `_$ftmp`; a var / `ref^` / index lvalue iterates in place. The
// iterator is `<T[]><void^>(storage address)`: for `ref^` the address is the
// pointer ITSELF (no addr-of-through-deref), and the void^ bridge keeps both
// casts buffer-class-legal. resolve typed the loop var + validated homogeneity;
// the synthesized nodes are hand-stamped here. See lowerForRanged for the
// fresh-id INVARIANT.
std::unique_ptr<ast::Node> lowerForTuple(parse::Node const& p,
                                         parse::Tree const& tree, int& next_id) {
    // p.children: [0]=loop-var decl (kind==kAssignStmt flags a typeless REUSE),
    // [1]=iterable expr, [2]=body.
    parse::Node const& lvp = *p.children[0];
    parse::Node const& itp = *p.children[1];
    int v_id = lvp.resolved_entry_id;
    bool reuse = (lvp.kind == parse::Kind::kAssignStmt);
    // In place iff the iterable is an lvalue (var / `ref^` / index); everything
    // else (a literal, a call, a computed tuple) has no storage -> spill.
    bool spill = !(itp.kind == parse::Kind::kIdentExpr
                || itp.kind == parse::Kind::kDerefExpr
                || itp.kind == parse::Kind::kIndexExpr);

    widen::TypeRef tup_type = itp.inferred_type;
    widen::TypeRef itS = widen::strip(tup_type);
    // resolve/classify validated a (homogeneous) tuple (>= 2 slots) or an array;
    // desugar runs only on classify-clean trees. An array is N copies of the
    // (N-1)-D sub-array (or base element for 1-D).
    int N; widen::TypeRef T;
    if (widen::form(itS) == widen::Type::Form::kArray) {
        widen::Type const& at = widen::get(itS);
        N = at.dims.front();
        T = (at.dims.size() <= 1) ? at.elem
            : widen::internArray(at.elem,
                std::vector<int>(at.dims.begin() + 1, at.dims.end()));
    } else {
        std::vector<widen::TypeRef> slots = widen::get(itS).slots;
        assert(!slots.empty() && "lowerForTuple: iterable is a tuple or array");
        N = static_cast<int>(slots.size());
        T = slots[0];                                     // homogeneous element
    }
    widen::TypeRef iter_type = widen::internIterator(T);  // T[]
    // The binding's declared type: as-written (typed / by-ref) or the element
    // (typeless fresh, off the entry — classify forces a reference for a
    // non-primitive element; the loop var is always resolved). Reuse needs no type.
    assert(v_id >= 0 && "lowerForTuple: loop var resolved");
    widen::TypeRef bind_type = (lvp.return_type != widen::kNoType)
        ? lvp.return_type
        : tree.entries[v_id].slids_type;
    // By reference iff the loop var's resolved type is a reference to the element T:
    // a declared `T^`, or the classify forced-by-ref over a non-primitive element. A
    // by-VALUE loop over POINTER elements keeps bind_type == T (not T^), so it stays
    // by value — distinguishing it from a `T^` reference whose pointee IS T.
    bool by_ref = widen::form(widen::strip(bind_type)) == widen::Type::Form::kPointer
        && widen::deepStrip(widen::get(widen::strip(bind_type)).pointee)
               == widen::deepStrip(T);

    int ifile = itp.file_id, itok = itp.tok;
    int vfile = lvp.file_id, vtok = lvp.name_tok;
    widen::TypeRef intptr = widen::intern("intptr");

    auto ident = [&](std::string const& nm, int id, widen::TypeRef ty,
                     int file, int tok) {
        auto n = std::make_unique<ast::Node>();
        n->kind = ast::Kind::kIdentExpr;
        n->name = nm; n->resolved_entry_id = id; n->inferred_type = ty;
        n->file_id = file; n->tok = tok;
        return n;
    };
    auto intLit = [&](std::string const& v) {
        auto n = std::make_unique<ast::Node>();
        n->kind = ast::Kind::kIntLiteral; n->text = v;
        n->inferred_type = intptr; n->file_id = ifile; n->tok = itok;
        return n;
    };

    std::vector<std::unique_ptr<ast::Node>> varlist;

    // [spill] materialize the rvalue tuple into a fresh `_$ftmp` (so it has an
    // address). Must precede the iterator (which addresses it). varlist[0].
    int ftmp_id = -1;
    if (spill) {
        ftmp_id = next_id++;
        auto ftmp = std::make_unique<ast::Node>();
        ftmp->kind = ast::Kind::kVarDeclStmt;
        ftmp->name = "_$ftmp"; ftmp->resolved_entry_id = ftmp_id;
        ftmp->return_type = tup_type;
        ftmp->file_id = ifile; ftmp->tok = itok; ftmp->name_tok = itok;
        ftmp->children.push_back(copyNode(itp, tree, next_id));
        varlist.push_back(std::move(ftmp));
    }

    // intptr _$idx = 0
    int idx_id = next_id++;
    auto idx_decl = std::make_unique<ast::Node>();
    idx_decl->kind = ast::Kind::kVarDeclStmt;
    idx_decl->name = "_$idx"; idx_decl->resolved_entry_id = idx_id;
    idx_decl->return_type = intptr;
    idx_decl->file_id = ifile; idx_decl->tok = itok; idx_decl->name_tok = itok;
    idx_decl->children.push_back(intLit("0"));

    // _$iter = <T[]> <void^> (storage address). The base address is the start of
    // the tuple (== slot 0): spill / in-place var / index -> ^storage; in-place
    // `ref^` -> the ref pointer itself (its VALUE is the address — the dodge).
    std::unique_ptr<ast::Node> base_addr;
    if (!spill && itp.kind == parse::Kind::kDerefExpr) {
        base_addr = copyNode(*itp.children[0], tree, next_id);
    } else {
        base_addr = std::make_unique<ast::Node>();
        base_addr->kind = ast::Kind::kAddrOfExpr;
        base_addr->inferred_type = widen::internPointer(tup_type);
        base_addr->file_id = ifile; base_addr->tok = itok;
        if (spill) base_addr->children.push_back(
            ident("_$ftmp", ftmp_id, tup_type, ifile, itok));
        else base_addr->children.push_back(copyNode(itp, tree, next_id));
    }
    auto to_void = std::make_unique<ast::Node>();
    to_void->kind = ast::Kind::kCastExpr;
    to_void->inferred_type = widen::intern("void^");
    to_void->file_id = ifile; to_void->tok = itok;
    to_void->children.push_back(std::move(base_addr));
    auto to_iter = std::make_unique<ast::Node>();
    to_iter->kind = ast::Kind::kCastExpr;
    to_iter->inferred_type = iter_type;
    to_iter->file_id = ifile; to_iter->tok = itok;
    to_iter->children.push_back(std::move(to_void));
    int iter_id = next_id++;
    auto iter_decl = std::make_unique<ast::Node>();
    iter_decl->kind = ast::Kind::kVarDeclStmt;
    iter_decl->name = "_$iter"; iter_decl->resolved_entry_id = iter_id;
    iter_decl->return_type = iter_type;
    iter_decl->file_id = ifile; iter_decl->tok = itok; iter_decl->name_tok = itok;
    iter_decl->children.push_back(std::move(to_iter));

    varlist.push_back(std::move(idx_decl));
    varlist.push_back(std::move(iter_decl));

    // cond: _$idx < N
    auto cond = std::make_unique<ast::Node>();
    cond->kind = ast::Kind::kBinaryExpr; cond->text = "<";
    cond->inferred_type = widen::intern("bool"); cond->op_type = intptr;
    cond->file_id = ifile; cond->tok = itok;
    cond->children.push_back(ident("_$idx", idx_id, intptr, ifile, itok));
    cond->children.push_back(intLit(std::to_string(N)));

    // update: { _$idx = _$idx + 1; _$iter = _$iter + 1 } (the iter += 1 is a GEP).
    auto mkbump = [&](std::string const& nm, int id, widen::TypeRef ty) {
        auto bin = std::make_unique<ast::Node>();
        bin->kind = ast::Kind::kBinaryExpr; bin->text = "+";
        bin->inferred_type = ty; bin->op_type = ty;
        bin->file_id = ifile; bin->tok = itok;
        bin->children.push_back(ident(nm, id, ty, ifile, itok));
        bin->children.push_back(intLit("1"));
        auto a = std::make_unique<ast::Node>();
        a->kind = ast::Kind::kAssignStmt; a->name = nm; a->resolved_entry_id = id;
        a->file_id = ifile; a->tok = itok;
        a->children.push_back(std::move(bin));
        return a;
    };
    auto update = std::make_unique<ast::Node>();
    update->kind = ast::Kind::kBlockStmt;
    update->file_id = ifile; update->tok = itok;
    update->children.push_back(mkbump("_$idx", idx_id, intptr));
    update->children.push_back(mkbump("_$iter", iter_id, iter_type));

    // binding: by value `v = _$iter^`, by ref `v = _$iter` (iterator -> reference).
    std::unique_ptr<ast::Node> init;
    if (by_ref) {
        init = ident("_$iter", iter_id, iter_type, vfile, vtok);
    } else {
        init = std::make_unique<ast::Node>();
        init->kind = ast::Kind::kDerefExpr;
        init->inferred_type = T;
        init->file_id = vfile; init->tok = vtok;
        init->children.push_back(ident("_$iter", iter_id, iter_type, vfile, vtok));
    }
    std::unique_ptr<ast::Node> binding;
    if (reuse) {
        binding = std::make_unique<ast::Node>();
        binding->kind = ast::Kind::kAssignStmt;
        binding->name = lvp.name; binding->resolved_entry_id = v_id;
        binding->file_id = vfile; binding->tok = vtok;
        binding->children.push_back(std::move(init));
    } else {
        binding = std::make_unique<ast::Node>();
        binding->kind = ast::Kind::kVarDeclStmt;
        binding->name = lvp.name; binding->resolved_entry_id = v_id;
        binding->return_type = bind_type;
        binding->file_id = vfile; binding->tok = vtok; binding->name_tok = vtok;
        binding->children.push_back(std::move(init));
    }

    // body: { binding; <user body statements> }
    auto body = std::make_unique<ast::Node>();
    body->kind = ast::Kind::kBlockStmt;
    body->file_id = p.children[2]->file_id; body->tok = p.children[2]->tok;
    body->children.push_back(std::move(binding));
    auto user_body = copyNode(*p.children[2], tree, next_id);
    for (auto& st : user_body->children) body->children.push_back(std::move(st));

    // Assemble: [0]=cond, [1]=update, [2]=body, [3..]=varlist.
    auto out = std::make_unique<ast::Node>();
    out->kind = ast::Kind::kForLongStmt;
    out->file_id = p.file_id; out->tok = p.tok;
    out->children.push_back(std::move(cond));
    out->children.push_back(std::move(update));
    out->children.push_back(std::move(body));
    for (auto& v : varlist) out->children.push_back(std::move(v));
    return out;
}

// Collect every class definition reachable from `node` — at file scope and
// nested in function bodies (a local class). Recurses uniformly so a class in a
// nested function (or in a ctor/dtor body) is found too.
void collectClassDefs(parse::Node const& node,
                      std::vector<parse::Node const*>& acc) {
    for (auto const& c : node.children) {
        if (!c) continue;
        if (c->kind == parse::Kind::kClassDef) acc.push_back(c.get());
        collectClassDefs(*c, acc);
    }
}

// Collect a namespace's member function definitions (recursing into nested
// namespaces) so they can be hoisted to program scope.
void collectNamespaceFunctions(parse::Node const& ns,
                               std::vector<parse::Node const*>& fns) {
    for (auto const& m : ns.children) {
        if (!m) continue;
        if (m->kind == parse::Kind::kFunctionDef) {
            fns.push_back(m.get());
        } else if (m->kind == parse::Kind::kNamespaceDecl) {
            collectNamespaceFunctions(*m, fns);
        }
    }
}

// Find the outermost namespace decls reachable from `node`: at file scope and
// inside function bodies (a namespace may be opened in either). Nesting is left
// to collectNamespaceFunctions, so this does not descend into namespaces.
void findTopNamespaces(parse::Node const& node,
                       std::vector<parse::Node const*>& out) {
    for (auto const& c : node.children) {
        if (!c) continue;
        if (c->kind == parse::Kind::kNamespaceDecl) {
            out.push_back(c.get());
        } else if (c->kind == parse::Kind::kFunctionDef) {
            findTopNamespaces(*c, out);
        }
    }
}

// ---- PPID lowering -------------------------------------------------------
// ++/-- are extracted from the phrase they sit in: pre-bumps fire at the
// phrase start, post-bumps at its end. Each inc/dec becomes a read of its
// operand, and the bump is emitted as a kBumpExpr. A phrase carrying bumps is
// wrapped in a kSeqExpr {pre... value post...} (value_index = #pre), so a
// conditionally-evaluated phrase (the rhs of && / ||) keeps its bumps inside
// the subtree that only runs when that side is reached.

std::unique_ptr<ast::Node> makeBump(ast::Node const& operand,
                                    std::string const& op) {
    auto b = std::make_unique<ast::Node>();
    b->kind = ast::Kind::kBumpExpr;
    b->text = op;
    b->resolved_entry_id = operand.resolved_entry_id;
    b->inferred_type = operand.inferred_type;
    b->file_id = operand.file_id;
    b->tok = operand.tok;
    return b;
}

void lowerPhraseSlot(std::unique_ptr<ast::Node>& slot);

// Walk an expression that belongs to the CURRENT phrase: collect its bumps into
// pre/post, replace each inc/dec with a read of the operand. Sub-phrase
// children — call args and the rhs of && / || — recurse via lowerPhraseSlot.
void lowerInPhrase(std::unique_ptr<ast::Node>& slot,
                   std::vector<std::unique_ptr<ast::Node>>& pre,
                   std::vector<std::unique_ptr<ast::Node>>& post) {
    if (!slot) return;
    ast::Node& n = *slot;
    if (n.kind == ast::Kind::kPreIncExpr || n.kind == ast::Kind::kPostIncExpr) {
        auto operand = std::move(n.children[0]);   // ident lvalue (resolve checked)
        auto bump = makeBump(*operand, n.text);
        if (n.kind == ast::Kind::kPreIncExpr) pre.push_back(std::move(bump));
        else                                  post.push_back(std::move(bump));
        slot = std::move(operand);                 // the read replaces the inc/dec
        return;
    }
    if (n.kind == ast::Kind::kCallExpr) {
        for (auto& arg : n.children) lowerPhraseSlot(arg);
        return;
    }
    if (n.kind == ast::Kind::kTupleExpr) {
        // Each comma slot of a tuple literal is its OWN phrase (like a call arg),
        // evaluated left to right — so its bumps stay inside that slot's seq.
        for (auto& el : n.children) lowerPhraseSlot(el);
        return;
    }
    if (n.kind == ast::Kind::kBinaryExpr && (n.text == "&&" || n.text == "||")) {
        lowerInPhrase(n.children[0], pre, post);   // lhs: same phrase
        lowerPhraseSlot(n.children[1]);            // rhs: own conditional phrase
        return;
    }
    // Every other interior node (arith/bitwise/^^/unary) is part of this
    // phrase; leaves have no children, so the loop is a no-op for them.
    // seq/bump are synthesized by this pass and must never be re-walked.
    assert(n.kind != ast::Kind::kSeqExpr && n.kind != ast::Kind::kBumpExpr
        && "lowerInPhrase: re-walked a synthesized PPID node");
    for (auto& c : n.children) lowerInPhrase(c, pre, post);
}

// Lower the phrase rooted at `slot`; wrap it in a seq iff it carried bumps.
void lowerPhraseSlot(std::unique_ptr<ast::Node>& slot) {
    if (!slot) return;
    std::vector<std::unique_ptr<ast::Node>> pre, post;
    lowerInPhrase(slot, pre, post);
    if (pre.empty() && post.empty()) return;
    auto seq = std::make_unique<ast::Node>();
    seq->kind = ast::Kind::kSeqExpr;
    seq->inferred_type = slot->inferred_type;
    seq->file_id = slot->file_id;
    seq->tok = slot->tok;
    seq->value_index = static_cast<int>(pre.size());
    for (auto& b : pre) seq->children.push_back(std::move(b));
    seq->children.push_back(std::move(slot));
    for (auto& b : post) seq->children.push_back(std::move(b));
    slot = std::move(seq);
}

std::unique_ptr<ast::Node> makeBumpStmt(std::unique_ptr<ast::Node> bump) {
    auto stmt = std::make_unique<ast::Node>();
    stmt->kind = ast::Kind::kExprStmt;
    stmt->file_id = bump->file_id;
    stmt->tok = bump->tok;
    stmt->children.push_back(std::move(bump));
    return stmt;
}

// Lower one statement's phrases. The statement itself is a phrase, so its
// statement-level bumps are returned in pre/post for the caller to splice as
// sibling statements around it — a post-inc of the store target must fire
// AFTER the store (`a = a++` leaves a one larger), not inside the rhs. Bumps
// inside a sub-phrase (call args, the rhs of && / ||) stay in a seq, since that
// phrase genuinely exits before the store. A return embeds its bump: there is
// no slot after a terminator, and its value is read before the bump anyway.
void lowerStatementPPID(ast::Node& stmt,
                        std::vector<std::unique_ptr<ast::Node>>& pre,
                        std::vector<std::unique_ptr<ast::Node>>& post) {
    switch (stmt.kind) {
        // The statement IS the phrase: its direct expression operands carry
        // statement-level bumps (pre before, post after the statement).
        case ast::Kind::kVarDeclStmt:
        case ast::Kind::kAssignStmt:
            // children[0] = the rhs (the lvalue is a bare name, no ppid).
            if (!stmt.children.empty() && stmt.children[0]) {
                lowerInPhrase(stmt.children[0], pre, post);
            }
            return;
        case ast::Kind::kStoreStmt:
        case ast::Kind::kMoveStmt:
        case ast::Kind::kSwapStmt:
            // store: [0]=lvalue, [1]=rhs; move: [0]=lhs, [1]=rhs; swap: [0],[1] both
            // lvalues. Each is a direct operand of the statement phrase, so a bump
            // lifts off it and the post fires AFTER the store/move/swap — e.g.
            // `x++ <--> y++` -> `x <--> y; x++; y++`, `arr[k++] = v` -> `arr[k]=v; k++`.
            for (auto& ch : stmt.children) {
                if (ch) lowerInPhrase(ch, pre, post);
            }
            return;
        case ast::Kind::kDestructureStmt:
            // children[0] = the rhs tuple (its slots are sub-phrases via the
            // kTupleExpr arm); [1..] = plain target lvalues (a complex target with
            // ppid is the deferred complex-lhs case).
            if (!stmt.children.empty() && stmt.children[0]) {
                lowerInPhrase(stmt.children[0], pre, post);
            }
            return;
        case ast::Kind::kReturnStmt:
            if (!stmt.children.empty() && stmt.children[0]) {
                lowerPhraseSlot(stmt.children[0]);
            }
            return;
        case ast::Kind::kCallStmt:
            for (auto& arg : stmt.children) lowerPhraseSlot(arg);
            return;
        case ast::Kind::kExprStmt: {
            // The value is discarded. A bare inc/dec needs only its bump — no read.
            auto& child = stmt.children[0];
            if (child->kind == ast::Kind::kPreIncExpr
                || child->kind == ast::Kind::kPostIncExpr) {
                child = makeBump(*child->children[0], child->text);
            } else {
                lowerPhraseSlot(child);
            }
            return;
        }
        // Leaf statements with no liftable ppid operand.
        case ast::Kind::kDeleteStmt:
        case ast::Kind::kDtorCallStmt:
        case ast::Kind::kBreakStmt:
        case ast::Kind::kContinueStmt:
        case ast::Kind::kFunctionDef:
        case ast::Kind::kFunctionDecl:
            return;
        // Not reachable here: compound statements are dispatched by
        // lowerStatementList; kAugAssignStmt is lowered to kAssignStmt before this
        // pass; expression kinds are never a statement. Asserted for exhaustiveness
        // so a future statement kind cannot silently skip PPID lowering.
        case ast::Kind::kProgram:
        case ast::Kind::kAugAssignStmt:
        case ast::Kind::kBlockStmt:
        case ast::Kind::kIfStmt:
        case ast::Kind::kWhileStmt:
        case ast::Kind::kDoWhileStmt:
        case ast::Kind::kForLongStmt:
        case ast::Kind::kSwitchStmt:
        case ast::Kind::kCaseClause:
        case ast::Kind::kCallExpr:
        case ast::Kind::kStringLiteral:
        case ast::Kind::kIntLiteral:
        case ast::Kind::kUintLiteral:
        case ast::Kind::kCharLiteral:
        case ast::Kind::kBoolLiteral:
        case ast::Kind::kFloatLiteral:
        case ast::Kind::kNullptrLiteral:
        case ast::Kind::kIdentExpr:
        case ast::Kind::kUnaryExpr:
        case ast::Kind::kBinaryExpr:
        case ast::Kind::kPreIncExpr:
        case ast::Kind::kPostIncExpr:
        case ast::Kind::kAddrOfExpr:
        case ast::Kind::kDerefExpr:
        case ast::Kind::kIndexExpr:
        case ast::Kind::kTupleExpr:
        case ast::Kind::kNewExpr:
        case ast::Kind::kCastExpr:
        case ast::Kind::kConvertExpr:
        case ast::Kind::kSeqExpr:
        case ast::Kind::kBumpExpr:
        case ast::Kind::kParam:
            assert(false && "lowerStatementPPID: not a lowerable statement kind");
            return;
    }
}

void lowerStatementList(std::vector<std::unique_ptr<ast::Node>>& stmts);

// Lower an if-statement's PPID. The condition is a self-contained phrase: its
// bumps stay inside a seq and fire as the condition is evaluated, before the
// branch (like a call argument). The then/else branches are statement lists;
// an `else if` chain is a nested kIfStmt, recursed structurally.
void lowerIfStmt(ast::Node& s) {
    if (!s.children.empty() && s.children[0]) lowerPhraseSlot(s.children[0]);
    if (s.children.size() > 1 && s.children[1]) {
        lowerStatementList(s.children[1]->children);   // then-block
    }
    if (s.children.size() > 2 && s.children[2]) {
        ast::Node& else_branch = *s.children[2];
        if (else_branch.kind == ast::Kind::kIfStmt) lowerIfStmt(else_branch);
        else lowerStatementList(else_branch.children);  // else-block
    }
}

// Lower a while / do-while loop's PPID. The condition is a self-contained phrase
// whose bumps fire each time it is tested (re-evaluated per iteration); the body
// is a statement list. Both kinds share the child layout ([cond, body]), so this
// serves kWhileStmt and kDoWhileStmt alike.
void lowerWhileStmt(ast::Node& s) {
    if (!s.children.empty() && s.children[0]) lowerPhraseSlot(s.children[0]);
    if (s.children.size() > 1 && s.children[1]) {
        lowerStatementList(s.children[1]->children);   // body block
    }
}

// Lower a long-for's PPID. children[0]=cond, [1]=update, [2]=body, [3..]=varlist.
// The varlist initializers run once (lower each as a self-contained phrase); the
// condition is a phrase re-tested each iteration; update + body are statement
// lists.
void lowerForLong(ast::Node& s) {
    for (std::size_t i = 3; i < s.children.size(); i++) {
        if (s.children[i] && !s.children[i]->children.empty()
            && s.children[i]->children[0]) {
            lowerPhraseSlot(s.children[i]->children[0]);   // varlist init
        }
    }
    if (!s.children.empty() && s.children[0]) lowerPhraseSlot(s.children[0]);   // cond
    if (s.children.size() > 1 && s.children[1]) {
        lowerStatementList(s.children[1]->children);   // update block
    }
    if (s.children.size() > 2 && s.children[2]) {
        lowerStatementList(s.children[2]->children);   // body block
    }
}

// Lower a switch's PPID. children[0] = scrutinee (a self-contained phrase whose
// bumps fire once as it is evaluated); [1..] = kCaseClause, each [0] = label
// (a folded constant — no bumps) and [1] = body statement list.
void lowerSwitchStmt(ast::Node& s) {
    if (!s.children.empty() && s.children[0]) lowerPhraseSlot(s.children[0]);
    for (std::size_t i = 1; i < s.children.size(); i++) {
        if (!s.children[i]) continue;
        ast::Node& clause = *s.children[i];
        if (clause.children.size() > 1 && clause.children[1]) {
            lowerStatementList(clause.children[1]->children);   // body block
        }
    }
}

// Run PPID statement-bump splicing over a statement list, rebuilding it with
// pre-bumps before / post-bumps after each statement. Recurses into a nested
// kBlockStmt / kIfStmt so bumps inside them splice within that scope, not at the
// enclosing one.
void lowerStatementList(std::vector<std::unique_ptr<ast::Node>>& stmts) {
    std::vector<std::unique_ptr<ast::Node>> lowered;
    for (auto& stmt : stmts) {
        if (!stmt) continue;
        if (stmt->kind == ast::Kind::kBlockStmt) {
            lowerStatementList(stmt->children);
            lowered.push_back(std::move(stmt));
            continue;
        }
        if (stmt->kind == ast::Kind::kIfStmt) {
            lowerIfStmt(*stmt);
            lowered.push_back(std::move(stmt));
            continue;
        }
        if (stmt->kind == ast::Kind::kWhileStmt
            || stmt->kind == ast::Kind::kDoWhileStmt) {
            lowerWhileStmt(*stmt);
            lowered.push_back(std::move(stmt));
            continue;
        }
        if (stmt->kind == ast::Kind::kForLongStmt) {
            lowerForLong(*stmt);
            lowered.push_back(std::move(stmt));
            continue;
        }
        if (stmt->kind == ast::Kind::kSwitchStmt) {
            lowerSwitchStmt(*stmt);
            lowered.push_back(std::move(stmt));
            continue;
        }
        std::vector<std::unique_ptr<ast::Node>> pre, post;
        lowerStatementPPID(*stmt, pre, post);
        for (auto& b : pre) lowered.push_back(makeBumpStmt(std::move(b)));
        lowered.push_back(std::move(stmt));
        for (auto& b : post) lowered.push_back(makeBumpStmt(std::move(b)));
    }
    stmts = std::move(lowered);
}

// Lower `base.field` to `base[slot]` — a class is a named tuple, so a field is a
// slot read by its declaration index. The base keeps its kSlid type; codegen
// handles a kSlid base in the index path exactly like a tuple slot.
std::unique_ptr<ast::Node> lowerFieldExpr(parse::Node const& p,
                                          parse::Tree const& tree, int& next_id) {
    assert(!p.children.empty() && p.children[0] && "kFieldExpr needs a base");
    parse::Node const& base = *p.children[0];
    // classify validated the base is a class and the field exists, so these hold
    // (no silent index-0 fallback for an unresolved field).
    widen::TypeRef bt = widen::strip(base.inferred_type);
    assert(widen::form(bt) == widen::Type::Form::kSlid
        && "lowerFieldExpr: base must be a class");
    auto it = tree.classes.find(bt);
    assert(it != tree.classes.end() && "lowerFieldExpr: class is registered");
    int idx = it->second.fieldIndex(p.name);
    assert(idx >= 0 && "lowerFieldExpr: field resolved by classify");
    auto node = std::make_unique<ast::Node>();
    node->kind = ast::Kind::kIndexExpr;
    node->inferred_type = p.inferred_type;   // the field type (classify stamped)
    node->file_id = p.file_id;
    node->tok = p.tok;
    node->children.push_back(copyNode(base, tree, next_id));
    auto idxn = std::make_unique<ast::Node>();
    idxn->kind = ast::Kind::kIntLiteral;
    idxn->text = std::to_string(idx);
    idxn->inferred_type = widen::intern("int");
    idxn->nominal_type = widen::intern("int");
    idxn->file_id = p.file_id;
    idxn->tok = p.tok;
    node->children.push_back(std::move(idxn));
    return node;
}

// Lower `obj.method(args)` to a normal call of the lifted <Class>__method, with the
// receiver's ADDRESS prepended as the implicit `self`. children[0] = receiver,
// children[1..] = args (classify validated the method + arity). The symbol matches
// the lift's `classSymbol(class) + "__" + method`.
std::unique_ptr<ast::Node> lowerMethodCall(parse::Node const& p,
                                           parse::Tree const& tree, int& next_id) {
    assert(!p.children.empty() && p.children[0] && "method call needs a receiver");
    parse::Node const& recv = *p.children[0];
    widen::TypeRef cls = widen::strip(recv.inferred_type);   // receiver's class (self type)
    assert(widen::form(cls) == widen::Type::Form::kSlid
        && "lowerMethodCall: receiver must be a class");

    // The symbol is minted from the method's DEFINING class (its owner frame's
    // class handle, via the threaded entry id), NOT the receiver's type — so an
    // inherited method called on a derived receiver names the base's symbol. They
    // coincide without inheritance; the fallback keeps a stray null id safe.
    widen::TypeRef defCls = cls;
    if (p.resolved_entry_id >= 0) {
        int cid = parse::classEntryForFrame(
            tree, tree.entries[p.resolved_entry_id].owner_ns_frame);
        if (cid >= 0) defCls = widen::strip(tree.entries[cid].slids_type);
    }

    auto call = std::make_unique<ast::Node>();
    call->kind = ast::Kind::kCallStmt;
    call->name = widen::classSymbol(defCls) + "__" + p.name;
    call->return_type = p.return_type;   // emitCall reads return_type
    call->param_types = p.param_types;   // [self, user...] — classify cached it
    call->file_id = p.file_id;
    call->tok = p.tok;
    call->name_tok = p.name_tok;

    // `_$recv` = the receiver's ADDRESS (param-0). For `ptr^.m()` the receiver is a
    // deref, whose object address IS the pointer operand — pass it directly (an
    // addr-of of a deref isn't a codegen lvalue). Otherwise `^receiver` (a variable's
    // alloca, or a field/element GEP — a field lowers to an index, addr-of handles).
    std::unique_ptr<ast::Node> recv_arg;
    if (recv.kind == parse::Kind::kDerefExpr && !recv.children.empty()
        && recv.children[0]) {
        recv_arg = copyNode(*recv.children[0], tree, next_id);   // the pointer itself
    } else {
        recv_arg = std::make_unique<ast::Node>();
        recv_arg->kind = ast::Kind::kAddrOfExpr;
        recv_arg->inferred_type = widen::internPointer(cls);
        recv_arg->file_id = recv.file_id;
        recv_arg->tok = recv.tok;
        recv_arg->children.push_back(copyNode(recv, tree, next_id));
    }
    call->children.push_back(std::move(recv_arg));

    for (std::size_t i = 1; i < p.children.size(); i++)
        if (p.children[i])
            call->children.push_back(copyNode(*p.children[i], tree, next_id));
    return call;
}

}  // namespace

void run(parse::Tree const& in, ast::Tree& out, diagnostic::Sink& diag) {
    (void)diag;
    // One program-wide id counter, seeded past every real entry. resolve/classify
    // are done and constfold doesn't append, so entries.size() is frozen here; the
    // counter is never restarted per loop, so two lowered short-fors can't collide.
    int next_id = static_cast<int>(in.entries.size());
    for (auto const& n : in.nodes) {
        out.nodes.push_back(copyNode(*n, in, next_id));
    }
    // Hoist namespace member functions to program scope as plain (symbol-renamed)
    // functions; the namespace wrappers were dropped by copyNode. Member consts
    // were substituted away by constfold and need no runtime form.
    for (std::size_t i = 0; i < in.nodes.size(); i++) {
        if (!in.nodes[i] || in.nodes[i]->kind != parse::Kind::kProgram) continue;
        ast::Node* prog = out.nodes[i].get();
        std::vector<parse::Node const*> nss;
        findTopNamespaces(*in.nodes[i], nss);
        for (parse::Node const* ns : nss) {
            std::vector<parse::Node const*> fns;
            collectNamespaceFunctions(*ns, fns);
            for (parse::Node const* f : fns) {
                prog->children.push_back(copyNode(*f, in, next_id));
            }
        }
    }
    // Lift each class's ctor/dtor member bodies to top-level functions named
    // <Class>__$ctor / <Class>__$dtor (the bodies are self-bound — resolve
    // rewrote bare field refs to `self.field`, lowered to slot indices on copy).
    // Codegen calls these symbols at construction / scope exit. Classes defined
    // in a function body (local classes) are collected recursively, and the
    // symbol uses the class's (possibly scope-mangled) canonical type name so
    // same-named local classes don't collide.
    for (std::size_t i = 0; i < in.nodes.size(); i++) {
        if (!in.nodes[i] || in.nodes[i]->kind != parse::Kind::kProgram) continue;
        ast::Node* prog = out.nodes[i].get();
        std::vector<parse::Node const*> classdefs;
        collectClassDefs(*in.nodes[i], classdefs);
        for (parse::Node const* c : classdefs) {
            // The lifted symbol base is minted from the class handle (bare name +
            // def_id for a local), matching codegen's call/struct/sizeof sites.
            std::string sym = widen::classSymbol(widen::strip(c->return_type));
            for (auto const& m : c->children) {
                if (!m || m->kind != parse::Kind::kFunctionDef) continue;
                auto fn = copyNode(*m, in, next_id);
                // ctor/dtor lift to the hook symbols; a METHOD lifts to
                // <Class>__<method> (self is already params[0]). All self-bound.
                if (m->name == "_$ctor")      fn->name = sym + "__$ctor";
                else if (m->name == "_$dtor") fn->name = sym + "__$dtor";
                else                          fn->name = sym + "__" + m->name;
                prog->children.push_back(std::move(fn));
            }
        }
    }
    // PPID lowering: walk each function body's statements, extract ++/--, and
    // splice statement-level pre/post bumps as sibling statements around each
    // statement (post-bumps land after the statement's store).
    for (auto& n : out.nodes) {
        if (!n || n->kind != ast::Kind::kProgram) continue;
        for (auto& fn : n->children) {
            if (!fn || fn->kind != ast::Kind::kFunctionDef) continue;
            lowerStatementList(fn->children);
        }
    }
    // Carry the class kSlid types so codegen can emit each `<Name>__$sizeof()`
    // helper (the parse-side class graph isn't otherwise visible to codegen).
    for (auto const& [ctype, info] : in.classes) {
        (void)ctype;
        if (info.type != widen::kNoType) out.classes.push_back(info.type);
    }
}

}  // namespace desugar
