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
        case parse::Kind::kGlobalScopeStmt: return ast::Kind::kGlobalScopeStmt;
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

// The LLVM symbol for a method: classSymbol(defClass) + "__" + name, plus an
// entry-id suffix when the method name is OVERLOADED in its class (2+ same-name
// method entries in the owner frame) — so distinct overloads get distinct symbols,
// exactly like functionSymbol. The call site and the chosen definition share the
// entry id, so they mangle identically.
std::string methodSymbol(parse::Tree const& tree, widen::TypeRef defCls,
                         std::string const& name, int entry_id) {
    std::string base = widen::classSymbol(defCls) + "__" + name;
    if (entry_id < 0) return base;
    int frame = tree.entries[entry_id].owner_ns_frame;
    int count = 0;
    for (parse::Entry const& q : tree.entries) {
        if (q.kind == parse::EntryKind::kFunction
            && q.owner_ns_frame == frame && q.name == name) {
            count++;
        }
    }
    if (count > 1) return base + "." + std::to_string(entry_id);
    return base;
}

// ---- Virtual dispatch: vtable slot map ----------------------------------------
// The vtable layout for a virtual class is: the base's slots first (each still at its
// base index), then this class's NEW virtual methods appended. An override reuses the
// inherited slot (same name + same user parameters) but supplies this class's impl.
// g_entry_slot maps every virtual method ENTRY to its slot (all overrides of a slot map
// to the same index — stable across the hierarchy, so a call resolves to a slot regardless
// of the runtime type). Populated by buildVtables before the lowering pass; read by
// lowerMethodCall.
std::map<int, int> g_entry_slot;

struct VSlot {
    std::string name;
    std::vector<widen::TypeRef> params;   // includes the receiver at [0]
    int impl_entry;                       // most-derived implementer for THIS class
};

std::map<widen::TypeRef, std::vector<VSlot>> g_vtable_cache;

// The ordered vtable slots for `cls` (memoized). Inherits the base's slots, then folds
// in this class's own virtual methods (overrides replace the slot's impl; new virtuals
// append). Side effect: stamps g_entry_slot for every own virtual method entry.
std::vector<VSlot> const& vtableOf(parse::Tree const& tree, widen::TypeRef cls) {
    cls = widen::strip(cls);
    auto cached = g_vtable_cache.find(cls);
    if (cached != g_vtable_cache.end()) return cached->second;
    std::vector<VSlot> slots;
    widen::TypeRef base = parse::classBaseType(tree, cls);
    if (base != widen::kNoType) slots = vtableOf(tree, base);   // inherit base slots
    int frame = parse::classNsFrame(tree, cls);
    if (frame >= 0) {
        for (int ei = 0; ei < (int)tree.entries.size(); ei++) {
            parse::Entry const& e = tree.entries[ei];
            if (e.kind != parse::EntryKind::kFunction
                || e.owner_ns_frame != frame || !e.is_virtual)
                continue;
            int found = -1;
            for (int k = 0; k < (int)slots.size(); k++)
                if (slots[k].name == e.name
                    && parse::userParamsEqual(slots[k].params, e.param_types)) {
                    found = k;
                    break;
                }
            if (found >= 0) {          // override — reuse the inherited slot
                slots[found].impl_entry = ei;
                g_entry_slot[ei] = found;
            } else {                   // a new virtual — append
                g_entry_slot[ei] = (int)slots.size();
                slots.push_back({e.name, e.param_types, ei});
            }
        }
    }
    return g_vtable_cache[cls] = std::move(slots);
}

// Build a per-virtual-class vtable global for codegen and populate g_entry_slot for
// dispatch. A class is virtual iff its slot list is non-empty.
void buildVtables(parse::Tree const& tree, ast::Tree& out) {
    g_entry_slot.clear();
    g_vtable_cache.clear();
    for (auto const& kv : tree.classes) {
        std::vector<VSlot> const& slots = vtableOf(tree, kv.first);
        if (slots.empty()) continue;
        ast::Vtable vt;
        vt.class_symbol = widen::classSymbol(widen::strip(kv.first));
        // Slot 0 is ALWAYS the destructor — the class's COMPLETE dtor (`__$vdtor`, which
        // runs the dtor body then chains through fields + the base subobject). It sits at
        // a fixed index across the whole hierarchy so `delete base_ptr` dispatches to the
        // most-derived dtor. Virtual methods follow at slots 1+ (g_entry_slot is 0-based
        // over the methods, so lowerMethodCall adds 1).
        vt.slot_symbols.push_back(vt.class_symbol + "__$vdtor");
        for (VSlot const& s : slots) {
            // A PURE slot has no implementation — emit null (an "" sentinel). The class
            // is abstract (not instantiable), so its vtable is never dispatched; a derived
            // that overrides the slot fills in its own impl.
            if (tree.entries[s.impl_entry].is_pure) {
                vt.slot_symbols.push_back("");
                continue;
            }
            int ce = parse::classEntryForFrame(
                tree, tree.entries[s.impl_entry].owner_ns_frame);
            assert(ce >= 0 && "a vtable slot's implementer has no owning class");
            widen::TypeRef defCls = (ce >= 0)
                ? widen::strip(tree.entries[ce].slids_type) : widen::strip(kv.first);
            vt.slot_symbols.push_back(
                methodSymbol(tree, defCls, s.name, s.impl_entry));
        }
        out.vtables.push_back(std::move(vt));
    }
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
std::unique_ptr<ast::Node> mintIdent(std::string const& name, int id,
                                     widen::TypeRef ty, int file, int tok);
bool returnTypeHasClassValue(widen::TypeRef t);
void liftSretCallExprs(std::unique_ptr<ast::Node>& node,
                       std::vector<std::unique_ptr<ast::Node>>& pre,
                       int& next_id, bool root_intercepted);

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
    // Statement-form `Class(args);` (a discarded construction kCallStmt) becomes an
    // UNNAMED scope-lifetime local: a synthetic kVarDeclStmt initialized from the
    // construction tuple (classify left it as children[0], typed as the class). The
    // existing kVarDeclStmt codegen field-inits it, runs the ctor, and registers it
    // for the enclosing scope's reverse-order dtor — no new construction code.
    if (p.kind == parse::Kind::kCallStmt && p.is_construction) {
        auto decl = std::make_unique<ast::Node>();
        decl->kind = ast::Kind::kVarDeclStmt;
        decl->name = "_$nameless";
        decl->resolved_entry_id = next_id++;
        decl->return_type = p.inferred_type;   // the class type
        decl->file_id = p.file_id;
        decl->tok = p.tok;
        decl->name_tok = p.tok;
        assert(!p.children.empty() && "construction stmt lost its tuple");
        decl->children.push_back(copyNode(*p.children[0], tree, next_id));
        return decl;
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
    // ast.is_const means a SUBSTITUTED constant — codegen emits no storage for it.
    // A const VARIABLE (a non-foldable type routed to kLocalVar in resolve) is NOT
    // substituted: it has real storage, so it lowers with is_const=false even though
    // declared `const` — its const-ness rides on its deep-const TYPE, not this flag.
    node->is_const = p.is_const && p.resolved_entry_id >= 0
        && tree.entries[p.resolved_entry_id].kind == parse::EntryKind::kConst;
    node->default_move_init = p.default_move_init;
    node->non_completing = p.non_completing;
    node->is_construction = p.is_construction;
    node->param_types = p.param_types;
    node->captures = p.captures;
    node->capture_types = p.capture_types;
    node->self_entry_id = p.self_entry_id;
    // Function definitions and calls (including qualified `Space:bar()`) resolve
    // to their entry-id-derived symbol; the qualifier is dropped (ast carries no
    // qualifier — a flat symbol replaces it).
    // A construction kCallExpr (resolved to a CLASS, not a function) keeps its
    // class name and is lifted to a temp decl later — never call functionSymbol
    // on it (it has no function symbol).
    if (!p.is_construction
        && (p.kind == parse::Kind::kFunctionDef
            || p.kind == parse::Kind::kFunctionDecl
            || p.kind == parse::Kind::kCallExpr
            || p.kind == parse::Kind::kCallStmt)) {
        node->name = functionSymbol(p, tree);
    }
    for (auto const& c : p.children) {
        if (!c) { node->children.push_back(nullptr); continue; }  // preserve a
                          // null slot (a switch default clause's absent label)
        if (c->kind == parse::Kind::kAliasDecl) continue;      // resolve-only
        if (c->kind == parse::Kind::kNamespaceDecl) continue;  // members hoisted
        if (c->kind == parse::Kind::kClassDef) continue;       // resolve-recorded
        if (c->kind == parse::Kind::kEnumDecl) continue;       // resolve-lowered
        if (c->kind == parse::Kind::kVarDeclStmt && c->is_global) continue;  // a
                          // GLOBAL is static storage (out.globals), not a statement
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

// Flatten every scope reachable from `node` to program-scope functions, in one
// uniform recursion. A namespace contributes its DIRECT member functions as-is; a
// class contributes its member functions renamed to the lifted hook/method symbols
// (`<Class>__$ctor` / `__$dtor` / `__<method>`). Either kind recurses into itself,
// so a scope nested in a scope — class-in-namespace, namespace-in-class, a local
// class in a function body, to any depth — is flattened by the SAME walk, with no
// per-context arm. (The wrapper nodes themselves were dropped by copyNode; this
// repopulates their members at program scope where codegen emits them.)
void flattenScope(parse::Node const& node, ast::Node* prog,
                  parse::Tree const& in, int& next_id) {
    for (auto const& m : node.children) {
        if (!m) continue;
        if (m->kind == parse::Kind::kNamespaceDecl) {
            for (auto const& f : m->children)
                if (f && f->kind == parse::Kind::kFunctionDef)
                    prog->children.push_back(copyNode(*f, in, next_id));
        } else if (m->kind == parse::Kind::kClassDef) {
            std::string sym = widen::classSymbol(widen::strip(m->return_type));
            for (auto const& f : m->children) {
                if (!f || f->kind != parse::Kind::kFunctionDef) continue;
                auto fn = copyNode(*f, in, next_id);
                if      (f->name == "_$ctor") fn->name = sym + "__$ctor";
                else if (f->name == "_$dtor") fn->name = sym + "__$dtor";
                else fn->name = methodSymbol(in, widen::strip(m->return_type),
                                             f->name, f->resolved_entry_id);
                prog->children.push_back(std::move(fn));
            }
        }
        flattenScope(*m, prog, in, next_id);   // recurse: deeper scopes, fn bodies
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

// An inc/dec operand that is not a bare ident: an index or deref chain. (After
// copyNode a field is already a kIndexExpr — there is no ast::kFieldExpr.) A
// complex operand needs an address-once binding, unlike a bare ident whose
// alloca already gives a stable address.
bool isComplexLvalue(ast::Node const& n) {
    return n.kind == ast::Kind::kIndexExpr
        || n.kind == ast::Kind::kDerefExpr;
}

// `_$lv` reference ident — its loaded VALUE is the captured leaf address.
std::unique_ptr<ast::Node> makeLvIdent(int lv_id, widen::TypeRef refT,
                                       int file, int tok) {
    auto id = std::make_unique<ast::Node>();
    id->kind = ast::Kind::kIdentExpr;
    id->name = "_$lv";
    id->resolved_entry_id = lv_id;
    id->inferred_type = refT;
    id->file_id = file; id->tok = tok;
    return id;
}

// `_$lv^` — the leaf read/write site, a deref of the captured address.
std::unique_ptr<ast::Node> makeLvDeref(int lv_id, widen::TypeRef refT,
                                       widen::TypeRef leaf, int file, int tok) {
    auto d = std::make_unique<ast::Node>();
    d->kind = ast::Kind::kDerefExpr;
    d->inferred_type = leaf;
    d->file_id = file; d->tok = tok;
    d->children.push_back(makeLvIdent(lv_id, refT, file, tok));
    return d;
}

// A complex-lvalue bump: a kBumpExpr whose children[0] yields the leaf ADDRESS
// (the `_$lv` reference loaded). codegen loads the leaf through that address,
// ±1, stores back — reusing the scalar bump's int/float/iterator logic keyed on
// inferred_type (the leaf). resolved_entry_id stays -1 (address-, not id-based).
std::unique_ptr<ast::Node> makeComplexBump(int lv_id, widen::TypeRef refT,
                                           widen::TypeRef leaf,
                                           std::string const& op,
                                           int file, int tok) {
    auto b = std::make_unique<ast::Node>();
    b->kind = ast::Kind::kBumpExpr;
    b->text = op;
    b->resolved_entry_id = -1;
    b->inferred_type = leaf;
    b->file_id = file; b->tok = tok;
    b->children.push_back(makeLvIdent(lv_id, refT, file, tok));
    return b;
}

// The address-once binding `_$lv = ^leaf` (a reference var-decl). For a deref
// target the address IS the pointer operand (don't take `^(ptr^)`); for an
// index/field target it is `^operand` (a reference to the leaf). Mirrors
// tryDesugarAugAssign. `operand` is consumed. Out-params return refT and the
// `_$lv` entry id.
std::unique_ptr<ast::Node> makeLvDecl(std::unique_ptr<ast::Node> operand,
                                      int& next_id, widen::TypeRef& refT,
                                      int& lv_id) {
    lv_id = next_id++;
    widen::TypeRef leaf = operand->inferred_type;
    int file = operand->file_id, tok = operand->tok;
    std::unique_ptr<ast::Node> addr;
    if (operand->kind == ast::Kind::kDerefExpr) {
        refT = operand->children[0]->inferred_type;   // the pointer's type
        addr = std::move(operand->children[0]);
    } else {
        refT = widen::internPointer(leaf);            // a reference to the leaf
        addr = std::make_unique<ast::Node>();
        addr->kind = ast::Kind::kAddrOfExpr;
        addr->inferred_type = refT;
        addr->file_id = file; addr->tok = tok;
        addr->children.push_back(std::move(operand));
    }
    auto decl = std::make_unique<ast::Node>();
    decl->kind = ast::Kind::kVarDeclStmt;
    decl->name = "_$lv";
    decl->resolved_entry_id = lv_id;
    decl->return_type = refT;
    decl->file_id = file; decl->tok = tok; decl->name_tok = tok;
    decl->children.push_back(std::move(addr));
    return decl;
}

void lowerPhraseSlot(std::unique_ptr<ast::Node>& slot, int& next_id,
                     bool lift_constructions = false);

// Walk an expression that belongs to the CURRENT phrase: collect its bumps into
// pre/post, replace each inc/dec with a read of the operand. Sub-phrase
// children — call args and the rhs of && / || — recurse via lowerPhraseSlot.
void lowerInPhrase(std::unique_ptr<ast::Node>& slot,
                   std::vector<std::unique_ptr<ast::Node>>& pre,
                   std::vector<std::unique_ptr<ast::Node>>& post,
                   int& next_id) {
    if (!slot) return;
    ast::Node& n = *slot;
    if (n.kind == ast::Kind::kPreIncExpr || n.kind == ast::Kind::kPostIncExpr) {
        bool is_pre = n.kind == ast::Kind::kPreIncExpr;
        if (isComplexLvalue(*n.children[0])) {
            // Lower any nested inc/dec in the operand's index chain into THIS
            // phrase first (e.g. `arr[i++]++`), then bind the leaf address once.
            lowerInPhrase(n.children[0], pre, post, next_id);
            int file = n.file_id, tok = n.tok;
            std::string op = n.text;
            widen::TypeRef leaf = n.inferred_type;
            widen::TypeRef refT;
            int lv_id;
            auto decl = makeLvDecl(std::move(n.children[0]), next_id, refT, lv_id);
            auto bump = makeComplexBump(lv_id, refT, leaf, op, file, tok);
            if (is_pre) {
                // bump fires at phrase entry: capture then mutate, both in pre;
                // the read yields the post-increment value.
                pre.push_back(std::move(decl));
                pre.push_back(std::move(bump));
                slot = makeLvDeref(lv_id, refT, leaf, file, tok);
            } else {
                // bump fires at phrase exit: capture + read at THIS point (a seq
                // so the address is taken once, at the read), bump in post.
                auto read_seq = std::make_unique<ast::Node>();
                read_seq->kind = ast::Kind::kSeqExpr;
                read_seq->inferred_type = leaf;
                read_seq->file_id = file; read_seq->tok = tok;
                read_seq->value_index = 1;   // [decl, read] -> value is the read
                read_seq->children.push_back(std::move(decl));
                read_seq->children.push_back(
                    makeLvDeref(lv_id, refT, leaf, file, tok));
                slot = std::move(read_seq);
                post.push_back(std::move(bump));
            }
            return;
        }
        auto operand = std::move(n.children[0]);   // ident lvalue (resolve checked)
        auto bump = makeBump(*operand, n.text);
        if (is_pre) pre.push_back(std::move(bump));
        else        post.push_back(std::move(bump));
        slot = std::move(operand);                 // the read replaces the inc/dec
        return;
    }
    if (n.kind == ast::Kind::kCallExpr) {
        for (auto& arg : n.children) lowerPhraseSlot(arg, next_id);
        return;
    }
    if (n.kind == ast::Kind::kTupleExpr) {
        // Each comma slot of a tuple literal is its OWN phrase (like a call arg),
        // evaluated left to right — so its bumps stay inside that slot's seq.
        for (auto& el : n.children) lowerPhraseSlot(el, next_id);
        return;
    }
    if (n.kind == ast::Kind::kBinaryExpr && (n.text == "&&" || n.text == "||")) {
        lowerInPhrase(n.children[0], pre, post, next_id);  // lhs: same phrase
        // rhs: own conditionally-evaluated phrase — lift any construction into the
        // rhs's OWN sub-seq (constructed/destroyed only when the short-circuit
        // doesn't skip it), not into this phrase's pre.
        lowerPhraseSlot(n.children[1], next_id, /*lift_constructions=*/true);
        return;
    }
    // Every other interior node (arith/bitwise/^^/unary) is part of this
    // phrase; leaves have no children, so the loop is a no-op for them.
    // seq/bump are synthesized by this pass — produced above and never re-walked
    // (each phrase is lowered once), so reaching one here is a bug.
    assert(n.kind != ast::Kind::kSeqExpr && n.kind != ast::Kind::kBumpExpr
        && "lowerInPhrase: re-walked a synthesized PPID node");
    for (auto& c : n.children) lowerInPhrase(c, pre, post, next_id);
}

// Lower the phrase rooted at `slot`; wrap it in a seq iff it carried bumps.
void lowerPhraseSlot(std::unique_ptr<ast::Node>& slot, int& next_id,
                     bool lift_constructions) {
    if (!slot) return;
    std::vector<std::unique_ptr<ast::Node>> pre, post;
    // A CONSTRUCTION (or ctor-returning call) inside a CONDITION phrase is lifted to
    // a `_$cret` temp in the seq's PRE — constructed at phrase entry and destroyed
    // at phrase exit (codegen's kSeqExpr destroys a class temp after the value). The
    // temp is thus scoped to the condition's EVALUATION and rebuilt each time the
    // phrase runs (each loop iteration). The seq re-evaluates per iteration, so a
    // loop condition's receiver is fresh each pass — the same machinery PPID uses.
    // ONLY for conditions: a return value / call arg builds-in-place via its own path.
    if (lift_constructions)
        liftSretCallExprs(slot, pre, next_id, /*root_intercepted=*/false);
    lowerInPhrase(slot, pre, post, next_id);
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
                        std::vector<std::unique_ptr<ast::Node>>& post,
                        int& next_id) {
    switch (stmt.kind) {
        // The statement IS the phrase: its direct expression operands carry
        // statement-level bumps (pre before, post after the statement).
        case ast::Kind::kVarDeclStmt:
        case ast::Kind::kAssignStmt:
            // children[0] = the rhs (the lvalue is a bare name, no ppid).
            if (!stmt.children.empty() && stmt.children[0]) {
                lowerInPhrase(stmt.children[0], pre, post, next_id);
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
                if (ch) lowerInPhrase(ch, pre, post, next_id);
            }
            return;
        case ast::Kind::kDestructureStmt:
            // children[0] = the rhs tuple (its slots are sub-phrases via the
            // kTupleExpr arm); [1..] = plain target lvalues (a complex target with
            // ppid is the deferred complex-lhs case).
            if (!stmt.children.empty() && stmt.children[0]) {
                lowerInPhrase(stmt.children[0], pre, post, next_id);
            }
            return;
        case ast::Kind::kReturnStmt:
            if (!stmt.children.empty() && stmt.children[0]) {
                lowerPhraseSlot(stmt.children[0], next_id);
            }
            return;
        case ast::Kind::kCallExpr:   // a discarded method-call expression statement
        case ast::Kind::kCallStmt:
            for (auto& arg : stmt.children) lowerPhraseSlot(arg, next_id);
            return;
        case ast::Kind::kExprStmt: {
            // The value is discarded. A bare inc/dec needs only its bump — no read.
            auto& child = stmt.children[0];
            if (child->kind == ast::Kind::kPreIncExpr
                || child->kind == ast::Kind::kPostIncExpr) {
                if (isComplexLvalue(*child->children[0])) {
                    // Complex lvalue, value discarded: bind the address once (pre)
                    // then bump through it. Pre vs post is moot with no read.
                    lowerInPhrase(child->children[0], pre, post, next_id);
                    int file = child->file_id, tok = child->tok;
                    std::string op = child->text;
                    widen::TypeRef leaf = child->inferred_type;
                    widen::TypeRef refT;
                    int lv_id;
                    auto decl = makeLvDecl(std::move(child->children[0]),
                                           next_id, refT, lv_id);
                    pre.push_back(std::move(decl));
                    child = makeComplexBump(lv_id, refT, leaf, op, file, tok);
                } else {
                    child = makeBump(*child->children[0], child->text);
                }
            } else {
                lowerPhraseSlot(child, next_id);
            }
            return;
        }
        // Leaf statements with no liftable ppid operand.
        case ast::Kind::kDeleteStmt:
        case ast::Kind::kDtorCallStmt:
        case ast::Kind::kBreakStmt:
        case ast::Kind::kContinueStmt:
        case ast::Kind::kGlobalScopeStmt:
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

void lowerStatementList(std::vector<std::unique_ptr<ast::Node>>& stmts,
                        int& next_id);

// ---------------------------------------------------------------------------
// Aggregate-copy lowering. An array IS a homogeneous tuple; an operation on an
// aggregate is performed BY SLOT, iteratively and recursively, down to scalar
// leaves. A copy whose source and destination differ in SHAPE (cross-form array
// <-> tuple at any level) or in LEAF TYPE (element widening) is lowered here to
// per-slot stores; a same-type aggregate copy stays a single whole-value store
// (the trivial base case). Indexing is form-agnostic (codegen's emitElementAddr
// dispatches on the current type — array dim or tuple/field slot), so the same
// `dst[i] = src[i]` shape walks arrays and tuples alike.

bool isAggForm(widen::TypeRef t) {
    widen::Type::Form f = widen::form(widen::strip(t));
    return f == widen::Type::Form::kTuple || f == widen::Type::Form::kArray;
}

// The number of top-level slots of an aggregate: a tuple's slot count, or an
// array's outermost dimension.
int aggSlotCount(widen::TypeRef t) {
    widen::TypeRef s = widen::strip(t);
    if (widen::form(s) == widen::Type::Form::kTuple) {
        return static_cast<int>(widen::get(s).slots.size());
    }
    std::vector<int> const& dims = widen::get(s).dims;
    return dims.empty() ? 0 : dims[0];
}

// The i-th sub-component type of an aggregate: a tuple's slot, or an array's
// element with the outermost dimension stripped (same for every i).
widen::TypeRef aggSlotType(widen::TypeRef t, int i) {
    widen::TypeRef s = widen::strip(t);
    if (widen::form(s) == widen::Type::Form::kTuple) {
        return widen::get(s).slots[i];
    }
    widen::Type const& a = widen::get(s);
    widen::TypeRef elem = a.elem;                       // copy before any intern
    std::vector<int> rest(a.dims.begin() + 1, a.dims.end());
    return rest.empty() ? elem : widen::internArray(elem, rest);
}

// Deep-clone an expression subtree (the lvalue base is replicated once per leaf
// store). ast::Node holds unique_ptr children, so it can't be copy-constructed.
std::unique_ptr<ast::Node> cloneAstExpr(ast::Node const& n) {
    auto c = std::make_unique<ast::Node>();
    c->kind = n.kind;
    c->name = n.name;
    c->text = n.text;
    c->return_type = n.return_type;
    c->nominal_type = n.nominal_type;
    c->inferred_type = n.inferred_type;
    c->op_type = n.op_type;
    c->file_id = n.file_id;
    c->tok = n.tok;
    c->name_tok = n.name_tok;
    c->resolved_entry_id = n.resolved_entry_id;
    c->value_index = n.value_index;
    c->loop_levels = n.loop_levels;
    c->is_const = n.is_const;
    c->default_move_init = n.default_move_init;
    c->non_completing = n.non_completing;
    c->param_types = n.param_types;
    c->captures = n.captures;
    c->capture_types = n.capture_types;
    c->self_entry_id = n.self_entry_id;
    for (auto const& ch : n.children) {
        c->children.push_back(ch ? cloneAstExpr(*ch) : nullptr);
    }
    for (auto const& p : n.params) {
        c->params.push_back(p ? cloneAstExpr(*p) : nullptr);
    }
    return c;
}

// `base[i]` as a kIndexExpr typed `slotT`. The index is a plain int literal —
// codegen reads its text for a tuple slot and emits it as an i64 for an array.
std::unique_ptr<ast::Node> buildSlotIndex(std::unique_ptr<ast::Node> base, int i,
                                          widen::TypeRef slotT, int file, int tok) {
    auto idx = std::make_unique<ast::Node>();
    idx->kind = ast::Kind::kIntLiteral;
    idx->text = std::to_string(i);
    idx->inferred_type = widen::intern("int");
    idx->nominal_type = idx->inferred_type;
    idx->file_id = file;
    idx->tok = tok;
    auto ix = std::make_unique<ast::Node>();
    ix->kind = ast::Kind::kIndexExpr;
    ix->inferred_type = slotT;
    ix->file_id = file;
    ix->tok = tok;
    ix->children.push_back(std::move(base));
    ix->children.push_back(std::move(idx));
    return ix;
}

// Emit per-slot leaf stores copying `src` into `dst` (both lvalue exprs over
// addressable storage). Recurse while the destination slot is itself an
// aggregate that differs from the source slot (cross-form, or a deeper leaf
// widen); otherwise emit one store of the whole slot (a scalar, with widen, or a
// same-type sub-aggregate as a whole-value copy).
void emitAggCopyLeaves(ast::Node const& dst, ast::Node const& src,
                       std::vector<std::unique_ptr<ast::Node>>& out,
                       int file, int tok) {
    widen::TypeRef dstT = dst.inferred_type;
    widen::TypeRef srcT = src.inferred_type;
    int n = aggSlotCount(dstT);
    for (int i = 0; i < n; i++) {
        widen::TypeRef dSlot = aggSlotType(dstT, i);
        widen::TypeRef sSlot = aggSlotType(srcT, i);
        auto dIx = buildSlotIndex(cloneAstExpr(dst), i, dSlot, file, tok);
        auto sIx = buildSlotIndex(cloneAstExpr(src), i, sSlot, file, tok);
        if (isAggForm(dSlot)
            && widen::deepStrip(dSlot) != widen::deepStrip(sSlot)) {
            emitAggCopyLeaves(*dIx, *sIx, out, file, tok);
        } else {
            auto store = std::make_unique<ast::Node>();
            store->kind = ast::Kind::kStoreStmt;
            store->file_id = file;
            store->tok = tok;
            store->children.push_back(std::move(dIx));
            store->children.push_back(std::move(sIx));
            out.push_back(std::move(store));
        }
    }
}

bool isSimpleLvalue(ast::Node const& n) {
    return n.kind == ast::Kind::kIdentExpr
        || n.kind == ast::Kind::kIndexExpr
        || n.kind == ast::Kind::kDerefExpr;
}

// A fresh ident referring to a desugar-minted local (the spill temp / a decl's
// var), used as the base of the per-slot index chain.
std::unique_ptr<ast::Node> mintIdent(std::string const& name, int id,
                                     widen::TypeRef ty, int file, int tok) {
    auto n = std::make_unique<ast::Node>();
    n->kind = ast::Kind::kIdentExpr;
    n->name = name;
    n->resolved_entry_id = id;
    n->inferred_type = ty;
    n->file_id = file;
    n->tok = tok;
    return n;
}

// A re-evaluable index / deref operand: a literal or a bare ident (re-reading it
// has no side effect). Anything else (a call, `++`/`--`, an arith expr) is hoisted
// before a by-slot walk re-indexes the lvalue.
bool isTrivialIndex(ast::Node const& n) {
    return n.kind == ast::Kind::kIntLiteral
        || n.kind == ast::Kind::kUintLiteral
        || n.kind == ast::Kind::kCharLiteral
        || n.kind == ast::Kind::kBoolLiteral
        || n.kind == ast::Kind::kFloatLiteral
        || n.kind == ast::Kind::kNullptrLiteral
        || n.kind == ast::Kind::kIdentExpr;
}

// Move `lv.children[ci]` into a fresh `_$ix` temp decl (pushed to `pre`) and
// replace it with an ident, so re-reading that position has no side effect.
void hoistChildToTemp(ast::Node& lv, std::size_t ci, int& next_id,
                      std::vector<std::unique_ptr<ast::Node>>& pre) {
    ast::Node& sub = *lv.children[ci];
    widen::TypeRef ty = sub.inferred_type;
    int file = sub.file_id, tok = sub.tok;
    int id = next_id++;
    auto decl = std::make_unique<ast::Node>();
    decl->kind = ast::Kind::kVarDeclStmt;
    decl->name = "_$ix";
    decl->resolved_entry_id = id;
    decl->return_type = ty;
    decl->file_id = file;
    decl->tok = tok;
    decl->name_tok = tok;
    decl->children.push_back(std::move(lv.children[ci]));
    pre.push_back(std::move(decl));
    lv.children[ci] = mintIdent("_$ix", id, ty, file, tok);
}

// Hoist any side-effecting sub-expression of an lvalue — an index, a deref operand,
// recursing the base — into a temp, so the lvalue is SIDE-EFFECT-FREE to re-index.
// A by-slot copy / move then reads `g[_$i]` ONCE, not `g[bump()]` once per slot.
void hoistLvalueSideEffects(ast::Node& lv, int& next_id,
                            std::vector<std::unique_ptr<ast::Node>>& pre) {
    if (lv.kind == ast::Kind::kIndexExpr && lv.children.size() >= 2) {
        if (lv.children[0]) hoistLvalueSideEffects(*lv.children[0], next_id, pre);
        if (lv.children[1] && !isTrivialIndex(*lv.children[1]))
            hoistChildToTemp(lv, 1, next_id, pre);
    } else if (lv.kind == ast::Kind::kDerefExpr && !lv.children.empty()
               && lv.children[0]) {
        if (!isTrivialIndex(*lv.children[0])) hoistChildToTemp(lv, 0, next_id, pre);
        else hoistLvalueSideEffects(*lv.children[0], next_id, pre);
    }
}

// If `src` is not an indexable lvalue (a literal / computed value), spill it into a
// fresh same-type `_$agg` local so each slot read is a simple index. If it IS an
// lvalue, hoist any side-effecting index / operand so re-indexing it per slot — and,
// for a move, the per-leaf null — runs the side effect ONCE. Returns the indexable
// base; pushes any spill / hoist decls into `pre`.
std::unique_ptr<ast::Node> spillSource(std::unique_ptr<ast::Node> src,
                                       widen::TypeRef srcType, int& next_id,
                                       std::vector<std::unique_ptr<ast::Node>>& pre) {
    if (isSimpleLvalue(*src)) {
        hoistLvalueSideEffects(*src, next_id, pre);
        return src;
    }
    int file = src->file_id, tok = src->tok;
    int id = next_id++;
    auto decl = std::make_unique<ast::Node>();
    decl->kind = ast::Kind::kVarDeclStmt;
    decl->name = "_$agg";
    decl->resolved_entry_id = id;
    decl->return_type = srcType;
    decl->file_id = file;
    decl->tok = tok;
    decl->name_tok = tok;
    decl->children.push_back(std::move(src));
    pre.push_back(std::move(decl));
    return mintIdent("_$agg", id, srcType, file, tok);
}

// True if `t` has any pointer / iterator leaf (recursing tuple slots + array
// elements). Mirrors codegen's typeHasPointer; used to skip the per-leaf source
// null of a pointer-free aggregate move.
bool typeHasPointerLeaf(widen::TypeRef t) {
    widen::TypeRef s = widen::strip(t);
    widen::Type::Form f = widen::form(s);
    if (f == widen::Type::Form::kPointer || f == widen::Type::Form::kIterator)
        return true;
    if (f == widen::Type::Form::kTuple) {
        std::vector<widen::TypeRef> slots = widen::get(s).slots;   // copy: no intern
        for (widen::TypeRef slot : slots)
            if (typeHasPointerLeaf(slot)) return true;
        return false;
    }
    if (f == widen::Type::Form::kArray) {
        widen::TypeRef elem = widen::get(s).elem;                  // copy: no intern
        return typeHasPointerLeaf(elem);
    }
    return false;
}

// Emit per-leaf null stores over the source lvalue `src`: for each pointer /
// iterator leaf, store nullptr; recurse into aggregate slots. The desugar analogue
// of codegen's emitNullLeaves — a moved-from source is left valid (its pointers
// nulled) by the BY-SLOT lowering rather than the codegen seam.
void emitAggNullLeaves(ast::Node const& src,
                       std::vector<std::unique_ptr<ast::Node>>& out,
                       int file, int tok) {
    widen::TypeRef t = src.inferred_type;
    widen::Type::Form f = widen::form(widen::strip(t));
    if (f == widen::Type::Form::kPointer || f == widen::Type::Form::kIterator) {
        auto nullp = std::make_unique<ast::Node>();
        nullp->kind = ast::Kind::kNullptrLiteral;
        nullp->inferred_type = t;
        nullp->file_id = file;
        nullp->tok = tok;
        auto store = std::make_unique<ast::Node>();
        store->kind = ast::Kind::kStoreStmt;
        store->file_id = file;
        store->tok = tok;
        store->children.push_back(cloneAstExpr(src));
        store->children.push_back(std::move(nullp));
        out.push_back(std::move(store));
        return;
    }
    if (!isAggForm(t) || !typeHasPointerLeaf(t)) return;   // scalar / pointer-free
    int n = aggSlotCount(t);
    for (int i = 0; i < n; i++) {
        widen::TypeRef slotT = aggSlotType(t, i);
        auto ix = buildSlotIndex(cloneAstExpr(src), i, slotT, file, tok);
        emitAggNullLeaves(*ix, out, file, tok);
    }
}

// Lower one statement's aggregate cross-form / leaf-widen copy. Returns the
// replacement statement list (empty -> not an aggregate copy, leave unchanged).
// Handles kVarDeclStmt (init), kAssignStmt (bare name), kStoreStmt (lvalue),
// kMoveStmt (lvalue, plus a per-leaf source null), and kReturnStmt (materialize a
// return temp, copy by slot). `fn_return_type` is the enclosing function's return
// type (used only by the kReturnStmt arm).
std::vector<std::unique_ptr<ast::Node>> lowerAggCopyStmt(ast::Node& s,
                                                         parse::Tree const& tree,
                                                         widen::TypeRef fn_return_type,
                                                         int& next_id) {
    std::vector<std::unique_ptr<ast::Node>> repl;
    int file = s.file_id, tok = s.tok;

    // RETURN of a cross-form / leaf-widen aggregate: materialize a `_$ret` temp of
    // the function's return type, copy the value into it BY SLOT, return the temp.
    // (The forms below copy into an already-existing destination lvalue.)
    if (s.kind == ast::Kind::kReturnStmt) {
        if (s.children.empty() || !s.children[0]) return repl;
        if (!isAggForm(fn_return_type)
            || !isAggForm(s.children[0]->inferred_type)
            || widen::deepStrip(fn_return_type)
                == widen::deepStrip(s.children[0]->inferred_type)) {
            return repl;
        }
        int id = next_id++;
        std::unique_ptr<ast::Node> srcNode = std::move(s.children[0]);
        widen::TypeRef srcType = srcNode->inferred_type;
        std::vector<std::unique_ptr<ast::Node>> body;
        auto retDecl = std::make_unique<ast::Node>();
        retDecl->kind = ast::Kind::kVarDeclStmt;
        retDecl->name = "_$ret";
        retDecl->resolved_entry_id = id;
        retDecl->return_type = fn_return_type;
        retDecl->file_id = file;
        retDecl->tok = tok;
        retDecl->name_tok = tok;
        body.push_back(std::move(retDecl));
        std::unique_ptr<ast::Node> srcBase =
            spillSource(std::move(srcNode), srcType, next_id, body);
        std::unique_ptr<ast::Node> dstBase =
            mintIdent("_$ret", id, fn_return_type, file, tok);
        emitAggCopyLeaves(*dstBase, *srcBase, body, file, tok);
        auto ret = std::make_unique<ast::Node>();
        ret->kind = ast::Kind::kReturnStmt;
        ret->file_id = file;
        ret->tok = tok;
        ret->children.push_back(mintIdent("_$ret", id, fn_return_type, file, tok));
        body.push_back(std::move(ret));
        auto block = std::make_unique<ast::Node>();
        block->kind = ast::Kind::kBlockStmt;
        block->file_id = file;
        block->tok = tok;
        for (auto& b : body) block->children.push_back(std::move(b));
        repl.push_back(std::move(block));
        return repl;
    }
    bool is_move = false;

    // Identify (dstType, dst-lvalue-builder, src-child-slot). A decl keeps its
    // (init-stripped) node as the leading statement so the var stays in the
    // enclosing scope; the spill + stores follow in a block that scopes `_$agg`.
    widen::TypeRef dstType = widen::kNoType;
    std::unique_ptr<ast::Node> dstBase;        // an lvalue over the destination
    std::unique_ptr<ast::Node> srcNode;        // moved out of s

    if (s.kind == ast::Kind::kVarDeclStmt) {
        if (s.children.empty() || s.is_const || s.default_move_init) return repl;
        // The declared type rides on the node (copyNode copies it; a desugar-minted
        // decl stamps it) — NEVER index tree.entries by a local's id (minted locals
        // have no entry; see the for-lowering INVARIANT).
        dstType = s.return_type;
        if (!isAggForm(dstType)) return repl;
        srcNode = std::move(s.children[0]);
        if (!isAggForm(srcNode->inferred_type)
            || widen::deepStrip(dstType)
                == widen::deepStrip(srcNode->inferred_type)) {
            s.children[0] = std::move(srcNode);   // restore; not our case
            return repl;
        }
        dstBase = mintIdent(s.name, s.resolved_entry_id, dstType, file, tok);
    } else if (s.kind == ast::Kind::kAssignStmt) {
        if (s.children.empty()) return repl;
        // A bare-name assign carries no lvalue type on the node; recover it from the
        // variable's entry. Only a SOURCE-level variable (a real entry) can be an
        // aggregate cross-form target — a desugar-minted local (id past the entry
        // table) is a scalar loop/temp binding, so skip it (and never over-read).
        if (s.resolved_entry_id < 0
            || s.resolved_entry_id >= static_cast<int>(tree.entries.size())) {
            return repl;
        }
        dstType = parse::entryType(tree, s.resolved_entry_id);
        if (!isAggForm(dstType)) return repl;
        srcNode = std::move(s.children[0]);
        if (!isAggForm(srcNode->inferred_type)
            || widen::deepStrip(dstType)
                == widen::deepStrip(srcNode->inferred_type)) {
            s.children[0] = std::move(srcNode);
            return repl;
        }
        dstBase = mintIdent(s.name, s.resolved_entry_id, dstType, file, tok);
    } else if (s.kind == ast::Kind::kStoreStmt) {
        if (s.children.size() != 2) return repl;
        dstType = s.children[0]->inferred_type;
        if (!isAggForm(dstType)) return repl;
        srcNode = std::move(s.children[1]);
        if (!isAggForm(srcNode->inferred_type)
            || widen::deepStrip(dstType)
                == widen::deepStrip(srcNode->inferred_type)) {
            s.children[1] = std::move(srcNode);
            return repl;
        }
        dstBase = std::move(s.children[0]);
    } else if (s.kind == ast::Kind::kMoveStmt) {
        if (s.children.size() != 2) return repl;
        dstType = s.children[0]->inferred_type;
        if (!isAggForm(dstType)) return repl;
        srcNode = std::move(s.children[1]);
        if (!isAggForm(srcNode->inferred_type)
            || widen::deepStrip(dstType)
                == widen::deepStrip(srcNode->inferred_type)) {
            s.children[1] = std::move(srcNode);
            return repl;
        }
        dstBase = std::move(s.children[0]);
        is_move = true;
    } else {
        return repl;
    }

    widen::TypeRef srcType = srcNode->inferred_type;
    std::vector<std::unique_ptr<ast::Node>> body;
    std::unique_ptr<ast::Node> srcBase =
        spillSource(std::move(srcNode), srcType, next_id, body);
    // The DEST lvalue (a store / move target like `g[bump()]`) is re-indexed per slot
    // too — hoist its side effects (a no-op for a bare-ident dest).
    hoistLvalueSideEffects(*dstBase, next_id, body);
    // A move nulls the SOURCE's pointer leaves after the copy. spillSource has made
    // the source a side-effect-free lvalue (hoisting any side-effecting index), so the
    // null reads the SAME storage the copy did — the source runs once.
    std::unique_ptr<ast::Node> moveNullSrc =
        is_move ? cloneAstExpr(*srcBase) : nullptr;
    emitAggCopyLeaves(*dstBase, *srcBase, body, file, tok);
    if (moveNullSrc) emitAggNullLeaves(*moveNullSrc, body, file, tok);

    auto block = std::make_unique<ast::Node>();
    block->kind = ast::Kind::kBlockStmt;
    block->file_id = file;
    block->tok = tok;
    for (auto& b : body) block->children.push_back(std::move(b));

    if (s.kind == ast::Kind::kVarDeclStmt) {
        // Keep the decl (its alloca + any ctor hooks), stripped of its init.
        s.children.clear();
        auto decl = std::make_unique<ast::Node>(std::move(s));
        repl.push_back(std::move(decl));
    }
    repl.push_back(std::move(block));
    return repl;
}

// Walk a statement list, lowering aggregate copies in place and recursing into
// nested compound statements (mirrors lowerStatementList's structural descent).
void lowerAggregateList(std::vector<std::unique_ptr<ast::Node>>& stmts,
                        parse::Tree const& tree, widen::TypeRef fn_return_type,
                        int& next_id) {
    std::vector<std::unique_ptr<ast::Node>> out;
    for (auto& stmt : stmts) {
        if (!stmt) { out.push_back(nullptr); continue; }
        ast::Kind k = stmt->kind;
        if (k == ast::Kind::kBlockStmt) {
            lowerAggregateList(stmt->children, tree, fn_return_type, next_id);
        } else if (k == ast::Kind::kIfStmt || k == ast::Kind::kWhileStmt
                || k == ast::Kind::kDoWhileStmt || k == ast::Kind::kForLongStmt) {
            for (std::size_t i = 1; i < stmt->children.size(); i++) {
                if (!stmt->children[i]) continue;
                if (stmt->children[i]->kind == ast::Kind::kBlockStmt) {
                    lowerAggregateList(stmt->children[i]->children, tree,
                                       fn_return_type, next_id);
                } else if (stmt->children[i]->kind == ast::Kind::kIfStmt) {
                    // else-if chain: recurse the nested if as a one-stmt list.
                    std::vector<std::unique_ptr<ast::Node>> one;
                    one.push_back(std::move(stmt->children[i]));
                    lowerAggregateList(one, tree, fn_return_type, next_id);
                    stmt->children[i] = std::move(one[0]);
                }
            }
        } else if (k == ast::Kind::kSwitchStmt) {
            for (std::size_t i = 1; i < stmt->children.size(); i++) {
                if (stmt->children[i]
                    && stmt->children[i]->children.size() > 1
                    && stmt->children[i]->children.back()) {
                    lowerAggregateList(stmt->children[i]->children.back()->children,
                                       tree, fn_return_type, next_id);
                }
            }
        }
        auto repl = lowerAggCopyStmt(*stmt, tree, fn_return_type, next_id);
        if (repl.empty()) {
            out.push_back(std::move(stmt));
        } else {
            for (auto& r : repl) out.push_back(std::move(r));
        }
    }
    stmts = std::move(out);
}

// A return type that constructs (a class with a ctor, or an aggregate with such a
// leaf). Mirrors codegen::typeNeedsHook(ctor). Used to find calls whose result an
// inline use can't destroy in place — they are lifted to a temp.
// True when `t` is, or contains, a CLASS value returned by value (sret). Such a
// call result used inline is a TEMPORARY that must be materialized into a named
// `_$cret` slot — both so it is destroyed (when the class has a dtor) AND so its
// ADDRESS can be taken (a method receiver lowers to `^_$cret`). A trivial class (no
// ctor/dtor) is still returned by sret and still needs the slot, so the gate is "is
// a class value", NOT "has a ctor" — a chained call `a().m()` on a by-value class
// return would otherwise hit `^<call>` (addr-of an rvalue) and abort in codegen.
bool returnTypeHasClassValue(widen::TypeRef t) {
    using F = widen::Type::Form;
    widen::TypeRef s = widen::strip(t);
    F f = widen::form(s);
    if (f == F::kSlid) return true;
    if (f == F::kArray) { widen::TypeRef e = widen::get(s).elem; return returnTypeHasClassValue(e); }
    if (f == F::kTuple) {
        std::vector<widen::TypeRef> slots = widen::get(s).slots;   // copy: no intern
        for (widen::TypeRef sl : slots) if (returnTypeHasClassValue(sl)) return true;
    }
    return false;
}

// Lift hook-returning calls within `node` into `pre` temp decls, INSIDE-OUT (an
// inner call lifts before its enclosing call, preserving evaluation order), each
// replaced by an ident reading the temp. `root_intercepted` means `node` itself
// sits at a position codegen already handles (the rhs of a decl/assign/return,
// which constructs in place) — don't lift node itself, only its arg subtrees. The
// minted `_$cret` decl is a class/aggregate init from a call, so codegen's
// kVarDeclStmt sret intercept constructs it and registers it for destruction at the
// enclosing scope; `take(mk())` becomes `Class _$cret = mk(); take(_$cret)`.
void liftSretCallExprs(std::unique_ptr<ast::Node>& node,
                       std::vector<std::unique_ptr<ast::Node>>& pre,
                       int& next_id, bool root_intercepted) {
    if (!node) return;
    // A short-circuit `&&` / `||` evaluates its RHS conditionally. A construction in
    // the RHS must be lifted into the RHS's OWN sub-seq (done by lowerInPhrase's
    // && / || arm via lowerPhraseSlot), NOT hoisted into this (unconditional) phrase's
    // pre — else its ctor/dtor would run even when the short-circuit skips it. Descend
    // only into the LHS here (it runs unconditionally); leave the RHS for that path.
    // The `&&` / `||` node itself is bool, never ctor-returning, so there is nothing
    // to lift at this node.
    if (node->kind == ast::Kind::kBinaryExpr
        && (node->text == "&&" || node->text == "||")) {
        liftSretCallExprs(node->children[0], pre, next_id, false);
        return;
    }
    for (auto& ch : node->children)
        liftSretCallExprs(ch, pre, next_id, false);
    // A `Class(args)` construction used inline (e.g. a method receiver) is a
    // TEMPORARY — lift it into a synthetic decl exactly like a hook-returning call,
    // but seed the decl from the construction TUPLE (node->children[0], typed as the
    // class), so the kVarDeclStmt field-inits + runs the ctor. The block-wrap in
    // liftSretCallList then makes the temp's dtor fire at STATEMENT end.
    bool is_class_call = node->kind == ast::Kind::kCallExpr
        && returnTypeHasClassValue(node->return_type);
    bool is_construction = node->is_construction;
    if (!root_intercepted && (is_class_call || is_construction)) {
        int file = node->file_id, tok = node->tok;
        widen::TypeRef T = node->return_type;
        int id = next_id++;
        auto decl = std::make_unique<ast::Node>();
        decl->kind = ast::Kind::kVarDeclStmt;
        decl->name = "_$cret";
        decl->resolved_entry_id = id;
        decl->return_type = T;
        decl->file_id = file;
        decl->tok = tok;
        decl->name_tok = tok;
        if (is_construction) {
            // The construction's per-field tuple init (not the call node itself).
            assert(!node->children.empty() && "construction lost its tuple");
            decl->children.push_back(std::move(node->children[0]));
        } else {
            decl->children.push_back(std::move(node));
        }
        pre.push_back(std::move(decl));
        node = mintIdent("_$cret", id, T, file, tok);
    }
}

// Walk a statement list, lifting hook-returning calls out of inline (expression)
// positions into preceding temp decls. Mirrors lowerAggregateList's structural
// descent into compound statements. A call at a codegen-handled root (decl/assign/
// return rhs, or a discarded call statement) is left in place — only NESTED calls
// (e.g. a call as another call's argument) are lifted. Loop / if conditions and
// store/move/swap operands are NOT lifted (a hook call there still errors at
// codegen, pending a later increment).
void liftSretCallList(std::vector<std::unique_ptr<ast::Node>>& stmts, int& next_id) {
    std::vector<std::unique_ptr<ast::Node>> out;
    for (auto& stmt : stmts) {
        if (!stmt) { out.push_back(nullptr); continue; }
        ast::Kind k = stmt->kind;
        if (k == ast::Kind::kBlockStmt) {
            liftSretCallList(stmt->children, next_id);
        } else if (k == ast::Kind::kIfStmt || k == ast::Kind::kWhileStmt
                || k == ast::Kind::kDoWhileStmt || k == ast::Kind::kForLongStmt) {
            for (std::size_t i = 1; i < stmt->children.size(); i++) {
                if (!stmt->children[i]) continue;
                if (stmt->children[i]->kind == ast::Kind::kBlockStmt) {
                    liftSretCallList(stmt->children[i]->children, next_id);
                } else if (stmt->children[i]->kind == ast::Kind::kIfStmt) {
                    std::vector<std::unique_ptr<ast::Node>> one;
                    one.push_back(std::move(stmt->children[i]));
                    liftSretCallList(one, next_id);
                    stmt->children[i] = std::move(one[0]);
                }
            }
        } else if (k == ast::Kind::kSwitchStmt) {
            for (std::size_t i = 1; i < stmt->children.size(); i++) {
                if (stmt->children[i]
                    && stmt->children[i]->children.size() > 1
                    && stmt->children[i]->children.back()) {
                    liftSretCallList(stmt->children[i]->children.back()->children,
                                     next_id);
                }
            }
        }
        std::vector<std::unique_ptr<ast::Node>> pre;
        // A lifted temp inside a kCallStmt / kExprStmt is a STATEMENT-scoped
        // temporary (a `Class(args)` construction receiver, or a hook-returning
        // method receiver) — it must be destroyed at the SEMICOLON, so its decls
        // are wrapped with the statement in a synthetic block whose scope end IS
        // the statement end. A VarDecl/Assign/Return rhs only lifts NESTED arg
        // temps; those keep their established enclosing-scope lifetime (no wrap).
        bool stmt_scoped = false;
        if (k == ast::Kind::kVarDeclStmt || k == ast::Kind::kAssignStmt
            || k == ast::Kind::kReturnStmt) {
            // A `Class(args)` construction as the DIRECT rhs of a DECLARATION or a
            // RETURN builds in place: unwrap it to its per-field construction tuple
            // (children[0]) so the existing kVarDeclStmt field-init / return sret-
            // fallback path constructs it into the slot (store + emitConstructHooks) —
            // no temp, no extra ctor. (`Class x = Class(...)`, `return Class(x)`.)
            // A re-ASSIGNMENT (`w = Class(...)`, w already constructed) has no fresh
            // slot to build into: unwrapping would store the fields WITHOUT running
            // the ctor (silently dropping it). Leave the construction in place so it
            // reaches codegen's value-position guard and errors cleanly — class
            // re-assignment from a construction is not supported (declare a new
            // variable instead).
            if (k != ast::Kind::kAssignStmt
                && !stmt->children.empty() && stmt->children[0]
                && stmt->children[0]->is_construction) {
                assert(!stmt->children[0]->children.empty()
                    && "construction rhs lost its tuple");
                stmt->children[0] = std::move(stmt->children[0]->children[0]);
            }
            // The direct rhs call is constructed in place by codegen — leave it
            // (root_intercepted), but lift any NESTED call in its args.
            if (!stmt->children.empty())
                liftSretCallExprs(stmt->children[0], pre, next_id,
                                  /*root_intercepted=*/true);
        } else if (k == ast::Kind::kCallStmt || k == ast::Kind::kCallExpr) {
            // The statement IS a (discarded) call codegen handles; lift its args
            // (incl. a lowered method call's receiver = AddrOf(temp)). A discarded
            // method-call EXPRESSION statement is a kCallExpr (lowerMethodCall emits
            // a value call); its construction/rvalue receiver lifts the same way.
            for (auto& arg : stmt->children)
                liftSretCallExprs(arg, pre, next_id, false);
            stmt_scoped = true;
        } else if (k == ast::Kind::kExprStmt) {
            if (!stmt->children.empty())
                liftSretCallExprs(stmt->children[0], pre, next_id, false);
            stmt_scoped = true;
        }
        // A CONDITION's construction temp is NOT hoisted here — it must be scoped to
        // the condition's EVALUATION (rebuilt per loop iteration), so it is lifted
        // into the condition seq by lowerPhraseSlot (the PPID phrase path) instead.
        if (stmt_scoped && !pre.empty()) {
            auto block = std::make_unique<ast::Node>();
            block->kind = ast::Kind::kBlockStmt;
            block->file_id = stmt->file_id;
            block->tok = stmt->tok;
            for (auto& d : pre) block->children.push_back(std::move(d));
            block->children.push_back(std::move(stmt));
            out.push_back(std::move(block));
        } else {
            for (auto& d : pre) out.push_back(std::move(d));
            out.push_back(std::move(stmt));
        }
    }
    stmts = std::move(out);
}

// Collect kReturnStmt nodes in a body, NOT descending into nested function defs
// (they own their own sret slot / returns).
void collectReturns(ast::Node& n, std::vector<ast::Node*>& out) {
    if (n.kind == ast::Kind::kFunctionDef || n.kind == ast::Kind::kFunctionDecl) return;
    if (n.kind == ast::Kind::kReturnStmt) out.push_back(&n);
    for (auto& ch : n.children) if (ch) collectReturns(*ch, out);
}

// Find the kVarDeclStmt declaring local `id` in a body (skip nested fns).
ast::Node* findLocalDecl(ast::Node& n, int id) {
    if (n.kind == ast::Kind::kFunctionDef || n.kind == ast::Kind::kFunctionDecl)
        return nullptr;
    if (n.kind == ast::Kind::kVarDeclStmt && n.resolved_entry_id == id) return &n;
    for (auto& ch : n.children)
        if (ch) { if (ast::Node* d = findLocalDecl(*ch, id)) return d; }
    return nullptr;
}

bool nrvoContains(std::vector<int> const& v, int x) {
    for (int e : v) if (e == x) return true;
    return false;
}

// A `return X` whose X is one of the candidate returned-locals.
bool isNrvoLocalReturn(ast::Node const& r, std::vector<int> const& candidates,
                       int& out_id) {
    if (r.children.empty() || !r.children[0]) return false;
    ast::Node const& rv = *r.children[0];
    if (rv.kind != ast::Kind::kIdentExpr) return false;
    if (!nrvoContains(candidates, rv.resolved_entry_id)) return false;
    out_id = rv.resolved_entry_id;
    return true;
}

// Scope walk computing NRVO INELIGIBILITY. `live` = candidate returned-locals
// currently in scope (popped at each block's end). A candidate is ineligible if it
// can't safely share the single %sret.in slot — i.e. its lifetime overlaps another
// returned-local's (caught at a decl while the other is live), or it is live at a
// SLOT-WRITING return (an rvalue / call / param / non-exact return that writes
// %sret.in directly, which would clobber it). Checking DECL-point liveness (not just
// return points) is what makes good() eligible and both bad() shapes ineligible.
void nrvoOverlapWalk(std::vector<std::unique_ptr<ast::Node>>& stmts,
                     std::vector<int> const& candidates,
                     std::vector<int>& live, std::vector<int>& ineligible) {
    auto mark = [&](int id) {
        if (!nrvoContains(ineligible, id)) ineligible.push_back(id);
    };
    std::vector<int> declared_here;
    for (auto& s : stmts) {
        if (!s) continue;
        ast::Kind k = s->kind;
        if (k == ast::Kind::kVarDeclStmt
            && nrvoContains(candidates, s->resolved_entry_id)) {
            int L = s->resolved_entry_id;
            for (int m : live) { mark(L); mark(m); }   // L coexists with every live M
            live.push_back(L);
            declared_here.push_back(L);
        } else if (k == ast::Kind::kReturnStmt) {
            int id;
            if (!isNrvoLocalReturn(*s, candidates, id))     // slot-writing return
                for (int m : live) mark(m);
        } else if (k == ast::Kind::kBlockStmt) {
            nrvoOverlapWalk(s->children, candidates, live, ineligible);
        } else if (k == ast::Kind::kIfStmt || k == ast::Kind::kWhileStmt
                || k == ast::Kind::kDoWhileStmt || k == ast::Kind::kForLongStmt) {
            for (std::size_t i = 1; i < s->children.size(); i++) {
                if (!s->children[i]) continue;
                if (s->children[i]->kind == ast::Kind::kBlockStmt) {
                    nrvoOverlapWalk(s->children[i]->children, candidates, live,
                                    ineligible);
                } else if (s->children[i]->kind == ast::Kind::kIfStmt) {
                    std::vector<std::unique_ptr<ast::Node>> one;
                    one.push_back(std::move(s->children[i]));
                    nrvoOverlapWalk(one, candidates, live, ineligible);
                    s->children[i] = std::move(one[0]);
                }
            }
        } else if (k == ast::Kind::kSwitchStmt) {
            for (std::size_t i = 1; i < s->children.size(); i++) {
                if (s->children[i] && s->children[i]->children.size() > 1
                    && s->children[i]->children.back())
                    nrvoOverlapWalk(s->children[i]->children.back()->children,
                                    candidates, live, ineligible);
            }
        }
    }
    for (int L : declared_here)
        for (std::size_t i = 0; i < live.size(); i++)
            if (live[i] == L) { live.erase(live.begin() + i); break; }
}

// NRVO: a returned local L of the EXACT return type whose lifetime is DISJOINT from
// every other returned-local (and from any slot-writing return) IS the return value
// — alias its storage to %sret.in, build it in place, never move/dtor it here. Mark
// L's decl + each `return L` `nrvo` (codegen makes them a bare `ret void`). Multiple
// disjoint locals (good(): different local per arm) all share %sret.in safely — only
// one is live per path. Overlapping locals (bad()) fall back to the move. Applies to
// hook returns (one construct/destruct) and POD aggregates / classes (eliding the
// copy). Exactness keeps cross-form returns on the `_$ret` path.
void analyzeNrvo(ast::Node& fn) {
    using F = widen::Type::Form;
    F f = widen::form(widen::strip(fn.return_type));
    if (f != F::kArray && f != F::kTuple && f != F::kSlid) return;  // sret returns only
    std::vector<ast::Node*> returns;
    for (auto& ch : fn.children) if (ch) collectReturns(*ch, returns);
    if (returns.empty()) return;
    // Candidate returned-locals: `return L` for a bare ident of the EXACT return type
    // backed by a real local decl (a param has its own incoming storage — can't
    // alias to the slot, so a `return param` is a slot-writing return).
    std::vector<int> candidates;
    for (ast::Node* r : returns) {
        if (r->children.empty() || !r->children[0]) continue;
        ast::Node const& rv = *r->children[0];
        if (rv.kind != ast::Kind::kIdentExpr || rv.resolved_entry_id < 0) continue;
        if (widen::deepStrip(rv.inferred_type) != widen::deepStrip(fn.return_type))
            continue;
        if (nrvoContains(candidates, rv.resolved_entry_id)) continue;
        ast::Node* decl = nullptr;
        for (auto& ch : fn.children)
            if (ch) { decl = findLocalDecl(*ch, rv.resolved_entry_id); if (decl) break; }
        if (decl) candidates.push_back(rv.resolved_entry_id);
    }
    if (candidates.empty()) return;
    std::vector<int> live, ineligible;
    nrvoOverlapWalk(fn.children, candidates, live, ineligible);
    for (int L : candidates) {
        if (nrvoContains(ineligible, L)) continue;
        ast::Node* decl = nullptr;
        for (auto& ch : fn.children)
            if (ch) { decl = findLocalDecl(*ch, L); if (decl) break; }
        if (decl) decl->nrvo = true;
    }
    for (ast::Node* r : returns) {
        int id;
        if (isNrvoLocalReturn(*r, candidates, id) && !nrvoContains(ineligible, id))
            r->nrvo = true;
    }
}

// Splice a pre/post entry as a sibling statement. A scalar bump is a kBumpExpr
// (an expression) wrapped in a kExprStmt; a complex lvalue's address-once
// binding is already a kVarDeclStmt and rides through unchanged.
std::unique_ptr<ast::Node> spliceStmt(std::unique_ptr<ast::Node> b) {
    if (b->kind == ast::Kind::kVarDeclStmt) return b;
    return makeBumpStmt(std::move(b));
}

// Lower an if-statement's PPID. The condition is a self-contained phrase: its
// bumps stay inside a seq and fire as the condition is evaluated, before the
// branch (like a call argument). The then/else branches are statement lists;
// an `else if` chain is a nested kIfStmt, recursed structurally.
void lowerIfStmt(ast::Node& s, int& next_id) {
    if (!s.children.empty() && s.children[0]) lowerPhraseSlot(s.children[0], next_id, /*lift_constructions=*/true);  // if condition
    if (s.children.size() > 1 && s.children[1]) {
        lowerStatementList(s.children[1]->children, next_id);   // then-block
    }
    if (s.children.size() > 2 && s.children[2]) {
        ast::Node& else_branch = *s.children[2];
        if (else_branch.kind == ast::Kind::kIfStmt) lowerIfStmt(else_branch, next_id);
        else lowerStatementList(else_branch.children, next_id);  // else-block
    }
}

// Lower a while / do-while loop's PPID. The condition is a self-contained phrase
// whose bumps fire each time it is tested (re-evaluated per iteration); the body
// is a statement list. Both kinds share the child layout ([cond, body]), so this
// serves kWhileStmt and kDoWhileStmt alike.
void lowerWhileStmt(ast::Node& s, int& next_id) {
    if (!s.children.empty() && s.children[0]) lowerPhraseSlot(s.children[0], next_id, /*lift_constructions=*/true);  // while/do-while condition
    if (s.children.size() > 1 && s.children[1]) {
        lowerStatementList(s.children[1]->children, next_id);   // body block
    }
}

// Lower a long-for's PPID. children[0]=cond, [1]=update, [2]=body, [3..]=varlist.
// The varlist initializers run once (lower each as a self-contained phrase); the
// condition is a phrase re-tested each iteration; update + body are statement
// lists.
void lowerForLong(ast::Node& s, int& next_id) {
    for (std::size_t i = 3; i < s.children.size(); i++) {
        if (s.children[i] && !s.children[i]->children.empty()
            && s.children[i]->children[0]) {
            lowerPhraseSlot(s.children[i]->children[0], next_id);   // varlist init
        }
    }
    if (!s.children.empty() && s.children[0]) lowerPhraseSlot(s.children[0], next_id, /*lift_constructions=*/true);  // cond
    if (s.children.size() > 1 && s.children[1]) {
        lowerStatementList(s.children[1]->children, next_id);   // update block
    }
    if (s.children.size() > 2 && s.children[2]) {
        lowerStatementList(s.children[2]->children, next_id);   // body block
    }
}

// Lower a switch's PPID. children[0] = scrutinee (a self-contained phrase whose
// bumps fire once as it is evaluated); [1..] = kCaseClause, whose labels are
// folded constants (no bumps) and whose body is children.back().
void lowerSwitchStmt(ast::Node& s, int& next_id) {
    if (!s.children.empty() && s.children[0]) lowerPhraseSlot(s.children[0], next_id, /*lift_constructions=*/true);  // switch discriminant
    for (std::size_t i = 1; i < s.children.size(); i++) {
        if (!s.children[i]) continue;
        ast::Node& clause = *s.children[i];
        if (clause.children.size() > 1 && clause.children.back()) {
            lowerStatementList(clause.children.back()->children, next_id);   // body
        }
    }
}

// Run PPID statement-bump splicing over a statement list, rebuilding it with
// pre-bumps before / post-bumps after each statement. Recurses into a nested
// kBlockStmt / kIfStmt so bumps inside them splice within that scope, not at the
// enclosing one.
void lowerStatementList(std::vector<std::unique_ptr<ast::Node>>& stmts,
                        int& next_id) {
    std::vector<std::unique_ptr<ast::Node>> lowered;
    for (auto& stmt : stmts) {
        if (!stmt) continue;
        if (stmt->kind == ast::Kind::kBlockStmt) {
            lowerStatementList(stmt->children, next_id);
            lowered.push_back(std::move(stmt));
            continue;
        }
        if (stmt->kind == ast::Kind::kIfStmt) {
            lowerIfStmt(*stmt, next_id);
            lowered.push_back(std::move(stmt));
            continue;
        }
        if (stmt->kind == ast::Kind::kWhileStmt
            || stmt->kind == ast::Kind::kDoWhileStmt) {
            lowerWhileStmt(*stmt, next_id);
            lowered.push_back(std::move(stmt));
            continue;
        }
        if (stmt->kind == ast::Kind::kForLongStmt) {
            lowerForLong(*stmt, next_id);
            lowered.push_back(std::move(stmt));
            continue;
        }
        if (stmt->kind == ast::Kind::kSwitchStmt) {
            lowerSwitchStmt(*stmt, next_id);
            lowered.push_back(std::move(stmt));
            continue;
        }
        std::vector<std::unique_ptr<ast::Node>> pre, post;
        lowerStatementPPID(*stmt, pre, post, next_id);
        for (auto& b : pre) lowered.push_back(spliceStmt(std::move(b)));
        lowered.push_back(std::move(stmt));
        for (auto& b : post) lowered.push_back(spliceStmt(std::move(b)));
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
    // A value-producing call: usable as an expression (`x = obj.m()`) AND as a
    // discarded statement (`obj.m();`) — emitStmt emits a kCallExpr statement the
    // same way it emits a kCallStmt (value discarded, sret result destroyed).
    call->kind = ast::Kind::kCallExpr;
    call->name = methodSymbol(tree, defCls, p.name, p.resolved_entry_id);
    call->return_type = p.return_type;   // emitCall reads return_type
    call->inferred_type = p.return_type; // a value: its type IS the method's return
                                         // (a switch scrutinee reads inferred_type)
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

    // VIRTUAL DISPATCH: a call to a virtual method — unless it is a `Base:method()`
    // bypass — loads the receiver's vtable and calls indirect through the method's slot.
    // The static symbol (call->name) is left as a harmless fallback; codegen ignores it
    // when vtable_slot >= 0. An inherited method resolves to the base's (virtual) entry,
    // whose slot is valid in every derived vtable (base slots keep their index).
    if (p.resolved_entry_id >= 0 && !p.bypass_virtual
        && tree.entries[p.resolved_entry_id].is_virtual) {
        auto it = g_entry_slot.find(p.resolved_entry_id);
        assert(it != g_entry_slot.end()
            && "a virtual method has no vtable slot (buildVtables missed it)");
        if (it != g_entry_slot.end()) call->vtable_slot = it->second + 1;  // +1: dtor at 0
    }
    return call;
}

}  // namespace

// Does this subtree contain an explicit `global;` statement (at any depth)? Used to
// suppress the auto-insert when main already opens the global scope itself.
bool astHasGlobalScope(ast::Node const& n) {
    if (n.kind == ast::Kind::kGlobalScopeStmt) return true;
    for (auto const& c : n.children)
        if (c && astHasGlobalScope(*c)) return true;
    return false;
}

// Record a file-local global (kGlobalVar) for codegen: mint a stable `@`-symbol,
// carry its declared/inferred type, and lower its folded initializer to a literal
// ast node (codegen emits it as the static LLVM constant; a null init zero-fills).
void recordGlobal(parse::Node const& n, parse::Tree const& in, ast::Tree& out,
                  int& next_id) {
    int id = n.resolved_entry_id;
    if (id < 0) return;
    ast::GlobalVar gv;
    gv.symbol = "__global_" + n.name + "_" + std::to_string(id);
    gv.type = in.entries[id].slids_type;
    if (!n.children.empty() && n.children[0])
        gv.init = copyNode(*n.children[0], in, next_id);
    out.globals[id] = std::move(gv);
}

// A group's ctor/dtor member is spelled `_$gctor`/`_$gdtor` in a NAMED group (unique
// within its namespace) and `_$glazyctor_<id>`/`_$glazydtor_<id>` in a dissolved ANON
// group (hoisted to program scope, so it needs a globally-unique name). One predicate
// per role recognizes both spellings, so the collector's ctor-vs-dtor test is uniform.
bool isGlobalCtorName(std::string const& n) {
    return n == "_$gctor" || n.rfind("_$glazyctor_", 0) == 0;
}
bool isGlobalDtorName(std::string const& n) {
    return n == "_$gdtor" || n.rfind("_$glazydtor_", 0) == 0;
}

// Fill a group's sentinel + first-touch gate symbols from a per-id prefix. The
// prefix distinguishes the three group flavors so their symbols never collide:
// `__global_` (named group), `__ganon_` (dissolved anon group), `__cg_` (compound).
void setGroupGateSymbols(ast::GlobalGroup& g, char const* prefix, int id) {
    g.sentinel_symbol = std::string(prefix) + "sentinel_" + std::to_string(id);
    g.touch_symbol = std::string(prefix) + "touch_" + std::to_string(id);
}

// Does a global need a SYNTHESIZED ctor (and maybe dtor)? A class-containing type
// always does; a scalar aggregate does only when it has an initializer to store. A
// plain foldable scalar stays a static `@`-global (initialized before main).
bool needsSynth(widen::TypeRef type, bool has_init) {
    widen::Type::Form f = widen::form(widen::strip(type));
    bool aggregate = f == widen::Type::Form::kArray
                     || f == widen::Type::Form::kTuple
                     || f == widen::Type::Form::kSlid;
    return widen::hasInPlaceClass(type) || (aggregate && has_init);
}

// Finalize a NAMED or ANON group after its members + user hooks are collected: keep
// only the members that actually need the gate (a foldable scalar in a hook-less group
// stays static; a hook forces ALL members onto the gate so any access fires the ctor),
// wire the survivors to the group touch, and name the synth ctor/dtor thunks. Returns
// false for an inert group (no gated member, no user hook) — the caller drops it.
bool finalizeGroup(ast::GlobalGroup& g, ast::Tree& out) {
    bool has_hook = !g.user_ctor_symbol.empty() || !g.user_dtor_symbol.empty();
    bool need_dtor = !g.user_dtor_symbol.empty();
    std::vector<int> gated;
    for (int id : g.member_ids) {
        // .at (not []) — every member_id was recordGlobal'd before being pushed, so a
        // miss is a broken invariant that should abort LOUDLY, not silently synthesize
        // an empty kNoType global that miscompiles downstream.
        ast::GlobalVar& gv = out.globals.at(id);
        if (!has_hook && !needsSynth(gv.type, (bool)gv.init)) continue;  // static scalar
        gated.push_back(id);
        gv.touch_symbol = g.touch_symbol;
        if (widen::hasInPlaceClass(gv.type)) need_dtor = true;
    }
    g.member_ids = std::move(gated);
    if (g.member_ids.empty() && !has_hook) return false;
    g.ctor_symbol = g.touch_symbol + "_ctor";
    g.dtor_symbol = need_dtor ? g.touch_symbol + "_dtor" : std::string();
    return true;
}

// Walk scope containers, recording every global declaration into out.globals.
// A global GROUP (an is_global namespace) with a `_$gctor` member is LAZY: record a
// GlobalGroup (sentinel/touch/ctor/dtor symbols) and stamp each member's
// touch_symbol so its access sites gate on first-touch construction.
void collectGlobals(std::vector<std::unique_ptr<parse::Node>> const& nodes,
                    parse::Tree const& in, ast::Tree& out, int& next_id,
                    std::map<int, ast::GlobalGroup>& lazy_anon) {
    for (auto const& n : nodes) {
        if (!n) continue;
        // A dissolved LAZY anonymous group's ctor/dtor — a receiver-less free function
        // stamped with the group id. Fills the shared group's ctor/dtor symbol; the
        // sentinel/touch symbols derive from the id (distinct `__ganon_` prefix so they
        // never collide with a named group's `__global_` ones).
        if (n->kind == parse::Kind::kFunctionDef && n->global_group_id >= 0) {
            int gid = n->global_group_id;
            ast::GlobalGroup& g = lazy_anon[gid];
            setGroupGateSymbols(g, "__ganon_", gid);
            std::string sym = functionSymbol(*n, in);
            if (isGlobalCtorName(n->name)) g.user_ctor_symbol = sym;
            else g.user_dtor_symbol = sym;
            collectGlobals(n->children, in, out, next_id, lazy_anon);
            continue;
        }
        if (n->kind == parse::Kind::kVarDeclStmt && n->is_global) {
            recordGlobal(*n, in, out, next_id);
            // A dissolved anon member joins its group (in declaration order). Its
            // gating (vs static) is decided in finalizeGroup, once the group's hooks
            // are known — the ctor/dtor functions may be visited after this member.
            if (n->global_group_id >= 0 && n->resolved_entry_id >= 0) {
                int gid = n->global_group_id;
                ast::GlobalGroup& g = lazy_anon[gid];
                setGroupGateSymbols(g, "__ganon_", gid);
                g.member_ids.push_back(n->resolved_entry_id);
            }
            continue;
        }
        if (n->kind == parse::Kind::kNamespaceDecl && n->is_global) {
            // A named group is its own gate: construct every compound member on first
            // touch of any member, then run the user `_()`; tear down in reverse. Class
            // members ride the group gate whether or not the group has a user ctor/dtor.
            ast::GlobalGroup g;
            setGroupGateSymbols(g, "__global_", n->resolved_entry_id);
            for (auto const& m : n->children) {
                if (!m || m->kind != parse::Kind::kFunctionDef) continue;
                if (isGlobalCtorName(m->name)) g.user_ctor_symbol = functionSymbol(*m, in);
                else if (isGlobalDtorName(m->name)) g.user_dtor_symbol = functionSymbol(*m, in);
            }
            for (auto const& m : n->children) {
                if (!(m && m->kind == parse::Kind::kVarDeclStmt && m->is_global)) continue;
                recordGlobal(*m, in, out, next_id);
                if (m->resolved_entry_id >= 0) g.member_ids.push_back(m->resolved_entry_id);
            }
            if (finalizeGroup(g, out)) out.global_groups.push_back(std::move(g));
            continue;
        }
        if (n->kind == parse::Kind::kProgram
            || n->kind == parse::Kind::kNamespaceDecl
            || n->kind == parse::Kind::kClassDef
            || n->kind == parse::Kind::kFunctionDef
            || n->kind == parse::Kind::kBlockStmt)
            collectGlobals(n->children, in, out, next_id, lazy_anon);
    }
}

void run(parse::Tree const& in, ast::Tree& out, diagnostic::Sink& diag) {
    (void)diag;
    // One program-wide id counter, seeded past every real entry. resolve/classify
    // are done and constfold doesn't append, so entries.size() is frozen here; the
    // counter is never restarted per loop, so two lowered short-fors can't collide.
    int next_id = static_cast<int>(in.entries.size());
    // Compute the vtable slot map + per-class vtable globals BEFORE lowering, so
    // lowerMethodCall can stamp each virtual call's slot (via g_entry_slot).
    buildVtables(in, out);
    std::map<int, ast::GlobalGroup> lazy_anon;
    collectGlobals(in.nodes, in, out, next_id, lazy_anon);
    for (auto& [gid, g] : lazy_anon)
        if (finalizeGroup(g, out)) out.global_groups.push_back(std::move(g));
    // Lone COMPOUND globals (array / tuple / class) NOT in any group are LAZY (the
    // all-compound-lazy policy): zero-init storage + a SYNTHESIZED ctor/dtor that
    // construct/destruct `@symbol` in place on first touch. A class-containing type
    // needs both hooks; a scalar aggregate needs a ctor only when it has an initializer
    // to store (a zero-init scalar array stays a static `zeroinitializer`).
    for (auto& [id, gv] : out.globals) {
        if (!gv.touch_symbol.empty()) continue;      // already in a group
        if (!needsSynth(gv.type, (bool)gv.init)) continue;   // scalars / zero-init stay static
        ast::GlobalGroup g;
        g.synth_global_id = id;
        setGroupGateSymbols(g, "__cg_", id);
        g.ctor_symbol = "__cgctor_" + std::to_string(id);
        g.dtor_symbol = widen::hasInPlaceClass(gv.type)
                            ? "__cgdtor_" + std::to_string(id) : std::string();
        gv.touch_symbol = g.touch_symbol;
        out.global_groups.push_back(std::move(g));
    }
    for (auto const& n : in.nodes) {
        out.nodes.push_back(copyNode(*n, in, next_id));
    }
    // Flatten every scope's member functions to program scope in ONE uniform
    // recursion (namespaces hoist their functions, classes lift their methods +
    // ctor/dtor hooks, both recurse), so any nesting is handled the same way. The
    // wrappers themselves were dropped by copyNode; member consts were substituted
    // away by constfold and need no runtime form.
    for (std::size_t i = 0; i < in.nodes.size(); i++) {
        if (!in.nodes[i] || in.nodes[i]->kind != parse::Kind::kProgram) continue;
        flattenScope(*in.nodes[i], out.nodes[i].get(), in, next_id);
    }
    // sret-call lift: hoist a hook-returning call out of an inline (expression)
    // position into a preceding `_$cret` temp decl, so the call lands at a position
    // codegen can construct + own (the temp's dtor runs at scope). Runs BEFORE the
    // aggregate-copy + PPID passes so the lifted decls / reduced statements are
    // lowered like any other.
    for (auto& n : out.nodes) {
        if (!n || n->kind != ast::Kind::kProgram) continue;
        for (auto& fn : n->children) {
            if (!fn || fn->kind != ast::Kind::kFunctionDef) continue;
            liftSretCallList(fn->children, next_id);
        }
    }
    // Aggregate-copy lowering: rewrite every cross-form / leaf-widen aggregate
    // copy (array <-> tuple, any nesting) into per-slot stores BEFORE PPID, so a
    // copy with a `++` index lowers its bumps with the rest. Runs after the
    // aug-assign / class-lift phases so their generated copies are lowered too.
    for (auto& n : out.nodes) {
        if (!n || n->kind != ast::Kind::kProgram) continue;
        for (auto& fn : n->children) {
            if (!fn || fn->kind != ast::Kind::kFunctionDef) continue;
            lowerAggregateList(fn->children, in, fn->return_type, next_id);
        }
    }
    // PPID lowering: walk each function body's statements, extract ++/--, and
    // splice statement-level pre/post bumps as sibling statements around each
    // statement (post-bumps land after the statement's store).
    for (auto& n : out.nodes) {
        if (!n || n->kind != ast::Kind::kProgram) continue;
        for (auto& fn : n->children) {
            if (!fn || fn->kind != ast::Kind::kFunctionDef) continue;
            lowerStatementList(fn->children, next_id);
        }
    }
    // NRVO analysis: after all lowering, decide which functions can build their
    // returned local directly in the sret slot (mark decl + returns `nrvo`).
    for (auto& n : out.nodes) {
        if (!n || n->kind != ast::Kind::kProgram) continue;
        for (auto& fn : n->children) {
            if (!fn || fn->kind != ast::Kind::kFunctionDef) continue;
            analyzeNrvo(*fn);
        }
    }
    // Auto-insert `global;` at the TOP of `main` when it has none and lazy groups
    // exist — the global lifetime then spans all of `main` (its dtor registry runs
    // at main's exit). An EXPLICIT `global;` anywhere in main suppresses this.
    if (!out.global_groups.empty()) {
        for (auto& n : out.nodes) {
            if (!n || n->kind != ast::Kind::kProgram) continue;
            for (auto& fn : n->children) {
                if (!fn || fn->kind != ast::Kind::kFunctionDef || fn->name != "main")
                    continue;
                if (!astHasGlobalScope(*fn)) {
                    auto g = std::make_unique<ast::Node>();
                    g->kind = ast::Kind::kGlobalScopeStmt;
                    g->file_id = fn->file_id;
                    g->tok = fn->tok;
                    fn->children.insert(fn->children.begin(), std::move(g));
                }
            }
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
