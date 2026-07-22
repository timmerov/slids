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
    kMethodCallStmt, // obj.method(args); — name = method, children[0] = receiver
                     // lvalue, children[1..] = args. Desugar lowers to a normal
                     // call of the lifted <Class>__method with `^receiver` prepended.
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
    kGlobalScopeStmt, // `global;` — opens the global lifetime for its enclosing
                   // scope; at that scope's exit the lazy-global dtor registry runs.
                   // Auto-inserted at the top of `main` when absent.
    kSwitchStmt,   // switch (value) { C: {..} default: {..} }. children[0] =
                   // scrutinee expr, [1..] = kCaseClause (source order).
    kCaseClause,   // one clause of a switch: a label-list + a body block.
                   // children[0..n-2] = labels (each a const-expr; nullptr => a
                   // `default` label), children.back() = body kBlockStmt. text ==
                   // "continue" marks a trailing fall-through into the next clause
                   // (else the clause exits the switch at its body's `}`).
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
    bool const_method = false;   // kFunctionDef: a `const` between the return type and the
                                 // method/operator name (`Ret const m(...)`) — const receiver.
                                 // PARSE-ONLY today; the const-self semantics are deferred.
    bool is_global = false;      // kVarDeclStmt: a GLOBAL variable (`global T x=…;`
                                 // or a bare file-scope decl) — mutable static
                                 // storage; resolve registers it as kGlobalVar.
    int global_group_id = -1;    // >=0: a member var-decl / ctor / dtor dissolved from
                                 // a LAZY anonymous group (resolve explodes it into
                                 // bare siblings); collectGlobals rebuilds the shared
                                 // sentinel/ctor/dtor GlobalGroup keyed by this id.
    bool is_reopen = false;      // kClassDef: a RE-OPEN of an existing class (resolve
                                 // points resolved_entry_id at the primary's entry; the
                                 // class BODY passes skip this node). A re-open adds only
                                 // body members, EXCEPT an INCOMPLETE class (see
                                 // is_incomplete), where a re-open may append fields.
    bool is_incomplete = false;  // kClassDef: the field tuple ends with a trailing `...`
                                 // — an INCOMPLETE class. A later same-scope re-open may
                                 // append fields until one CLOSES it (a re-open whose
                                 // field tuple omits the trailing `...`).
    bool is_construction = false; // kCallStmt/kCallExpr: a `Class(args)` nameless
                                 // class construction (target resolved to a kClass),
                                 // NOT a function call. resolve sets it; classify
                                 // builds the per-field construction tuple; desugar
                                 // lowers it to a synthetic kVarDeclStmt.
    bool is_temp_init = false;   // kConvertExpr: a `Type(value)` NAMELESS PRIMITIVE
                                 // TEMPORARY — bound by the DECL-INIT rules (fit-check,
                                 // no implicit narrow), NOT the truncating conversion
                                 // grid. The primitive twin of `Class(args)`.
    bool ctor_no_args = false;   // kCallExpr[is_construction]: the source named NO ctor
                                 // arguments (`Class` / `Class()`), so the object is
                                 // DEFAULT-constructed. Read by the operator-chain lowering:
                                 // a DEFAULT head may be seeded with the cheaper `op=`, but a
                                 // head built WITH args must use `op<OP>=` — an `op=` there
                                 // would discard the very arguments it was constructed with.
    bool class_op_chain = false; // kBinaryExpr / kUnaryExpr: a class-PRODUCING operator.
                                 // classify RESOLVES it and STAMPS it (the candidate ids below +
                                 // inferred_type = the result class = the OPERAND's class — the
                                 // LHS operand of a binary) but does NOT lower it — desugar's
                                 // chain lowering builds the
                                 // accumulator and the op calls, because eliding the temp into
                                 // the destination needs the chain AND its destination together
                                 // and only a whole statement has both. The last child is the
                                 // result class's DEFAULT field-init tuple (the accumulator's
                                 // construction value), parked here by classify: children[2] on
                                 // a binary [lhs, rhs, tuple], children[1] on a UNARY
                                 // [operand, tuple]. A unary runs exactly one operator
                                 // (op_un_eid) — it produces the accumulator's whole value — so
                                 // the seed / fuse / 2-arg ids below are unused on it.
    bool op_collapse_head = false; // kBinaryExpr[class_op_chain]: the head CONSTRUCTION is the
                                 // accumulator itself (built into it, never an operand — one
                                 // temp saved). THE ROLE IS DECIDED HERE, ONCE, and desugar
                                 // OBEYS it: the collapse is an OPTIMIZATION, and when it does
                                 // not fit (a head built WITH args needs `op<OP>=`, and the
                                 // class may only have the 2-arg `op<OP>`) classify declines it
                                 // and the construction becomes an ORDINARY rvalue operand.
                                 // desugar used to RE-DERIVE the role from the AST
                                 // (lhs.is_construction), so it could not be told.
    int op_bin_eid = -1;         // 2-arg `op<OP>(lhs, rhs)`  — the head pair in one call
    int op_un_eid = -1;          // 1-arg `op<OP>(operand)`   — an arity-1 UNARY produce-self
    int op_aug_eid = -1;         // 1-arg `op<OP>=(rhs)`      — the fuse
    int op_eq_lhs_eid = -1;      // 1-arg `op=(lhs)`          — the seed of a decomposed head
    int op_eq_rhs_eid = -1;      // 1-arg `op=(rhs)`          — the seed of a COLLAPSED head
    int op_move_eid = -1;        // 1-arg `op<--(Self^)`      — the move INTO a live target.
                                 // Resolved here so desugar can CALL the operator by name.
                                 // A transfer synthesized after classify has no other way to
                                 // reach it: a bare move node would rely on codegen's funnel
                                 // to dispatch, and a whole-class transfer must ALWAYS run
                                 // the class's move function, never a blit twin.
    bool parenless = false;      // kCallStmt: a bare `Name;` statement (no `()`). Only
                                 // valid when Name is a class — a parenless default
                                 // construction (`Name` == `Name()`); resolveCallTarget
                                 // rejects a parenless non-class so `f;` is not a call.
    bool class_conversion = false; // kConvertExpr: a `(Class = src)` conversion whose
                                 // TARGET is a class — classify rewrites its children to
                                 // [default-construct `_$cret` decl, `_$cret.op=(src)`
                                 // call] and stamps resolved_entry_id = the `_$cret` id;
                                 // desugar lifts the two into a `_$cret` temp (an rvalue
                                 // via op=), the class analog of a construction lift.
    bool agg_conv_spill = false; // kConvertExpr: an AGGREGATE conversion whose source
                                 // was side-effecting, so classify spilled it once into a
                                 // `_$cinit` temp — children = [spill decl, the per-slot
                                 // tuple that indexes the temp]. desugar hoists the spill
                                 // decl and yields the tuple (like a construction lift).
    bool is_mutable = false;     // kParam: declared with leading `mutable` — opts OUT
                                 // of the default const-pointee munge (mungeParamType)
    bool is_virtual = false;     // kFunctionDef/kFunctionDecl (a class method or the
                                 // `_$dtor`): declared with leading `virtual` — dispatched
                                 // through the vtable. A class with >=1 virtual method is
                                 // a virtual class.
    bool is_pure = false;        // kFunctionDecl: a pure virtual (`virtual T m() = delete;`)
                                 // — no body; a class with a pure method is abstract (not
                                 // instantiable). Always accompanies is_virtual.
    bool is_foreign = false;     // kFunctionDecl: a foreign C function (`T f(...) = import;`
                                 // or inside an `import { }` block) — no slids body, its
                                 // symbol is the BARE C name (no mangling), bound at link.
    bool bypass_virtual = false; // kMethodCallStmt/kMethodCallExpr: a `Base:method()`
                                 // qualified call — resolve reframed the receiver to the
                                 // base subobject and this call STATICALLY targets that
                                 // ancestor's method (skips vtable dispatch).
    bool default_move_init = false;      // kVarDeclStmt: initialized with `<--` (a move),
                                 // so desugar nulls the init's pointer leaves
    bool default_swap_init = false;      // kVarDeclStmt: initialized with `<-->` (a swap);
                                 // classify default-constructs then re-dispatches as a
                                 // kSwapStmt (never reaches desugar as a var-decl)
    bool construction_init = false;      // kVarDeclStmt: the `Type name(args)` paren-init
                                 // form (vs `= init`) — a FIELD-LIST construction that
                                 // is ALWAYS built in place, never value-init. Both forms
                                 // lower the args to a kTupleExpr, so this flag is the only
                                 // thing that tells `Class c(1,2,3)` (field-list) from
                                 // `Class c = (1,2,3)` (tries a tuple op= first).
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
    // Function TEMPLATES. On a kFunctionDef, a non-empty type_params marks a TEMPLATE
    // definition (`T add<T>(T a, T b)`): its body stays in pristine parse state — every
    // stage skips it — until a call site binds the type-list and classify clones +
    // instantiates it. On a kCallStmt/kCallExpr, tmpl_args carries the EXPLICIT
    // `<type-list>` arguments (parse-interned spellings; resolve resolves them in scope).
    std::vector<std::string> type_params;
    std::vector<int> type_param_toks;            // token per template-list name (carets)
    std::vector<widen::TypeRef> tmpl_args;
    std::vector<int> tmpl_arg_toks;
    std::vector<widen::TypeRef> param_types;     // kCallStmt/kCallExpr: classify-cached resolved fn's param types
    // A NESTED function (kFunctionDef in a body) and each call to it carry the
    // entry ids of the enclosing-function locals/params it captures — passed
    // by reference (the host alloca's address) when the nested function is
    // lifted to a top-level function in codegen. capture_types (on the
    // kFunctionDef, parallel to captures) is each captured var's slids type, for
    // emitting the lifted function's by-ref params.
    std::vector<int> captures;
    std::vector<widen::TypeRef> capture_types;
    // A method / ctor / dtor body's `self` — an address-aliased local of the
    // class type, whose storage IS the target of the implicit `_$recv` pointer.
    // resolve registers it (so `self` / `self.field` / `^self` resolve as an
    // ordinary local); codegen binds its address to `_$recv^` at the prologue.
    // -1 for a free function (no receiver).
    int self_entry_id = -1;
};

enum class EntryKind {
    kFunction,
    kLocalVar,
    kConst,
    kGlobalVar,    // a file-local GLOBAL variable — mutable static storage (an
                   // `internal @`-global), reached like a namespace member. Unlike
                   // kConst it is NOT substituted; unlike kLocalVar it has static
                   // (not stack) storage. slids_type = declared/inferred type;
                   // literal_text/literal_kind carry the folded static initializer.
    kAlias,        // type alias; slids_type = target spelling (may be another alias)
    kNamespace,    // namespace name; ns_frame_id identifies its member set
    kClass,        // class name; a type (slids_type = its kSlid) AND a namespace
                   // (ns_frame_id holds its member aliases/consts/enums).
    kField,        // a CLASS FIELD, registered as a transient lexical entry in a
                   // method body's field frame (so a bare `f` resolves like a local and
                   // is shadowed by a same-named body local). A reference to one is
                   // lowered to `_$recv^.f` (field_depth base hops) at resolution.
                   // slids_type = the field's type; NOT storage (never a store target
                   // directly — the lowering turns it into a field access first).
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
    bool synthesized = false;     // kFunction: a COMPILER-synthesized default op=/op<--/
                                  // op<-->(Self^) (not user-written). Resolves like any
                                  // operator, but is ELIDABLE — from a class RVALUE source
                                  // a binding builds in place rather than materialize +
                                  // default-copy (a user op wins over elision instead).
    bool is_virtual = false;      // kFunction (a class method): declared `virtual` —
                                  // dispatched through the vtable. Copied from the
                                  // method's parse::Node is_virtual at registration.
    bool is_pure = false;         // kFunction: a pure virtual (`= delete`) — no body,
                                  // never an orphan; a class with an un-overridden pure
                                  // method is abstract (not instantiable).
    bool is_foreign = false;      // kFunction: a foreign C function (`= import` / an
                                  // `import { }` block) — no slids body, defined by a C
                                  // library (linked, e.g. `-lm`), so undefined here is NOT
                                  // an orphan. symbolFor emits the BARE C name (no mangle).
    bool is_template = false;     // kFunction: a function TEMPLATE — param_types hold
                                  // PATTERNS (type-param leaves, def_id ==
                                  // widen::kTmplParamDefId), the body is unresolved, and
                                  // no call targets it directly: classify instantiates on
                                  // demand. A template owns its name — any same-scope
                                  // same-name function (or template) is a compile error.
    std::vector<widen::TypeRef> tmpl_args;  // kFunction: an INSTANCE's bound type args in
                                  // template-list order. Non-empty marks an instance:
                                  // excluded from overload-candidate gathering (it shares
                                  // the template's name), and symbolFor appends the
                                  // Itanium `I..E` encoding so instances get distinct
                                  // symbols even with identical parameter lists.
    int alias_of = -1;            // kFunction: a FUNCTION ALIAS (`alias sin = sinf;`) — a
                                  // duplicate of the target overload registered under the
                                  // alias name so it joins the overload set; it emits the
                                  // TARGET's symbol (symbolFor follows this to that entry).
    bool is_external = false;     // kFunction: DECLARED in an imported `.slh` header —
                                  // its definition legitimately lives in another
                                  // translation unit (linked in), so being undefined
                                  // in THIS unit is not an orphan. Codegen emits a
                                  // `declare` for it when it is called (see the ast
                                  // Node.external_decl flag it feeds).
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
    // kField: the number of `_$base` hops from the method's own class to the class that
    // declares this field (0 = an own field -> `_$recv^.f`; N -> `_$recv^._$base…f`).
    int field_depth = 0;
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
    std::string name;          // the bare source name (a local class disambiguates
                               // by its type's def_id, not by its name).
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
    // INCOMPLETE class: `is_open` is true while the class still carries a trailing
    // `...` (a re-open may append fields); it flips false at the closing re-open.
    // `pending_fields` points at the field kParams appended by open re-opens (still
    // OWNED by their re-open node, so the body phase resolves their default exprs with
    // the class frame open). registerClassBody interns them onto the layout after the
    // primary's own fields — the whole feature freezes the layout in ONE place
    // (single-file: all appends are seen in Phase 1 before Phase 2 interns).
    bool is_open = false;
    // PERSISTENT (unlike is_open, which the closing re-open clears): the class was declared
    // incomplete at some opening (a trailing `...`). Stays true after completion, so the
    // COMPLETER still knows the class is opaque to importers. With a header declaration this
    // makes the class OPAQUE cross-TU (widen::Type::opaque).
    bool declared_incomplete = false;
    std::vector<Node*> pending_fields;
    // The IMPLICITLY-INVOKED members contributed by RE-OPENS — ctor, dtor, copy, move,
    // swap (still OWNED by their re-open node, like pending_fields). A class's lifecycle
    // is the union of every opening — "as if everything was declared within a single
    // class definition" — but a re-open node skips the class BODY passes, so these would
    // otherwise be invisible to registerClassBody, which is the only place that sees the
    // whole class and so is where every per-CLASS lifecycle question is answered (the
    // hook scan: has_ctor false -> the hook is never called and its `__impl` is emitted
    // dead; and the header-class ADD check).
    std::vector<Node*> pending_hooks;
    int fieldIndex(std::string const& f) const {
        for (std::size_t i = 0; i < field_names.size(); ++i)
            if (field_names[i] == f) return static_cast<int>(i);
        return -1;
    }
};

// A registered function TEMPLATE: the pristine definition node plus the resolve-state
// snapshot needed to re-enter resolution at the definition point when classify demands
// an instance. Resolve's scope state (frames, live entries, open namespaces,
// definite-assignment) is transient to its run, so it is captured at the point the
// template's body WOULD have resolved — which gives the body the same visibility an
// ordinary function body has (all forward refs included).
struct TemplateInfo {
    Node* def = nullptr;               // the kFunctionDef, body in pristine parse state
    std::vector<std::unique_ptr<Node>>* host_list = nullptr;  // the statement/member list
                                       // holding `def`; instances splice in here (at the
                                       // END of classify — a mid-walk splice would
                                       // invalidate the walker's iterators)
    bool nested = false;               // defined in a body scope: instances are nested
                                       // functions (captures, lifted symbol)
    bool snapshot_taken = false;
    // A METHOD template's body resolves under the class-member context strings
    // resolveScopeBodies sets around members (base-qualified refs read them);
    // captured with the scope snapshot, installed at instantiation.
    std::string current_class_name;
    std::string current_base_name;
    // ALIAS templates (`alias Ref<T> = T^;`) ride this same table (keyed by their
    // kAlias entry): only `def` and this flag are used — a type expansion needs no
    // snapshot or instance list. True once the TARGET has been resolved into the
    // entry's PATTERN (lazily at first use, or at the validate pass).
    bool pattern_built = false;
    std::vector<int> frame_id_stack;
    std::vector<std::size_t> frame_entries_start_stack;
    std::vector<int> live_entry_ids;
    std::vector<int> open_ns_frames;
    std::set<int> initialized_locals;
    std::set<int> assigned_arrays;
    std::map<std::vector<widen::TypeRef>, int> instances;  // bound types -> instance entry
};

struct Tree {
    std::vector<std::unique_ptr<Node>> nodes;

    // Symbol table — populated by classify, consumed by later stages.
    std::vector<Entry> entries;
    int next_frame_id = 0;

    // Function templates, keyed by the template's entry id. Filled at resolve
    // (registration + snapshot); consumed by classify's on-demand instantiation.
    std::map<int, TemplateInfo> templates;
    // Instance kFunctionDef nodes minted during classify's walk, each waiting to be
    // spliced into its host list once the walk is over. Spliced right AFTER the
    // template's own definition node (`after`) — never at the list's end, where a
    // statement after a `return` would break trailing-return analysis.
    struct PendingInstance {
        std::vector<std::unique_ptr<Node>>* host_list = nullptr;
        Node* after = nullptr;              // the template definition node
        std::unique_ptr<Node> node;
    };
    std::vector<PendingInstance> pending_tmpl_instances;
    int tmpl_instantiation_depth = 0;   // runaway `f<T>` -> `f<T^>` recursion guard

    // CLASS-template instances. The instance's NAME/TYPES/needs phases run at the
    // triggering use (the type must exist there), but its BODY defers: during resolve
    // to the end-of-resolve drain (where file-scope visibility is complete), so each
    // entry carries what the drain needs to re-enter — the template (snapshot), the
    // bound args (the T re-bind), and the instance entry (the self-name re-bind).
    // An instance minted AFTER resolve (a class template used inside a function
    // template's body, instantiated at classify) resolves its body synchronously
    // (body_resolved = true); classify runs the late stages over it. Either way the
    // node splices into host_list after `after`, exactly like a function instance.
    struct PendingClassInstance {
        std::vector<std::unique_ptr<Node>>* host_list = nullptr;
        Node* after = nullptr;              // the template definition node
        std::unique_ptr<Node> node;
        int tmpl_entry_id = -1;
        int instance_entry_id = -1;
        std::vector<widen::TypeRef> args;
        // The instance's MEMBER entries (methods, consts, enum members, nested
        // classes) registered at instantiation. The drain installs the
        // template's snapshot — whose live set predates the instance — so these
        // are re-appended there for the body's bare member references.
        std::vector<int> member_entries;
        bool body_resolved = false;
    };
    std::vector<PendingClassInstance> pending_class_instances;
    int class_instance_total = 0;   // lifetime count across all class templates. A
                                 // runaway that recurses through METHOD BODIES
                                 // (`Way<T>` declaring a `Way<T^>`) reaches the
                                 // drain FLAT — every instantiation at depth 1 —
                                 // so the nesting guard never fires; this cap does.
    bool resolve_done = false;   // flips at the end of resolve::run — a later class
                                 // instantiation (from classify) resolves its body
                                 // synchronously instead of queueing for the drain
    // While a CLASS-template instance's phases run, the template's bare name
    // means THE INSTANCE — the receiver (`Vec^`), a self-typed member, a
    // self-construction. Needed as a stack (an instance body may demand another
    // instance) and as an explicit map: a NAMESPACE-member template's name
    // resolves through the open-ns chain to the TEMPLATE entry, over any
    // transient frame alias.
    struct TmplSelf { int tmpl_entry; int instance_entry; };
    std::vector<TmplSelf> tmpl_self_stack;

    // Indexed by file_id (token::List file order): true if that file was pulled
    // in via `import` (a `.slh` header — token::File::imported_by != -1). Filled
    // by grammar from the token list. Consumed by resolve to mark a declaration
    // whose file is imported as external (its definition is in another TU).
    std::vector<bool> file_imported;

    // Indexed by file_id: true if that file is an imported header whose BASE NAME
    // matches the primary source's (`library.slh` <-> `library.sl`) — i.e. THIS TU is
    // that header's SIBLING implementation. A module's identity is its base name, so the
    // two may live in different directories. The sibling is the ONE TU that emits a
    // header class's SYNTHESIZED symbols (complete ctor/dtor, default copy/move/swap);
    // every other importer only declares them. Hence every `.slh` needs a sibling `.sl`,
    // even for a class with no methods at all — nothing else would ever define them.
    // (A member the author WROTE is not this: it may be defined in ANY .sl, so one header
    // can declare several classes that each have their own source file.)
    std::vector<bool> file_sibling;

    // Class layouts keyed by the class's interned kSlid handle (which is unique
    // per definition via def_id — see ClassInfo). Populated by resolve's class
    // pre-pass; read by classify + desugar.
    std::map<widen::TypeRef, ClassInfo> classes;

    // The immediate base class name while resolving a DERIVED class member body, else
    // empty. A qualified `Base:member` whose qualifier is this base reframes `self` to
    // the base sub-object (slot 0): `Base:member` -> `self._$base.member`.
    std::string current_base_name;
    // The OWN class name while resolving a class member body, else empty. A qualified
    // `Self:method()` (own-class qualifier) is a static dispatch bypass with `self`
    // unchanged (0 base hops) — the self-referential twin of `current_base_name`.
    std::string current_class_name;

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
    // A FUNCTION ALIAS (`alias name = target;` whose target is a function) recorded at
    // name registration; processed AFTER types resolve (the target's signature must be
    // final before it is copied). Each duplicates the target's overloads under `name`.
    struct FuncAliasReq {
        std::string name;
        std::string target;
        int frame;
        int file_id;
        int tok;
    };
    std::vector<FuncAliasReq> func_alias_reqs;
    // Every name declared as a function ANYWHERE (scope-blind) — lets alias registration
    // tell a FUNCTION alias (`alias sin = sinf`) from a TYPE alias before the target's
    // frame is known, so a function alias never mints a (colliding) kAlias entry.
    std::set<std::string> all_function_names;
    // Transient — set while resolving a long-for's UPDATE clause, which may not
    // break / continue / return. in_for_update is true throughout the update
    // subtree (guards return, transitively). for_update_floor is loop_stack.size()
    // at update entry: a break/continue is illegal when loop_stack hasn't grown
    // past it (i.e. it targets the for, not a nested loop introduced inside the
    // update). Both saved/restored around the update walk.
    bool in_for_update = false;
    int  for_update_floor = -1;
};

constexpr int kGlobalFrame = 0;   // the file/program scope frame id

// Symbol-table APIs. All storage + walking lives here; classify only decides
// what to add and what to look up.
int  pushFrame(Tree& t);                                  // returns new frame id
void popFrame(Tree& t);
int  allocFrameId(Tree& t);                               // id only, no push (ns identity)
int  currentFrameId(Tree const& t);
int  addEntry(Tree& t, Entry e);                          // returns entry id
int  findInFrame(Tree const& t, int frame_id, std::string const& name);
// A namespace/class MEMBER by name in its owning frame (owner_ns_frame == ns_frame;
// at kGlobalFrame, a file-scope non-member). Returns entry id or -1.
int  findMemberDeclared(Tree const& t, int ns_frame, std::string const& name);
// Signature equality of two method parameter lists, skipping param 0 (`_$recv`) — the
// receiver differs by class between a base method and its override, so it is not part of
// the user-visible signature. THE single decode of "same method signature".
bool userParamsEqual(std::vector<widen::TypeRef> const& a,
                     std::vector<widen::TypeRef> const& b);
// The method entry in `ns_frame` matching (name, user-params), other than `exclude`
// (-1 for none); -1 if no match. The overload-aware sibling of findMemberDeclared.
int  findMethodInFrame(Tree const& t, int ns_frame, std::string const& name,
                       std::vector<widen::TypeRef> const& params, int exclude);
// True if `info` declares a field named `name`. The synthetic base slot `_$base`
// is included, but a user field name never collides with it (the `_$` prefix is
// reserved), so callers checking a user name need no special-case.
bool classHasField(ClassInfo const& info, std::string const& name);
// The base class TYPE of a DERIVED class — its unnamed first field `_$base` (slot 0),
// stripped; kNoType for a non-derived class. THE single decode of the `_$base` slot-0
// inheritance convention — every base-chain walk (resolve + classify) steps through it
// rather than re-spelling `field_names[0] == "_$base"`. baseTypeOf decodes a ClassInfo
// directly (no map lookup); classBaseType is the lookup-then-decode for a TypeRef.
widen::TypeRef baseTypeOf(ClassInfo const& info);
widen::TypeRef classBaseType(Tree const& t, widen::TypeRef cls);
// True for a ROOT virtual class carrying its own hidden `_$vptr` at slot 0 (the vtable
// pointer). Derived virtual classes inherit it via `_$base` and have no `_$vptr`.
bool hasVptr(ClassInfo const& info);
// A class's own member frame + every transitive base frame (most-derived first), for
// member lookup / scope-opening that sees inherited members. The shared chain iterator.
std::vector<int> classAndBaseFrames(Tree const& t, widen::TypeRef cls);
// The kClass entry for a class TYPE (slids_type match) / for its member FRAME
// (ns_frame_id match). The one place a class type <-> its entry is bridged.
int  classEntryForType(Tree const& t, widen::TypeRef classType);
int  classEntryForFrame(Tree const& t, int ns_frame);
// A class TYPE -> its own member frame (ns_frame_id); -1 if not a registered class.
// The class->frame bridge (inverse of classEntryForFrame), built on classEntryForType.
int  classNsFrame(Tree const& t, widen::TypeRef cls);
widen::TypeRef entryType(Tree const& t, int entry_id);

// The IMPLICITLY-INVOKED members, by member name: the constructor, the destructor, and
// the copy / move / swap operators. What sets these five apart from every other method
// is that a call to one is emitted WITHOUT the author naming it — from a declaration
// going out of scope, from a `=`, from a slot-wise transfer. THE single spelling of the
// list; `noun` is the phrase a diagnostic uses ("a constructor"), "" for a non-member.
bool        isImplicitMember(std::string const& name);
char const* implicitMemberNoun(std::string const& name);

// Build the implicit method-receiver param `_$recv` of the given (already-interned)
// type, stamped at `file_id`/`tok`. THE one construction of the receiver-param node —
// the in-class method form and the ctor/dtor form (grammar) and the out-of-line
// relocation (resolve) all splice this same node in at params[0].
std::unique_ptr<Node> makeReceiverParam(widen::TypeRef type, int file_id, int tok);

// THE canonical walk over a class and its HOISTED descendants (a class's hoisted
// classes are exactly its kClassDef-kind children, recursively). One place owns
// the recursion so a new per-class step or nesting rule lands once — resolve,
// classify, and any future stage call this rather than re-rolling the descent.
// `enter(cls)` runs before descending into cls's nested classes; `exit(cls)` after
// (so a frame opened in enter stays open across the descent and pops in exit).
template <class Enter, class Exit>
void forEachHoistedClass(Node& cls, Enter enter, Exit exit) {
    enter(cls);
    for (auto& m : cls.children) {
        if (m && m->kind == Kind::kClassDef)
            forEachHoistedClass(*m, enter, exit);
    }
    exit(cls);
}

}  // namespace parse
