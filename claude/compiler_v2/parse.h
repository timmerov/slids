#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "widen.h"   // widen::TypeRef — the structured type handle

namespace parse {

enum class Kind {
    kProgram,
    kFunctionDef,
    kFunctionDecl,
    kVarDeclStmt,
    kAssignStmt,
    kAugAssignStmt,  // name = lhs, text = op (e.g. "+", "&&"); children[0] = rhs
    kStoreStmt,      // store through an lvalue EXPRESSION (not a bare name):
                     // children[0] = lvalue expr (e.g. kDerefExpr), [1] = rhs.
                     // Used for `ref^ = v`; future array/field stores route here.
    kDestructureStmt,// `(a, b, ) = tuple;` — children[0] = rhs tuple expr,
                     // [1..] = target lvalues in slot order; a NULL child is an
                     // empty/skipped slot. Arity must match the rhs tuple.
    kMoveStmt,       // `a <-- b;` — a specialized assignment: children[0] = lhs
                     // lvalue expr, [1] = rhs. Copies rhs into lhs (widen / ptr-
                     // cast rules), then NULLS every addressable pointer leaf of
                     // the rhs. Desugar lowers it to a copy-assign + nullptr-
                     // stores; never reaches codegen.
    kSwapStmt,       // `a <--> b;` — exchange. children[0]/[1] = the two lvalue
                     // exprs (EXACTLY the same type). Desugar lowers it to a temp
                     // + three assigns; never reaches codegen.
    kDeleteStmt,   // delete p; — frees the pointer and nulls it. children[0] = the
                   // pointer lvalue (a variable). Phase 4: lowers to free() + store
                   // null; destructors land with classes (Phase 5).
    kDtorCallStmt, // lvalue.~(); — explicit destructor call (placement cleanup):
                   // runs the dtor on the receiver, NO free. children[0] = the class
                   // lvalue (the object whose destructor runs).
    kCallStmt,
    kCallExpr,     // value-producing call; name = callee, children = args
    kExprStmt,     // expression evaluated for effect, value discarded; children[0] = expr
    kAliasDecl,    // alias Name = Type; name = alias, return_type = target spelling
                   // bare `alias Ns;` (no `=`): name = last segment, qualifier =
                   // leading namespace segments, global_qualified for `::`
    kNamespaceDecl,// Name { members }; name = namespace, children = member decls.
                   // Consumed by resolve+desugar; never reaches codegen.
    kClassDef,     // Name(field-list){body} — a class. name = class, params =
                   // kParam fields (children[0] = optional default value), children
                   // = body member defs (definitions only; empty in the trivial
                   // bucket). resolve records a ClassInfo + interns the named-tuple
                   // type; desugar drops the node (construction lowers at the use
                   // site). A class "is a namespace + a named tuple".
    kFieldExpr,    // postfix `base.name` — a named field of a class lvalue.
                   // children[0] = base (a class lvalue), name = field name.
                   // classify infers it to the field type (via the class's
                   // ClassInfo); desugar lowers it to a kIndexExpr over the field's
                   // slot index, so it never reaches codegen (a class is a named
                   // tuple — slot access by name).
    kEnumDecl,     // enum [type] [Name] ( members ); name = enum (empty =
                   // anonymous), return_type = underlying type (default int),
                   // children = kVarDeclStmt members (is_const, optional init).
                   // Resolve lowers to alias + namespace + consts (named) or
                   // bare consts (anonymous); desugar drops it.
    kReturnStmt,
    kBlockStmt,    // { stmts } — a nested lexical scope; children = statements.
    kIfStmt,       // if (cond) then [else else]; children[0] = condition expr,
                   // [1] = then-branch (kBlockStmt), [2] = optional else-branch
                   // (kBlockStmt, or kIfStmt for `else if`).
    kWhileStmt,    // while (cond) body; children[0] = condition expr,
                   // [1] = body (kBlockStmt). Pre-condition: body may run zero
                   // times.
    kDoWhileStmt,  // post-condition while — slids `while { body } (cond);`.
                   // children[0] = condition expr, [1] = body (kBlockStmt). Body
                   // runs at least once; condition tested after each iteration.
    kForLongStmt,  // long-form for — `for (varlist) (cond) {update} {body}`.
                   // children[0] = condition, [1] = update (kBlockStmt),
                   // [2] = body (kBlockStmt), [3..] = varlist (kVarDeclStmt each).
                   // The canonical for node; other for shapes desugar TO this.
    kForEnumStmt,  // `for (var : Enum) {body}`. children[0] = loop-var decl,
                   // [1] = enum-ref (kIdentExpr), [2] = body. resolve rewrites it
                   // in place into a kForLongStmt over the enum's first..last
                   // members; never survives to constfold/classify/codegen.
    kForArrayStmt, // `for ([type] var : arr) {body}` over a fixed-size 1-D array.
                   // The grammar emits a kForEnumStmt carrier; resolve discovers
                   // the array iterable, UNDERSTANDS it in scope (loop var bound to
                   // the element, body resolved, DA run, one-dim / by-ref-match
                   // validated) and re-tags the node to this kind WITHOUT lowering.
                   // desugar lowers it to a kForLongStmt (a `_$idx` counter + a
                   // per-iteration element binding). children[0] = loop-var decl
                   // (no init — the binding is synthesized; kind flipped to
                   // kAssignStmt to flag a typeless REUSE), [1] = array ident,
                   // [2] = body (kBlockStmt).
    kForTupleStmt, // `for ([type] var : tup) {body}` over a homogeneous tuple —
                   // a literal `(7,4,2)` (spilled to a temp), an lvalue (a tuple
                   // local / `ref^` / index, iterated in place), or an rvalue call
                   // (spilled). The grammar emits a kForEnumStmt carrier; resolve
                   // discovers the tuple iterable, UNDERSTANDS it in scope (loop
                   // var bound to a slot, body resolved, DA run, homogeneity
                   // validated for non-literals) and re-tags this kind WITHOUT
                   // lowering. desugar lowers it to a kForLongStmt: a `_$idx`
                   // counter + a `_$iter` iterator that walks the slots (built as
                   // `<T[]><void^>` of the storage address, so a `ref^` iterable
                   // dodges addr-of-through-deref) + a per-iteration binding (by
                   // value `v = _$iter^`, by ref `v = _$iter`). children[0] =
                   // loop-var decl (kind flipped to kAssignStmt to flag a typeless
                   // REUSE), [1] = iterable expr, [2] = body.
    kForRangedStmt,// `for ([type] var : start .. [cmp] end [op step]) {body}` —
                   // the short ranged form, kept intact through resolve/constfold/
                   // classify (which UNDERSTAND it in scope) and lowered to a
                   // kForLongStmt by desugar. children[0] = loop-var decl (init =
                   // start), [1] = end expr, [2] = step expr (or null => +1),
                   // [3] = body (kBlockStmt). text = cmp ("<"/"<="/">"/">="/"!="),
                   // name = op ("+"/"-"/"*"/"/"/"<<"/">>"). range_dotdot_tok = the
                   // `..` token (empty-range caret); label = the loop's `:label`.
    kBreakStmt,    // break; — exits the nearest enclosing loop OR switch.
    kContinueStmt, // continue; — jumps to the nearest enclosing loop's test
                   // (switch frames are transparent to continue).
    kSwitchStmt,   // switch (value) { case C: ... default: ... }. children[0] =
                   // scrutinee expr, [1..] = kCaseClause (source order).
    kCaseClause,   // one case/default clause of a switch. children[0] = label
                   // const-expr (nullptr => the `default` clause), [1] = body
                   // (kBlockStmt; fall-through links it to the next clause).
    kStringLiteral,
    kIntLiteral,
    kUintLiteral,
    kCharLiteral,
    kBoolLiteral,
    kFloatLiteral,
    kNullptrLiteral,// `nullptr` — a typeless null pointer (internal type
                    // "anyptr"); coerces to any reference/iterator type.
    kIdentExpr,
    kUnaryExpr,    // text = op ("+", "-", "!", "~"); children[0] = operand
    kBinaryExpr,   // text = op (e.g. "+", "<<", "&&"); children[0] = lhs, [1] = rhs
    kPreIncExpr,   // text = op ("++"/"--"); children[0] = operand lvalue
    kPostIncExpr,  // text = op ("++"/"--"); children[0] = operand lvalue
    kAddrOfExpr,   // prefix `^lvalue` — address-of; yields a reference (T^).
                   // children[0] = operand lvalue.
    kDerefExpr,    // postfix `lvalue^` — dereference; yields the pointee as an
                   // lvalue. children[0] = operand (a reference/iterator).
    kIndexExpr,    // postfix `base[index]` — array subscript, an element lvalue.
                   // children[0] = base (array or a nested kIndexExpr),
                   // [1] = index expr. `a[x][y]` nests: ((a[x])[y]).
    kTupleExpr,    // anonymous tuple literal `(e0, e1, ...)` — children = slots.
                   // (size-1 collapses to the bare expr at parse; the comma marks
                   // a tuple.) inferred_type = the kTuple type from classify.
    kCastExpr,     // prefix `<Type^> operand` — pointer reinterpret cast.
                   // return_type = target type spelling; children[0] = operand.
                   // The address is unchanged; only the static type changes.
    kConvertExpr,  // `(Type=expr)` value/type conversion — assignment to a temp.
                   // return_type = target value type; children[0] = operand. Unlike
                   // kCastExpr (reinterpret bits), this CHANGES the bits (trunc /
                   // ext / fp<->int / sign reinterpret / non-null test). Chains
                   // `(A=B=expr)` nest right-to-left as one kConvertExpr per link.
    kNewExpr,      // new T / new T[n] / new(addr) T[n] — heap or placement alloc.
                   // return_type = element type T; children[0] = array-size expr
                   // (or null for a single object), [1] = placement-address expr
                   // (or null for a heap allocation). Yields T^ (single) or T[]
                   // (array). Phase 4: primitives only (no constructors).
    kSizeofExpr,   // sizeof(T) / sizeof(expr) — byte size as an `intptr`.
                   // return_type = a type-operand spelling (grammar) OR the
                   // underlying of an ident naming a type (resolve); else
                   // children[0] = the value expression whose type is measured.
                   // classify computes the size and rewrites this to kIntLiteral.
    kStringifyType,// ##type(expr) — children[0] = operand expression. classify
                   // infers the operand's type and rewrites this node in place
                   // to a kStringLiteral holding the type name; never survives
                   // classify. (The other ## macros are kStringLiteral at parse.)
    kParam,        // function parameter; name = ident, return_type = declared type
};

struct Node {
    Kind kind;
    std::string name;            // function name, callee name, variable name
    std::string text;            // literal value (string / int as text / char codepoint)
    widen::TypeRef return_type = widen::kNoType;  // function return / VarDecl declared type
    std::vector<std::unique_ptr<Node>> dim_exprs;  // kVarDeclStmt array dims that are
                                 // const-EXPRESSIONS (not literals): one slot per
                                 // array dim, null for a literal dim, the expr for
                                 // a const-expr dim. The type spelling carries a
                                 // provisional `[1]` at each; constfold folds the
                                 // expr and bakes the real size in. Empty when the
                                 // decl has no const-expr dim.
    std::vector<int> return_type_seg_toks;  // per-segment tokens of a qualified type
                                 // spelling in return_type (for precise carets);
                                 // empty for primitives / non-captured sites
    widen::TypeRef nominal_type = widen::kNoType;   // literal nodes: from constfold
    widen::TypeRef inferred_type = widen::kNoType;  // classify: expr in-context type
    widen::TypeRef op_type = widen::kNoType;        // classify: binary's computational type
    std::string alias_label;     // classify: the alias/enum NAME this expr carries as
                                 // a label (else empty). inferred_type/op_type stay
                                 // the erased underlying for width math + codegen;
                                 // this parallel label is what ##type reports. Sticky
                                 // alias+alias / alias+literal, dropped on a mismatch.
    widen::TypeRef strong_type = widen::kNoType;  // constfold: a folded/substituted literal that came
                                 // from a STRONG (typed) const carries that const's
                                 // type here (empty = weak literal). Propagated through
                                 // the fold so an inferred typeless const can tell a
                                 // strong rhs (takes its type) from a bare-literal rhs.
    int file_id = -1;            // source file of the construct
    int tok = -1;                // index into token::List::tokens for error attribution
    int name_tok = -1;           // ident token for named constructs (VarDecl, FunctionDef/Decl, Param)
    int resolved_entry_id = -1;  // classify: ident / lhs / callee -> Tree::entries index
    int range_dotdot_tok = -1;   // kForRangedStmt (and the kForLongStmt an enum-for
                                 // lowers to): the `..` token (>= 0 marks
                                 // range-derived; the caret for the "Invalid
                                 // range." empty-range check)
    // A loop's explicit `:label` (empty = the keyword default for/while), parsed
    // after the body. On a kBreakStmt/kContinueStmt: `text` holds a numbered
    // argument (digits), `name` a named argument (label, incl. the for/while
    // keyword); both empty = naked. resolve stamps loop_levels = hops outward in
    // the loop/switch context stack to the resolved target (0 = innermost),
    // consumed by codegen; -1 until resolved.
    std::string label;
    int loop_levels = -1;
    bool is_const = false;       // kVarDeclStmt: declared with leading `const`
    bool move_init = false;      // kVarDeclStmt: initialized with `<--` (a move),
                                 // so desugar nulls the init's pointer leaves
    bool quiet_diag = false;     // kStringifyType inside a `#x` desugar: the same
                                 // operand is also resolved by the sibling `^x`, so
                                 // suppress THIS arm's undefined-operand diagnostic
                                 // (the `^x` arm reports the real error once).
    bool require_homogeneous = false; // kVarDeclStmt: a for-tuple LITERAL spill
                                 // temp — classify rejects a non-homogeneous
                                 // inferred tuple (the iterator strides by slot 0).
    bool non_completing = false; // while/do-while/for-long: a constant-true
                                 // condition with no escaping break — the loop
                                 // never exits. Set in resolve (Abrupt completion
                                 // / unreachable-after), read in classify
                                 // (endsInReturnNode: a return-terminator).
    // Qualified name (ident / call / inline decl / bare alias): leading namespace
    // segments before `name`. `Space:Nested:kFour` -> qualifier {Space, Nested},
    // name kFour. `global_qualified` marks a leading `::` (global root). Consumed
    // by resolve, which rewrites the node to a plain resolved reference.
    std::vector<std::string> qualifier;
    std::vector<int> qualifier_toks;   // token index per qualifier segment (carets)
    bool global_qualified = false;
    std::vector<std::unique_ptr<Node>> children;
    std::vector<std::unique_ptr<Node>> params;   // kFunctionDef/Decl: kParam nodes
    std::vector<widen::TypeRef> param_types;     // kCallStmt/kCallExpr: classify-cached resolved fn's param types
    // A NESTED function (kFunctionDef in a body) and each call to it carry the
    // entry ids of the enclosing-function locals/params it captures — passed
    // by reference (the host alloca's address) when the nested function is
    // lifted to a top-level function in codegen. capture_types (on the
    // kFunctionDef, parallel to captures) is each captured var's slids type, for
    // emitting the lifted function's by-ref params.
    std::vector<int> captures;
    std::vector<widen::TypeRef> capture_types;
};

enum class EntryKind {
    kFunction,
    kLocalVar,
    kConst,
    kAlias,        // type alias; slids_type = target spelling (may be another alias)
    kNamespace,    // namespace name; ns_frame_id identifies its member set
};

struct Entry {
    EntryKind kind;
    std::string name;
    widen::TypeRef slids_type = widen::kNoType;  // LocalVar / Const: declared type;
                                  // Function: return type; Namespace: kNoType, or the
                                  // underlying type when it is an enum's
                                  // namespace facet (transparent type alias).
    std::string alias_label;      // LocalVar / param: the as-declared alias/enum
                                  // spelling when the declared type was a named
                                  // type (else empty). slids_type holds the erased
                                  // underlying; this is what ##type(var) reports.
    std::vector<widen::TypeRef> param_types;  // Function only
    // Function only — default parameters. num_required = count of leading params
    // without a default (required); params [num_required..param_types.size()) are
    // optional. param_default_text/kind (parallel to param_types) carry each
    // optional param's folded constant default — captured by classify's
    // signature pre-pass; required slots are empty / kProgram.
    int num_required = 0;
    std::vector<std::string> param_default_text;
    std::vector<Kind> param_default_kind;
    std::vector<int> captures;    // kFunction (nested): captured host entry ids
    int parent_frame_id = -1;
    int file_id = -1;
    int tok = -1;
    bool defined = false;         // Function: true once a body has been seen
    int def_file_id = -1;         // Function: source of the first definition
    int def_tok = -1;             // Function: token of the first definition (for
                                  // "first defined here"; distinct from tok, which
                                  // is the first *declaration* for "first declared here")
    // kNamespace: identity of its member set (members carry owner_ns_frame ==
    // this). A persistent id, distinct from any lexical frame; reopens reuse it.
    int ns_frame_id = -1;
    // Members (kConst / kFunction / kNamespace declared inside a namespace):
    // the ns_frame_id of the owning namespace. -1 for ordinary (non-member)
    // entries. The entry still lives at parent_frame_id for lifetime.
    int owner_ns_frame = -1;
    // kConst — filled by constfold; substitution at use sites reads these.
    std::string literal_text;     // canonical-precision text at declared type
    Kind literal_kind = Kind::kProgram;  // sentinel; valid after constfold capture
    widen::TypeRef const_strong_type = widen::kNoType;  // kConst: the const's STRONG type (kNoType = weak,
                                    // i.e. a named literal). Explicit-typed consts and
                                    // typeless consts inferred from a strong rhs are
                                    // strong; a typeless const from a bare literal is
                                    // weak. Substitution stamps it onto the literal.
};

// A class's field layout — the named-tuple half of "a class is a namespace + a
// named tuple". Built by resolve from a kClassDef node, keyed by class name in
// Tree::classes. Parse-side only: classify reads it to type field access and to
// fill defaults at construction; desugar reads it to map a field name to its
// slot index. Codegen never needs it (the layout also rides on the kSlid type).
struct ClassInfo {
    std::string name;
    std::vector<std::string> field_names;
    std::vector<widen::TypeRef> field_types;
    // The kParam node for each field (STABLE across stages). Its children[0] is
    // the author default expression, read LIVE at construction time — constfold
    // may REPLACE a default node in place (folding `BASE+5`->`15`), so a cached
    // pointer to the default itself would dangle; the kParam node does not move.
    std::vector<Node*> field_params;
    widen::TypeRef type = widen::kNoType; // the interned kSlid{name, slots}
    int def_file_id = -1;                // the class def's location (for the
    int def_tok = -1;                    // "first defined here" dup note)
    bool needs_ctor = false;             // layout: false in the trivial bucket
    bool needs_dtor = false;
    int fieldIndex(std::string const& f) const {
        for (std::size_t i = 0; i < field_names.size(); ++i)
            if (field_names[i] == f) return static_cast<int>(i);
        return -1;
    }
};

struct Tree {
    std::vector<std::unique_ptr<Node>> nodes;

    // Symbol table — populated by classify, consumed by later stages.
    std::vector<Entry> entries;
    int next_frame_id = 0;

    // Class layouts by name (see ClassInfo). Populated by resolve's class
    // pre-pass; read by classify + desugar.
    std::map<std::string, ClassInfo> classes;

    // Transient — while resolving a ctor/dtor body: the field names of the
    // enclosing class. An UNRESOLVED bare name matching one is rewritten to
    // `self^.field` (locals shadow fields, so this is only a fallback). nullptr
    // outside a class member body.
    std::vector<std::string> const* method_fields = nullptr;

    // Transient scope state — valid only during classify's run.
    std::vector<int> frame_id_stack;
    std::vector<std::size_t> frame_entries_start_stack;
    std::vector<int> live_entry_ids;
    // Transient — valid during resolve's run. The set of namespace frames whose
    // members are reachable unqualified at the current point (the open-namespace
    // chain plus any `alias Ns;` imports in scope).
    std::vector<int> open_ns_frames;
    // Transient — valid during resolve's body walk. Entry ids of kLocalVar
    // entries that are definitely initialized at the current point (params +
    // any local that a decl-with-init or an assignment has written). Reading a
    // kLocalVar absent from this set is "use of uninitialized variable".
    std::set<int> initialized_locals;
    // Transient — kLocalVar entry ids of ARRAY locals written at least once on
    // SOME prior path. Whole-array definite-assignment is a may-analysis: an
    // array can't be fully initialized in one statement (no initializer lists),
    // and a fill loop's element writes wouldn't survive the must-set's loop
    // join. So an array read requires only that some earlier subscript write
    // exists (monotonic, never rolled back) — reading before ANY write errors.
    std::set<int> assigned_arrays;
    // Transient — kLocalVar entry ids that have been READ at least once in the
    // current body (value-position use via resolveExpr). Drives the unused-local
    // sweep at end of body.
    std::set<int> read_locals;
    // Transient — kLocalVar entry ids DECLARED in the current body (not params),
    // in declaration order. At end of body, any not in read_locals is unused:
    // "set but never used" if also in initialized_locals, else "unused".
    std::vector<int> body_locals;
    // Transient — one frame per enclosing loop during resolve's body walk; empty
    // outside any loop (a break/continue there is an error). Each frame
    // accumulates the definite-assignment init-set at every break / continue
    // targeting that loop, INTERSECTED (a "must" join); `*_seen` distinguishes
    // "no break/continue yet" (top, the ∩-identity) from "intersected to empty".
    // A do-while consumes them (body runs once, so its inits escape, but a break
    // can shrink the after-set and a continue the condition's reachable reads);
    // a pre-condition while ignores them (after = entry set S regardless).
    struct LoopFrame {
        std::set<int> break_init;
        bool break_seen = false;
        std::set<int> continue_init;
        bool continue_seen = false;
        bool is_switch = false;   // a switch frame: a break target, but transparent
                                  // to continue (continue skips it to the nearest loop)
        std::string name;         // loop frame: its label (explicit `:name` or the
                                  // keyword default for/while); empty for switches.
                                  // Named break/continue match this; numbered count
                                  // loop frames (is_switch=false) outward.
    };
    std::vector<LoopFrame> loop_stack;
    // Transient — while resolving a NESTED function body: capture_floor is the
    // nested function's frame id (a kLocalVar resolved in a frame between the
    // global frame and the floor is a capture); capture_node points at the
    // nested kFunctionDef whose `captures` list is being built.
    int capture_floor = -1;
    Node* capture_node = nullptr;
    // Transient — host-level calls to nested functions, checked after the host
    // body resolves (captures are known by then): each captured host var must be
    // definitely-assigned at the call (snapshot of initialized_locals).
    struct NestedCallCheck {
        int entry;
        std::set<int> snapshot;
        int file_id;
        int tok;
    };
    std::vector<NestedCallCheck> nested_call_checks;
    // Transient — set while resolving a long-for's UPDATE clause, which may not
    // break / continue / return. in_for_update is true throughout the update
    // subtree (guards return, transitively). for_update_floor is loop_stack.size()
    // at update entry: a break/continue is illegal when loop_stack hasn't grown
    // past it (i.e. it targets the for, not a nested loop introduced inside the
    // update). Both saved/restored around the update walk.
    bool in_for_update = false;
    int  for_update_floor = -1;
};

// Symbol-table APIs. All storage + walking lives here; classify only decides
// what to add and what to look up.
int  pushFrame(Tree& t);                                  // returns new frame id
void popFrame(Tree& t);
int  allocFrameId(Tree& t);                               // id only, no push (ns identity)
int  currentFrameId(Tree const& t);
int  addEntry(Tree& t, Entry e);                          // returns entry id
int  findInLiveScopes(Tree const& t, std::string const& name);   // -1 if none
int  findInFrame(Tree const& t, int frame_id, std::string const& name);
widen::TypeRef entryType(Tree const& t, int entry_id);

}  // namespace parse
