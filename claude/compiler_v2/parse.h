#pragma once

#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <vector>

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
    kCallStmt,
    kCallExpr,     // value-producing call; name = callee, children = args
    kExprStmt,     // expression evaluated for effect, value discarded; children[0] = expr
    kAliasDecl,    // alias Name = Type; name = alias, return_type = target spelling
                   // bare `alias Ns;` (no `=`): name = last segment, qualifier =
                   // leading namespace segments, global_qualified for `::`
    kNamespaceDecl,// Name { members }; name = namespace, children = member decls.
                   // Consumed by resolve+desugar; never reaches codegen.
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
    std::string return_type;     // function return type; reused for VarDecl's declared type
    std::string nominal_type;    // literal nodes: nominal type assigned by constfold
    std::string inferred_type;   // classify: expression nodes' in-context type
    std::string op_type;         // classify: binary's computational type (commonType / shift LHS)
    std::string alias_label;     // classify: the alias/enum NAME this expr carries as
                                 // a label (else empty). inferred_type/op_type stay
                                 // the erased underlying for width math + codegen;
                                 // this parallel label is what ##type reports. Sticky
                                 // alias+alias / alias+literal, dropped on a mismatch.
    std::string strong_type;     // constfold: a folded/substituted literal that came
                                 // from a STRONG (typed) const carries that const's
                                 // type here (empty = weak literal). Propagated through
                                 // the fold so an inferred typeless const can tell a
                                 // strong rhs (takes its type) from a bare-literal rhs.
    int file_id = -1;            // source file of the construct
    int tok = -1;                // index into token::List::tokens for error attribution
    int name_tok = -1;           // ident token for named constructs (VarDecl, FunctionDef/Decl, Param)
    int resolved_entry_id = -1;  // classify: ident / lhs / callee -> Tree::entries index
    int range_dotdot_tok = -1;   // kForLongStmt synthesized from a ranged-for: the
                                 // `..` token (>= 0 marks range-derived; the caret
                                 // for the "Invalid range." empty-range check)
    // A loop's explicit `:label` (empty = the keyword default for/while), parsed
    // after the body. On a kBreakStmt/kContinueStmt: `text` holds a numbered
    // argument (digits), `name` a named argument (label, incl. the for/while
    // keyword); both empty = naked. resolve stamps loop_levels = hops outward in
    // the loop/switch context stack to the resolved target (0 = innermost),
    // consumed by codegen; -1 until resolved.
    std::string label;
    int loop_levels = -1;
    bool is_const = false;       // kVarDeclStmt: declared with leading `const`
    // Qualified name (ident / call / inline decl / bare alias): leading namespace
    // segments before `name`. `Space:Nested:kFour` -> qualifier {Space, Nested},
    // name kFour. `global_qualified` marks a leading `::` (global root). Consumed
    // by resolve, which rewrites the node to a plain resolved reference.
    std::vector<std::string> qualifier;
    std::vector<int> qualifier_toks;   // token index per qualifier segment (carets)
    bool global_qualified = false;
    std::vector<std::unique_ptr<Node>> children;
    std::vector<std::unique_ptr<Node>> params;   // kFunctionDef/Decl: kParam nodes
    std::vector<std::string> param_types;        // kCallStmt/kCallExpr: classify-cached resolved fn's param types
    // A NESTED function (kFunctionDef in a body) and each call to it carry the
    // entry ids of the enclosing-function locals/params it captures — passed
    // by reference (the host alloca's address) when the nested function is
    // lifted to a top-level function in codegen. capture_types (on the
    // kFunctionDef, parallel to captures) is each captured var's slids type, for
    // emitting the lifted function's by-ref params.
    std::vector<int> captures;
    std::vector<std::string> capture_types;
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
    std::string slids_type;       // LocalVar / Const: declared type; Function:
                                  // return type; Namespace: empty, or the
                                  // underlying type when it is an enum's
                                  // namespace facet (transparent type alias).
    std::string alias_label;      // LocalVar / param: the as-declared alias/enum
                                  // spelling when the declared type was a named
                                  // type (else empty). slids_type holds the erased
                                  // underlying; this is what ##type(var) reports.
    std::vector<std::string> param_types;  // Function only
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
    std::string const_strong_type;  // kConst: the const's STRONG type (empty = weak,
                                    // i.e. a named literal). Explicit-typed consts and
                                    // typeless consts inferred from a strong rhs are
                                    // strong; a typeless const from a bare literal is
                                    // weak. Substitution stamps it onto the literal.
};

struct Tree {
    std::vector<std::unique_ptr<Node>> nodes;

    // Symbol table — populated by classify, consumed by later stages.
    std::vector<Entry> entries;
    int next_frame_id = 0;

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
std::string const& entryType(Tree const& t, int entry_id);

}  // namespace parse
