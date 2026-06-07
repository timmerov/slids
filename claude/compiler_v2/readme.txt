compiler_v2 — slids compiler rewrite, in-progress

(Companion docs: plan.txt = the phase roadmap / main quest; todo.txt = open
side-quests — bugs, reach goals, deferrals. This file = per-stage current-state
notes: what each stage does today.)

PIPELINE (each stage consumes its predecessor's output, produces its successor's input):

    source text
      => lex       => token::List
      => numeric   => token::List (literal tokens canonicalized + validated)
      => grammar   => parse::Tree
      => resolve   => parse::Tree annotated (symbol refs; const idents substituted)
      => constfold => parse::Tree (literal sub-trees collapsed + nominal-typed)
      => classify  => parse::Tree annotated (types: inferred_type + op_type)
      => desugar   => ast::Tree
      => optimize  => ast::Tree annotated (perf rewrites in place)
      => layout    => ast::Tree annotated (mangled names + offsets)
      => codegen   => .ll text

main.cpp drives the chain per TU and runs `diagnostic::hasErrors(diag)` between
layout and codegen to short-circuit on errors.

DESIGN PRINCIPLES

  * Stages = code (executive decisions). Products = data + ALL manipulation APIs.
    Stage files never touch storage directly; they only call product APIs.
  * Three product types only: token, parse, ast. No side tables.
  * One error pipe: diagnostic::report. Error rendering may cross stage
    boundaries (it reads token::List to look up source attribution).
  * Lean unwind, no throw. Stop at first error today; sink can grow an
    N-error threshold without changing report sites.
  * No silent defaults. Every default arm is documented no-op, unreachable
    assert, or error path. Enforced by `-Werror -Wswitch-enum`.

TYPE REPRESENTATION (the carrier; not a stage)

  * Types are STRUCTURED, never strings. A type is a widen::TypeRef — a handle
    into a process-lifetime interned arena (widen::intern / widen::spell). A
    widen::Type carries a Form (kNone / kPrimitive / kVoid / kAnyptr / kPointer /
    kIterator / kArray / kSlid / kTuple / kAlias) plus its payload (cat+bits,
    pointee, elem+dims, slots, underlying). spell(intern(s)) == s exactly (bar
    kAlias, which is minted not parsed), guarded by `slidsc --type-selftest`.
  * Dedup is STRUCTURAL (by_struct, keyed on form + child handles), NOT by
    spelling — int / int32 / intptr still stay DISTINCT (their spellings differ)
    yet share cat()/bits(), but an alias-bearing composite (`(Dir,bool)`, `Dir^`)
    is now distinct from its kSlid-leaf form, which a spelling key could not do
    (grammar interns spellings with kSlid leaves BEFORE resolve knows they are
    aliases). intern(spelling) parses then structurally interns; by_spelling is
    only a parse memo. Structural constructors build a composite from child
    HANDLES: internPointer / internIterator / internArray / internTuple, and
    internAlias(name, underlying) — the last minted ONLY by resolve (which has the
    symbol table). strip() peels one alias layer; deepStrip() removes all (so
    `Integer^` and `IntPtr=int^` compare equal modulo aliases).
  * kAlias is a TRANSPARENT type: spells as its name (for ##type/diagnostics) but
    sees through to `underlying` for every structural query (classify / llvm /
    size / known / the form-predicate cluster via strip). Aliases + enum type
    facets are kAlias; alias_label is now a derived display cache, not a channel.
  * Every type FIELD is a TypeRef: Node.return_type / inferred_type / op_type /
    nominal_type / strong_type and Entry.slids_type / const_strong_type /
    param_types / capture_types (both parse:: and ast::), plus codegen::VarInfo.
    alias_label stays a std::string — it is a display NAME, not a type.
  * Structure is the single source of truth. Spellings are RENDERED on demand
    ONLY at genuine edges: diagnostics, ##type, the no-width commonType rule, and
    the classify primitive-name lexer. NEVER cache a canonical spelling — storing
    a type-string is what killed v1. codegen + print + the classify/resolve
    predicate+cast-rule cluster read structure off the handle (form/cat/bits);
    upstream stages that still compute spellings bridge at field boundaries via
    widen::spellOrEmpty (read) / widen::internOrNone (write).
  * kNoType is the "no type" handle (empty spelling). get(kNoType) reads as
    Form::kNone, so every predicate returns false/none on it AND a form==kVoid
    test can never mistake a no-type for void — a stray no-type surfaces loudly
    (e.g. llvmForRef assert) rather than silently lowering as void.

ANONYMOUS TUPLES + #x (landed this phase; spans every stage)

  * A tuple is Form::kTuple (slots). LITERAL `(a,b,...)` is kTupleExpr (grammar:
    parsePrimary, comma after the first paren expr; size-1 collapses to the bare
    expr). TYPE `(T,T,...)` parses in parseType; a `(`-led statement disambiguates
    via looksLikeTupleTypeDecl (trailing name) / looksLikeTupleDestructure (`)=`).
  * Landed: construct + whole-copy + const-index read `t[k]` (extractvalue; a
    RUNTIME index on a tuple is rejected — heterogeneous slots); slot write
    `t[k]=v` (struct-GEP) + destructure `(a,b,)=t` (kDestructureStmt, null child =
    skipped slot); slot-wise arith + scalar broadcast (`(1,2,3)+7`); params /
    returns / references (`{i32,i32}` by value, `(T,T)^` = ptr). Codegen builds
    the aggregate via insertvalue; classify slot-types via internTuple.
  * ARRAY from a homogeneous tuple: `int a[3]=(1,2,3)`, `a=(4,5,6)`, multi-dim
    `int td[2][3]=((1,2),(3,4),(5,6))` (nested tuple in storage/outer order onto
    the reversed-dim layout). Element-wise, tuple aggregate ELIDED, each slot
    widens into the element type (resolve marks assigned_arrays; classify counts +
    per-leaf checks; codegen flat-GEP per leaf).
  * FOR-TUPLE `for (v : tuple)` over a HOMOGENEOUS tuple: rewriteForTuple lowers
    (like for-array) to a kForLongStmt walking an iterator (`_$iter=^base[0]`,
    typeless so classify fills T); a VARIABLE iterates in place (no copy, mutable
    by-ref writes back), a LITERAL spills to a temp (require_homogeneous-checked).
  * #x desugars (grammar parseUnary) to the 5-tuple `(##file, ##line, ##type(x),
    ##name(x), ^x)`; x must be an lvalue. Passing an rvalue tuple to a reference
    param (`dump(#x)`) materializes it in a temp — emitCall brackets such a call
    in @llvm.stacksave/stackrestore so a materializing call in a loop doesn't leak.

STAGE FILES (.h / .cpp pairs)

  lex       text -> tokens. Wraps the scanner in an ImportWrapper that
            recursively expands `import X;` at depth-0 file scope into one
            unified token list. Tracks bracket-kind balance ( { [ only.
            Emits kEndOfFile per file, kEndOfInput once at the outermost
            return. Numeric literals: strips underscores, emits source-form
            text per kind (char/int/uint/float); rejects only structural
            errors (missing digits after 0x / 0b / e). Value parsing,
            escape interpretation, and overflow live in the numeric stage.
  numeric   tokens -> tokens. Validates and canonicalizes every literal
            token in place: char escapes ('A' -> 65, '\\n' -> 10); hex/
            binary -> decimal (0xFF -> 255, 0b1010 -> 10); float text via
            %.17g; bool "true"/"false" -> "1"/"0". Overflow assumes uint64
            / float64 storage; kIntLiteral whose value > INT64_MAX flips
            kind to kUintLiteral here. Codegen's float32-emit path now
            uses the hex bit-pattern form (item 7 landed) so lossy values
            like `3.14` reach llc successfully. One open item remains on
            the migration table — proper attribution for uint/char
            literal-fit errors.
  grammar   tokens -> parse tree. Pure syntax; every identifier is just a
            name string. Hand-written recursive descent. Parses: types
            (built-in primitives, an identifier type name, a namespace-qualified
            type name `Space:Dir` / `::A:B:T`, + T[] of any); a
            looksLikeQualifiedTypedDecl lookahead routes an identifier-typed decl
            to a var-decl (vs `Space:foo()` / `Space:kX = 1` / `p^ = v` /
            `arr[i] = v`, name-led statements): it scans the (qualified) name, an
            OPTIONAL `^` (reference) or empty `[]` (iterator) suffix, then requires
            the var name — so `Space:Dir x`, `Integer^ ref`, `Integer[] iter`,
            `Space:Dir^ d` all parse (a non-empty `[i]` is a subscript, not a
            suffix, so `arr[i] = v` stays a store; a bare `a ^ b` reads as a
            reference decl `a^ b` since a bare XOR is not a statement form). An
            array DIM (`Int nums[4]`) goes after the name -> the plain `Ident
            Ident` path;
            `alias Name = type;` + bare `alias Ns;` decls; namespace decls
            (`Name { members }`) and inline qualified member decls
            (`const int Space:kSix = 6;`); enum decls
            (`enum [type] [Name] ( m1 [= v], ... )`); function defs/decls with typed
            param lists; var-decls with optional leading `const` (file
            scope or function scope) — incl. a TYPELESS const (`const name =
            expr`, detected by `=` immediately after the name, parseType skipped
            so constfold infers the type); statements (var-decl incl. the
            `<ident> <ident>` typed-decl shape and a `<--` move-init form
            (`T x <-- y`, the move_init flag — `<-->` swap is not a decl), assign,
            aug-assign, move (`a <-- b`) / swap (`a <--> b`) — finishMoveSwap on
            both the bare-name and indexed/deref-lvalue paths, lhs as an expr child,
            alias,
            namespace decl, 0/1/N-arg call possibly qualified, bare inc/dec,
            return, if/else, while + post-condition do-while, the long-form for,
            the RANGED for, and the ENUM for — a qualified name leading a statement
            routes through one parseNameLedStmt); each for varlist head is parsed
            by parseForVarHead as `[type] name` — the var TYPE is optional, and a
            TYPELESS var (the lead is neither a primitive type-start nor a qualified
            typed-decl shape) is inferred / reused at resolve. The for forms then
            dispatch on what follows the `[type] var`: ':' then an operand, then
            '..' -> ranged form (`for (var : start .. [cmp] end [op step]) {body}`,
            cmp `< <= > >= !=` default `<`, op `+ - * / << >>` default `+1`,
            operands are unary-expressions), DESUGARED in the grammar to a
            kForLongStmt (hidden `_$end` / `_$step` vars + cond/update, tagged with
            the `..` token); ':' then a bare identifier -> ENUM form (`for (var :
            Enum) {body}`) parsed as a kForEnumStmt (resolve lowers it, below);
            anything else is the long form's varlist. parseSwitchStmt parses
            `switch (value) { (case const-expr | default) : stmts ... }` into a
            kSwitchStmt (children[0]=scrutinee, [1..]=kCaseClause, label null =
            default); the value is required and each clause body is an implicit
            block. A case label is parsed under the `case_label_` flag so a
            qualified enum-member label (`case Dir:N:`) resolves its trailing `:`
            as the terminator, not a qualifier (parseQualifiedNameCaseLabel scans
            the maximal `:`-chain and rewinds one segment when the terminator is
            missing). A loop carries an optional `:label` (parseOptionalLabel)
            right after its body `}` — for a do-while between the body and the
            `(cond)`, elsewhere with a required trailing `;`; labels are on loops
            only (a `:name` after a switch is a parse error). break / continue take
            an optional argument — an integer (stored in `text`, the Nth loop) or a
            name (in `name` — an identifier, or the `for`/`while` keyword default).
            expressions
            across the full C precedence ladder (literals + ident, unary
            `! ~ + -`, the `<Type^>` pointer cast (a prefix unary — a leading
            `<` is unambiguous since binary `<` only sits between operands; the
            target spelling rides on return_type, the operand is another unary so
            casts chain right-to-left), the `(Type = expr)` value conversion
            (kConvertExpr — a type-keyword right after `(` opens it, unambiguous
            since no parenthesized expr or tuple starts with a type; parseConvertChain
            parses the target onto return_type and recurses for chain links
            `(A = B = expr)`, right-to-left, no inner parens), prefix/postfix ++/--, full binary set
            arith/bitwise/shift/comparison/logical, parens, postfix-call on
            a bare ident, and the `##` stringify macros in parsePrimary's
            kHashHash arm — ##file / ##line / ##func / ##name(x) (raw lexed
            token text, scanned to the matching ')' by bracket depth) /
            ##date / ##time / ##type(x) (a kStringifyType node, child = the
            operand, lowered to a kStringLiteral in classify), sizeof(...) (a
            kSizeofExpr — a type operand [paren content starts with a type keyword]
            rides on return_type, else the operand is parsed as an expression for
            resolve to dispatch on; lowered to an intptr literal in constfold or
            classify), and new(...) (a kNewExpr — `new`, an optional `(addr)`
            placement prefix [today a `(` always opens a placement address; no type
            spelling starts with `(` yet — a TODO seam marks where the tuple /
            const-pointer lookahead goes], the element type [parseAllocElementType:
            a type WITHOUT a trailing `[`, which is the array size], an optional
            `[n]` size; children[0]=size-or-null, [1]=addr-or-null)). `delete p;` is
            a statement (kDeleteStmt). Stamps (file_id, tok)
            on every node for source
            attribution. No identifier resolution, no scope tracking,
            no type inference, no literal folding — all deferred to
            later stages. Errors are single-shot ("expected '...'") with
            caret at the offending token; sets fatal + early-returns
            up the call chain.
  resolve   parse tree -> annotated parse tree. Builds the symbol table
            (parse::Entry vector on parse::Tree) and resolves every
            identifier-use to a resolved_entry_id. Pushes/pops frames at
            scope-opening nodes (program, function-body today; block /
            class as Phase 2+ land). Pass 1a collects
            program-scope entries (Functions + Consts) without walking
            init expressions; pass 1b-enum + pass 1b walk file-scope enum
            member inits then const init rhs (so globals can reference each
            other regardless of decl order); pass 2 walks function bodies. Owns type aliases: a
            pass-1a-alias pre-sweep registers file-scope `alias` decls as
            kAlias entries; resolveTypeSpelling substitutes an alias chain to
            its underlying (cycle-detected), and resolveDeclType rewrites every
            declared / return / parameter spelling in place before validating
            it (widen::isKnownType) — so downstream stages see only underlying
            types and aliases never reach the ast. requireKnownType also rejects
            `void` with an iterator/array suffix (`void[]`, `void[N]`): void has
            no stride, so a void pointer must be a reference (`void^`). A
            kCastExpr's target type runs through the same resolveDeclType, so a
            cast inherits alias substitution and the void/unknown-type checks; a
            kConvertExpr's target rides the same path (operand resolved as a read,
            target alias-resolved + seg-tok carets). Whenever resolveDeclType erases
            a NAMED type to a different underlying (at a local-var decl site or the
            param pass-1 site), the as-declared alias/enum spelling is stashed in a
            parallel alias_label channel (parse::Entry.alias_label) for ##type to
            report — slids_type stays the erased underlying, so codegen never sees
            a name. A namespace-qualified type
            spelling (`Space:Dir`) resolves via resolveQualifiedType (the lead
            segments walk the shared ns chain, the leaf must be a type) before
            any downstream stage; the cycle-vs-resolution-failure suppression
            flag is named `reported`. A bad segment carets the OFFENDING SEGMENT
            (not the whole type position): parseType captures per-segment tokens
            onto Node.return_type_seg_toks, threaded as a defaulted seg_toks param
            through resolveDeclType -> resolveTypeSpelling -> resolveQualifiedType
            and wired at the var-decl declared-type + cast-target sites (a
            flat-tok fallback covers the sites that don't pass it yet).
            Owns the `##type` operand dispatch: the kStringifyType arm looks up
            the operand (resolveName for a bare name / resolveQualifiedRef for a
            qualified one — both return the entry for ANY kind, erroring only on
            a missing name) and branches on entry kind. A TYPE-NAME operand (a
            kAlias, or an enum's kNamespace type facet) runs through
            resolveTypeSpelling and is stamped on return_type (so `##type(Integer)`
            / `##type(Space:Dir)` -> the underlying); a VALUE operand (kConst /
            kLocalVar) takes the value path; neither is rejected ("'X' is not a
            value or an alias." undefined / "'X' is a <namespace|function>, not a
            value or an alias."). registerEnumMembers also stamps each NAMED-enum
            member's alias_label with the enum name (an anonymous enum has no name
            -> no label -> bare `const int`), so `##type(Enum:member)` reads
            `const Enum`. The kSizeofExpr arm shares that type-vs-value dispatch: a
            type operand (return_type from grammar) is alias-resolved + validated;
            an ident naming a type stamps the underlying on return_type; any other
            ident / expression is resolved as a value in an UNEVALUATED context
            (sizeof / ##type read only the type — resolveExpr's `unevaluated` flag
            suppresses use-before-init but keeps the read-mark, propagated through
            arith / index / deref operands; no definite-assignment required). kNewExpr
            alias-resolves + validates the element type (resolveDeclType) and
            resolves the size / placement-address sub-expressions as value reads.
            kDeleteStmt resolves its operand as a read (you can't delete an
            uninitialized pointer) and requires it be a variable lvalue — an
            UNRESOLVED ident is left to resolveExpr's own "Unresolved identifier"
            (no cascading "must be a pointer variable"); a resolved non-variable
            (const / function / a non-ident expr) is rejected here.
            Owns namespaces: a kNamespace entry has a persistent frame
            identity that reopens reuse; members ride the enclosing lexical
            lifetime, tagged by owning namespace. Bare lookup walks the open-
            namespace chain (siblings + enclosing namespaces + `alias Ns;`
            imports) then the lexical scope — qualifiers always optional, `::`
            names the global root and only defeats a shadow. Qualified names
            (`A:B:C`, leading `::`) resolve through one shared chain walker
            (refs, inline member decls, bare aliases word identically), each
            diagnostic careting the offending segment. A bare name matching
            members of two different open namespaces / enums is "ambiguous"
            (notes at both decls).
            Owns enums: `enum [type] [Name] ( members )` lowers here (not
            desugar — members must be kConst by constfold). Named -> a
            kNamespace whose slids_type carries the underlying (the name
            doubles as a transparent type alias) + kConst members; anonymous
            -> bare kConst members in the enclosing frame. May be declared at
            file, function, or NAMESPACE scope (registerEnum takes a parent_ns;
            a named enum becomes a member of its enclosing namespace, an
            anonymous one's members do). Values auto-
            increment from 0 (int) / 0.0 (float), C rules; an explicit init
            resets the run. An implicit member is synthesized as
            clone(last-explicit-init) + offset (constfold folds it), so a
            non-literal explicit init like `kB = 1 + 2` continues correctly.
            A file-scope pass-1a-enum REGISTERS names + members (before
            namespaces / aliases); a separate pass-1b-enum RESOLVES the member
            INIT expressions later — after every file-scope entry is collected,
            with the enum's own frame open — so a member init can reference a
            file-scope const (`enum E ( e = kG )`) or a sibling member bare
            (`enum E ( a, b = a )`). A block-scope enum registers + resolves in
            one shot in the body pass (all enclosing entries already exist). A
            namespace-member enum registers in registerNamespaceTree (which
            registers type-introducing members — enums, nested namespaces —
            before consts/functions, so a member's type may name a sibling enum
            regardless of order) and resolves inits in resolveNamespaceBodies.
            Definite assignment + unused locals: the body walk tracks three
            per-function entry-id sets (initialized_locals, read_locals,
            body_locals; all id-keyed, no names). A kLocalVar read before it is
            written → "Use of uninitialized variable 'x'." (params are seeded
            initialized; a decl-with-init or assignment marks written; rhs
            resolves before the mark so `x = x` fires). INFERRED-INIT promotion:
            an assign to a truly-undeclared name (!isQualified && resolveName < 0,
            so a reassign or a wrong-kind target falls through to the normal assign
            path) creates a fresh kLocalVar with empty slids_type, rewrites the
            kAssignStmt to a kVarDeclStmt, resolves the rhs (so `x = x` reads x
            uninitialized), and marks it initialized — the type is left for classify
            to infer + write back. A TYPELESS const (`const name = expr`, empty
            declared type) skips resolveDeclType in both the function-body const
            pre-pass and the main kVarDeclStmt arm (constfold infers the type).
            An end-of-body sweep
            then reports any body-declared local never read: "Unused local
            variable 'x'." if never written, else "Local variable 'x' set but
            never used."; gated on hasErrors so a use-before-init or dup
            diagnostic isn't trailed by a spurious unused report. ARRAYS use a
            separate MAY-set (assigned_arrays): a fixed-size array can't be fully
            initialized in one statement (no initializer lists) and a fill loop's
            element writes wouldn't survive the must-set's loop join, so an array
            read requires only that SOME earlier subscript write exists (monotonic,
            never rolled back) — reading before ANY write still errors; a `^arr[i]`
            address-of marks it assigned, and an iterator-base store (`it[i]=v`)
            READS the iterator (the pointer is dereferenced), not writes it. Consts and
            params are exempt (consts substituted away; params not in
            body_locals). Control-flow joins are modeled by a Completion
            { Normal, Abrupt } that resolveStmt RETURNS: return / break / continue
            are Abrupt, everything else Normal.
            resolveStmtList threads it over a statement list — a statement after
            an Abrupt sibling is "Unreachable statement." (2A) and the dead tail
            is skipped (it declares no locals); both the function body and every
            block walk through resolveStmtList. A kBlockStmt `{ stmts }` opens
            a nested frame: initialized_locals + read_locals FLOW THROUGH (scoped,
            not isolated — an assign/read inside a block affects the enclosing
            local), only body_locals is save/restored so the unused sweep
            (shared sweepUnusedLocals) runs per-block at block exit; a block is
            Abrupt iff its statement list is. Shadowing is allowed (inner masks
            outer via innermost-first lookup, restored on pop). A kIfStmt joins
            definite-assignment at the merge: snapshot the init-set S, resolve
            each arm from S, intersect the arms' out-sets — an Abrupt arm
            contributes the universal set (dropped from the ∩), a missing else
            contributes S unchanged (so an else-less if never adds an init); the
            if is Abrupt iff it has an else and both arms are. read_locals never
            joins (monotonic union — a read on any path is a use). Trailing-return
            correctness (classify) recurses into a trailing block and a trailing
            if/else whose arms both return (endsInReturn / endsInReturnNode).
            Loops: a kWhileStmt (pre-condition) is possibly-zero — condition + body
            resolve from S and the post-loop set is S again (body inits don't
            escape); normally Normal. EXCEPTION (3B revisited): a syntactically-
            constant-true condition (a bool/int/uint/char literal, incl. the
            synthesized empty `()`; condIsConstTrue) with no break targeting the
            loop (loop-frame break_seen false) is NON-COMPLETING — it returns Abrupt
            (so 2A flags its after-code unreachable) and sets Node.non_completing
            (classify reads it for return-correctness, codegen for the `unreachable`
            exit). A named-const-true condition isn't caught (resolve predates
            constfold). A kDoWhileStmt (post-condition, `while { body } (cond);`)
            runs the body once so its inits DO escape: resolve the body from S,
            check the condition's reads against body-out ∩ continue_accum, and set
            after = that ∩ break_accum (the same constant-true non-completing rule
            applies).
            break / continue are Abrupt; each resolves a TARGET frame by its
            argument — NAMED (`break name;` → nearest loop whose label matches; the
            label is the explicit `:name` or the keyword default for/while; switches
            carry none; innermost wins, shadowing allowed), NUMBERED (`break N;` →
            the Nth enclosing LOOP outward, SKIPPING switch frames; N>=1), or naked
            (break → nearest loop OR switch; continue → nearest loop, switches
            transparent) — and folds the current init-set into THAT frame's
            break/continue accumulator (∩, top-seeded via a `seen` flag; a do-while
            / for consume them, a pre-condition while ignores them). It stamps
            Node.loop_levels = hops outward to the target for codegen. NO flavor of
            break/continue is allowed directly in a for-update clause (the
            in_for_update + for_update_floor guard fires first). Errors: count <1 /
            exceeds nesting (caret on the count literal), no enclosing loop labeled
            <name>, inside-a-loop[-or-switch]. A kForLongStmt
            (long-form `for (varlist) (cond) {update} {body}`; the canonical for
            node — other for shapes desugar to it) opens ONE for-scope holding the
            varlist, with the update and body as sibling nested blocks (3 frames;
            the body may shadow a for-var). A TYPELESS varlist decl (empty
            return_type) is intercepted: WITH an init it becomes a kAssignStmt
            (reuse an enclosing local, else fresh inferred-init); with NO init it
            reuses an in-scope local as a no-op slot, errors "Cannot use <kind>
            '<x>' as a loop variable." if the name resolves to a non-local, or
            "Cannot infer the type of '<x>'; it has no initializer." (+ placeholder
            entry to stop a read cascade) if undeclared. Resolved body-then-update
            (execution order): cond reads from the post-varlist set S'; the body
            resolves from S' (break/continue target the for); the update is checked
            against body-out ∩ continue_accum (so it sees body-assigned vars but not
            ones a continue skipped). after = S' (the varlist inits run once
            unconditionally, so they ESCAPE — a reused enclosing var is observable
            after the loop; the possibly-zero body/update inits do not escape). The
            update may not
            break / continue / return — resolved under in_for_update +
            for_update_floor: a break/continue at the update's own loop-depth or
            any return in the update errors ("A '<kw>' statement is not allowed in
            a for-loop update clause."), while a loop nested in the update gives
            its own legal break/continue target. A kForEnumStmt (`for (var : Enum)
            {body}`) is REWRITTEN IN PLACE here into a kForLongStmt over the enum's
            first..last DEFINED members: resolve the enum-ref (must be a kNamespace
            with an underlying type), find the first/last kConst members by id
            (definition) order in its ns_frame, and synthesize
            `for (T var = Enum:first) (var <= _$end = Enum:last) { var = var + 1; }
            {body}` (members referenced by qualified name → normal resolve/constfold
            fills their values), tagged range-derived so a descending/empty enum
            (first > last) trips the empty-range "Invalid range." check on the enum
            name; then resolved through the for-long path. A kSwitchStmt resolves
            the scrutinee, then each clause body from the entry set S (any case can
            be matched directly, so direct entry is the weakest join input) under a
            loop_stack frame with is_switch=true: naked break targets the nearest
            loop OR switch, naked continue skips switch frames to the nearest loop
            ("A 'break' statement must be inside a loop or switch."). The after-set
            is the ∩ over exit paths (each break point's init-set, the bottom-fall,
            and — default-less — the no-match path = S); a switch with a default and
            no normal exit is Abrupt. An empty
            condition (`if ()` / `while ()` / `while {} ()` / for's `()`) is the
            always-true literal grammar synthesizes via the shared
            parseParenCondition (a slids convention "empty = true"). The loop-frame
            stack (Tree::loop_stack) is transient resolve state, id-keyed like the DA sets.
            Caches lvalue type on AugAssignStmts (s.return_type) and
            return type + param_types on CallStmts/CallExprs (one shared
            resolveUserCall) so downstream stages don't have to re-walk the
            entry table. Sharp diagnostics at the source: wrong-kind entry
            (assign / call use allowlists: only a local var is assignable,
            only a function is callable — every other kind reports
            type/constant/namespace/function and never slips through),
            duplicate decls, return-type mismatch, parameter-type mismatch,
            duplicate definition, arity mismatch, multi-arg print intrinsic,
            print intrinsic used as an expression, needs-qualifier /
            not-visible-from-scope / has-no-member / is-not-a-namespace for
            namespace access, and (final pass) any function declared but
            never defined — anywhere, used or not. Multi-source notes point
            at prior decls. Owns
            the "what does this name refer to" decision; types are not
            resolve's job.
  constfold parse tree -> parse tree. Iterative post-order walker.
            Assigns nominal_type to every literal per fold.sl:16-23
            (bool=uint1, char=uint8, integer/unsigned by smallest-bit-
            tier, float by float32-round-trip). Folds unary on literal
            (rules 1a-1f) and binary on two literals across all op
            families: int arith / bitwise (signed int64 with rule-6
            overflow-to-uint64), int shifts (count >= width → 0; uint64
            reinterpret to avoid UB), int comparisons (int64 path with
            uint64 fallback), float arith / shift / comparison (double
            + %.17g canonical text; pow2 mul/div lowering for float
            shifts). Substitutes kIdentExpr -> literal node in place
            when the resolved entry is a kConst with a captured value.
            Captures const-decl values back onto kConst entries when
            the rhs folds to a single literal — floats round through
            the declared type for precision capture (3.14 -> float32
            stored as 3.1400001049...); ints/bools/chars store rhs text
            verbatim with range validation against declared type.
            Const strength model (typeless consts): strong_type on nodes /
            const_strong_type on entries — a combineStrong helper + makeLitAt
            propagate strength through arith/bitwise folds (a strong/typed-const
            operand makes the result strong + takes its type, both-strong uses
            widen::commonType, a bool/comparison result is weak); trySubstituteConst
            stamps strong_type onto a substituted literal from entry.const_strong_type
            AND carries entry.alias_label onto it (so a const's type label survives
            substitution — needed for the inferred-var case);
            tryCaptureConst infers a typeless const's type (a strong rhs takes its
            type, a bare-literal rhs is WEAK -> weakDefaultType preferred spelling
            with the narrowest nominal kept under the hood) and marks explicit-typed
            consts strong, and stamps the captured value's literal_kind from the
            DECLARED type via literalKindForType (char -> kCharLiteral, bool ->
            kBoolLiteral, float* -> kFloatLiteral, uint* -> kUintLiteral, else
            kIntLiteral) rather than the folded initializer's kind — so char/bool
            consts and `enum char`/`enum bool` members keep their declared kind.
            The capture range-check says "inferred type" for a
            typeless const, "declared type" for an explicit one. walk() returns
            early on a kStringifyType node so the ##type operand subtree is
            fold-EXEMPT (a const under ##type is not substituted to a literal before
            classify reports it). A kSizeofExpr is likewise operand-exempt and
            folds in place (tryFoldSizeof): a statically-known operand — a type
            (return_type), a string literal (length + null), nullptr, an address-of
            (always 8), or a plain ident (its declared type, via
            widen::typeByteSize) — becomes a STRONG `intptr` literal HERE, before
            const capture, so sizeof can initialize a const / feed a const
            expression. Operands needing inference (deref / index / arithmetic) and
            a slid type are left for classify.
            A `(Type = expr)` conversion (tryFoldConvert) with a LITERAL operand
            folds the same way: it computes the C-semantics result (float->int
            trunc, int->int low-N-bit reinterpret, ->bool nonzero, ->float convert)
            and emits a STRONG target-typed literal, so a conversion can initialize
            a const or size an array dimension. A non-literal / pointer operand is
            left for codegen (no compile-time pointer value).
            Iterates to a fixpoint; any kConst whose rhs never folded
            errors with "Initializer for 'X' is not a constant
            expression." (consolidated into one diagnostic with notes
            for additional affected entries when a cycle spans several
            consts). Rejects div/mod by literal zero,
            `~float`, float `& | ^`, shift with float rhs or negative
            count, constants whose value doesn't fit declared type.
            Algebraic identities (x+0 -> x) are NOT done here -- they emit
            a non-literal, breaking the literal->literal contract; scalar
            cases are LLVM's job (see optimize).
  classify  parse tree -> annotated parse tree. Type inference and
            (Phase 3) overload resolution. Reads resolved_entry_id + entry
            data stamped by resolve; never builds entries or pushes frames
            itself, with ONE deliberate symbol-table exception: the inferred-init
            write-back (below). Infers every expression's inferred_type and every
            binary's op_type (computational type). INFERRED-INIT write-back: a
            kVarDeclStmt with empty return_type + !is_const + resolved_entry_id >= 0
            infers the rhs, NORMALIZES a literal-inferred type to its preferred
            spelling (preferredSpelling: int32->int, uint32->uint, float32->float;
            a typed rhs keeps its spelling), copies rhs.alias_label, and WRITES BACK
            entry.slids_type (assert-guarded on hasErrors || rhs.inferred_type
            non-empty); gated to !is_const so it doesn't clobber constfold's const
            inference. A kAugAssignStmt on such a var RE-READS entryType into
            s.return_type (resolve cached it empty before the decl was stamped).
            sizeof lowering: the kSizeofExpr cases constfold left (a deref / index /
            arithmetic operand, or a slid type) become a kIntLiteral of type
            `intptr` — widen::typeByteSize of the type/value operand, or content+1
            for a string literal; a slid type (-1) reports "Cannot take sizeof of
            'X'." (the foldable operands already became literals in constfold).
            ##type(x) lowering: a kStringifyType node becomes a kStringLiteral whose
            text is the operand's resolved type — alias_label ?: inferred_type, plus
            the const qualifier (a kIdentExpr operand resolving to a kConst -> "const
            " + (alias_label ?: slids_type); a const read inside a larger expression
            strips const). The lowering SHORT-CIRCUITS when resolve already stamped
            return_type (a bare/qualified TYPE-NAME operand): it emits that
            underlying spelling and skips inferExpr. alias_label propagation: an ident reads its entry's label;
            unary/shift pass it through; arith uses a sticky binaryLabel rule
            (alias+same-alias or alias+const-literal keeps the label, any mismatch
            drops it); a comparison clears it — inferred_type/op_type/slids_type stay
            the erased underlying so widen/codegen are untouched. Sharp rejections at
            the source: non-coercible operands for ! && || ^^, an if / while / for
            condition not coercible to bool, non-numeric shift sides, bitwise on
            float, no-common-type binaries. Return-correctness (endsInReturn) recurses
            into a trailing block and a trailing if/else whose arms both return.
            Constant-condition unreachable detection (runs HERE, post-constfold, so
            a folded literal / substituted const / synthesized empty-`()` is
            visible — vs resolve's 2A which is pre-constfold): constTruth folds the
            condition to True/False/NotConst; a const-true if flags its else dead,
            a const-false if flags its then, a const-false while flags its body —
            "Unreachable statement." at the dead branch's first statement (empty
            branch = nothing to flag). The const-TRUE-LOOP unreachable-after case
            (3B revisited) is handled in RESOLVE (a non-completing loop returns
            Abrupt, so 2A flags the code after it), not here; classify's only
            const-true-loop role is endsInReturnNode reading Node.non_completing for
            return-correctness. A kForLongStmt
            classifies its loop var (children[3]) FIRST so a typeless one is typed
            before the rest; for a ranged/enum for (range_dotdot_tok set) the
            loop-var type is then stamped onto any typeless `_$end`/`_$step`
            (children[4..]) so their bounds flex into it — matching an
            explicitly-typed range. Empty-range check (ranged-for only, gated on
            range_dotdot_tok): if a ranged-for's start and end both fold to literals
            and `start cmp end` is false, the body can never run -> "Invalid range."
            caret on the `..` (rangeFirstTestFalse compares the two literals; no
            infinite-loop check — deferred, todo.txt). A kSwitchStmt checks the
            scrutinee is integer-class (float rejected); each case label must be an
            integer constant (constfold folded it) that FITS the scrutinee type
            (literalFitsContext — an out-of-range / sign-mismatched label is
            rejected, never emitted as a truncated `iN`) and is unique by value
            (full 64-bit dedup, so 'a'==97 and 1+2==3 collide, with a "first case
            here" note); default is singular. A switch is a return-terminator
            (endsInReturnNode) iff it has a default, no clause has an escaping break
            (containsBreak — the same test codegen uses), and the LAST clause's
            body ends in a return; C-style fall-through carries a stacked empty /
            non-returning clause into that final return.
            Per-arg type inference at call sites uses the resolved
            callee's param_types (cached on the kCallStmt/kCallExpr by
            resolve) as context. A kCallExpr's inferred_type is the
            callee's return type; a void return used as a value is rejected
            here. Pointer-cast rules (Phase 4) live here as two predicates:
            ptrImplicitOk (a bare assignment may only STRIP info — nullptr->any,
            any->`void^`/`intptr`, iterator->reference of the same pointee) and
            ptrExplicitOk (a `<Type^>` cast additionally bridges through a
            buffer-class pointer [`void^`/`int8^`/`uint8^`] or `intptr`, and
            reinterprets iterator<->reference of the same pointee; two unrelated
            non-buffer pointers must chain through `void^`; only `intptr` bridges
            pointer and integer). checkPtrAssign enforces the implicit rule at
            EVERY pointer-assignment site — kVarDeclStmt init, kAssignStmt, and
            kStoreStmt (a references-array element / deref slot) — gated on a
            pointer being involved (pure-numeric assignments keep the width path).
            The kCastExpr arm validates the explicit rule and stamps the target.
            The kConvertExpr arm (`(Type = expr)`, Phase 4) infers the operand
            with NO context (it retypes, never flexes) then gates the grid: the
            target must be a value type (a pointer/iterator -> "may not be a
            pointer type", anything else non-numeric -> "must be a value type");
            a value source converts to any value target; a pointer/iterator source
            converts ONLY to `bool` or `intptr` (else "a pointer converts only to
            'bool' or 'intptr'"); a non-value source is rejected outright. It
            stamps the target as inferred_type (even on the error paths). Because
            the result is a strong typed value, an over-narrow assignment of it is
            caught by the same checkStrongConstAssign as any typed value.
            Move / swap (Phase 4): kMoveStmt classifies like a store — infer the
            lhs lvalue, the rhs in that context, then checkPtrAssign +
            checkStrongConstAssign (so a move COPIES under assignment rules; a
            narrowing move rejects). kSwapStmt requires the two operands' deepStrip
            types be IDENTICAL (no widening — a symmetric exchange can't convert
            both ways), else "Swap operands must be the same type". resolve owns
            the lvalue rule: resolveMoveSwapLvalue rejects a non-lvalue (a swap rhs
            is a general expression, so `x <--> 7` would otherwise crash codegen)
            — BOTH swap operands and a move's LHS must be lvalues ("A swap operand"
            / "A move target" must be an lvalue); a move's RHS is a plain read, so
            an rvalue source is allowed. DA: a move lhs is a pure write (need not
            be pre-init); a swap reads+writes both (both must be init).
            kNewExpr (Phase 4): a heap element must be statically sized
            (widen::typeByteSize >= 0 — a primitive; a slid -> "Cannot allocate");
            an array size must be integer-class; a placement address must be a
            buffer-class pointer (isBufferClassPtr, the cast set void^/int8^/uint8^).
            The result type is element + (array ? "[]" : "^"). kDeleteStmt's
            operand must be a pointer type. Future: overload resolution when
            multiple Function entries share a name.
  desugar   parse tree -> ast (separate node-type set). Today: identity
            copy that propagates every annotation classify and constfold
            stamped (nominal_type, inferred_type, op_type, resolved_entry_id,
            params, param_types, file_id, tok). Two rewrites are live.
            (1) aug-assign (`lhs op= rhs`) -> `lhs = lhs op rhs`, with the
            synthesized IdentExpr + BinaryExpr inheriting the aug-assign's
            classify-stamped types so codegen sees the rewrite as if it
            were classified directly. (2) PPID: a post-copy pass extracts
            ++/-- per phrase, replacing each with a read; also drops parse-
            only nodes (alias, namespace) and hoists namespace member
            functions to program scope with entry-id-derived symbols (no
            cached canonical-name strings). Statement-level
            bumps splice as sibling kExprStmt bump-statements around the
            statement (post AFTER the store -- the statement is the phrase);
            a bump inside a sub-phrase (call args, && / || rhs) stays in a
            synthesized kSeqExpr {pre... value post...} so a short-circuited
            bump never fires. The statement-bump splice (lowerStatementList)
            recurses into a kBlockStmt, a kIfStmt (lowerIfStmt: the condition is a
            self-contained phrase whose bumps fire before the branch, and the arms
            recurse), a kWhileStmt / kDoWhileStmt (lowerWhileStmt: the condition is
            a phrase re-tested each iteration, the body recurses), and a
            kForLongStmt (lowerForLong: varlist initializers + condition are
            phrases, the update + body are statement lists) so a bump inside them
            splices within that scope, not at function scope. Future canonical-form
            rewrites (other for shapes desugaring to kForLongStmt, receiver shapes,
            stringify, operator dispatch) slot in as their phases land.
  optimize  ast -> ast in place. Slids-aware perf rewrites LLVM can't do
            (compound-fuse, NRVO, identity-temp adoption, build-into-target).
            (TODO stub.)
  layout    ast -> ast in place. LLVM mangled names, field offsets, struct
            sizes, vtable layouts. (TODO stub.)
  codegen   ast -> .ll text. Reads inferred_type / op_type stamped by
            classify (no longer derives or recomputes types). SymTab keyed
            by parse::Tree::entries index; every ident / lvalue node carries
            its resolved_entry_id, so codegen does no string-keyed lookup.
            Function definitions emit param allocas + stores from %arg.N
            into named registers; calls emit `call <ret> @name(<typed
            args>)` via one shared emitCall using the classify-stamped
            return_type and param_types — the statement form discards the
            result register, the expression form (kCallExpr) widens it into
            the destination type. ALL local allocas are HOISTED to the function
            entry block (emitFunction pre-walks the body via collectVarDecls): an
            alloca emitted at its declaration site would re-allocate stack on
            every pass through an enclosing loop — unbounded growth → stack
            overflow (a v1 bug); the entry block runs once. Each uses the register
            name `%<name>.<entry_id>` — the entry-id suffix is the SHADOWING-SAFETY
            mechanism: a shadowing inner-scope local (`int x; { int x; }`) gets a
            distinct register from the outer `%x`, so llc doesn't reject a
            duplicate local value name. The id-keyed SymTab resolves each read to
            the right entry, so the suffix is a deterministic local derivation,
            NOT a cached canonical name (writer and reader agree by entry id; the
            string is never stored or re-derived elsewhere); the kVarDeclStmt arm
            then emits only the initializer store. Logical && / || / ^^
            (emitLogical) lower to PHI nodes, NOT alloca — an alloca there would
            land in a loop-header block when the logical is a loop condition and
            leak a slot per iteration (same class); the short-circuit + rhs edges
            route through dedicated known-label blocks so the phi predecessors are
            always valid. A kBlockStmt is transparent (emit children in order). A
            kIfStmt emits emitToBool on the condition + a conditional br to
            then/else/merge labels (no phi — definite-assignment rides the hoisted
            allocas); an arm ending in a control transfer (return / break /
            continue) emits no br-to-merge, and when every arm transfers the merge
            block is omitted entirely (resolve's 2A guarantees nothing live
            follows). A kWhileStmt is head/body/exit (test-first); a kDoWhileStmt
            is body/cond/exit (body-first, test after); a kForLongStmt runs the
            varlist init stores once (allocas hoisted) then head(cond)/body/update
            /exit (continue -> update, so the loop var still advances on continue).
            break -> exit, continue -> head (while) / cond (do-while) / update
            (for), via a LoopCtx { header, exit, outer } LINKED LIST threaded
            through emitStmt; kBreakStmt / kContinueStmt walk Node.loop_levels
            `outer` hops (resolve-stamped, asserted in range) to the target frame
            and branch to its exit (break) / header (continue) — so a labeled /
            numbered break reaches a non-innermost loop. The body's
            back-edge is emitted only if it can fall through (endsTerminated).
            A kSwitchStmt lowers to an `llvm switch` on the scrutinee dispatching
            to one block per clause (source order, default's block = the switch
            instr default, or exit when there is no default); each block emits its
            body then falls through via br to the next clause (or exit) unless
            terminated, so C-style fall-through is the natural block layout. A
            clause's LoopCtx inherits the enclosing loop's header (continue passes
            through) but overrides exit = the switch exit (naked break). The exit
            block gets an `unreachable` terminator only when nothing reaches it
            (no escaping break via containsBreak, no bottom-fall, has a default).
            endsTerminated / endsTerminatedNode (return + break + continue, and a
            block / both-armed-if whose paths all do) drive the if-arm and loop-
            back br decisions; a loop is never terminating (it reaches its exit).
            emitFunction's trailing-return terminator decision uses a separate
            ast-side endsInReturn / endsInReturnNode (return only) that recurses
            into a trailing block and a both-arms-return if, mirroring classify.
            Mangled names and field offsets land
            with layout.
              Pointers & arrays (Phase 4). References (`T^`) and iterators
            (`T[]`) are both LLVM `ptr`; `anyptr` (nullptr) too. kAddrOfExpr
            `^var` is the operand's alloca register (no load); kDerefExpr loads
            the pointer then loads the pointee. A fixed-size array `T name[N]`
            is an aggregate alloca, with multi-dim REVERSED (`int[3][5]` ->
            `[5 x [3 x i32]]`); emitElementAddr walks a kIndexExpr chain to the
            base and emits ONE getelementptr with the indices outer-to-inner
            (the leftmost source index is innermost) — it also rejects a partial
            index (chain length must equal the dimension count). An ITERATOR
            subscript instead loads the pointer and GEPs by element type.
            kStoreStmt stores through any lvalue expr (deref / index). Move /
            swap (kMoveStmt / kSwapStmt, `a <-- b` / `a <--> b`) are lowered HERE
            (they pass through desugar untouched). A MOVE copies the rhs into the
            lhs via emitExpr(rhs, dest=lhs type)+store (so it reuses the widen /
            implicit-pointer-cast / whole-tuple machinery, exactly an assignment),
            then emitNullLeaves walks the rhs's structured type and `store ptr
            null`s every pointer / iterator leaf — recursing by GEP into nested
            tuple slots (the "fancy case"), so the source is left valid; an rvalue
            source (isAstLvalue false) has no address and is a pure copy. A SWAP
            loads both lvalues into SSA temporaries and stores them crossed (no
            stack temp; a whole-value load/store handles tuples; both loads precede
            either store, so an aliased swap is safe). emitLvalueAddr gives an
            operand's address (a bare var's alloca / emitElementAddr / a deref's
            pointer); a move-init decl (`T x <-- y`) nulls after the var-decl store.
            Iterator
            arithmetic: `iter ± int` is a signed element GEP, `iter - iter` is
            ptrtoint-diff / elemBytes, `++`/`--` GEP ±1 element. Pointer
            comparisons icmp the raw pointers (unsigned). A kCastExpr
            (`<Type^> x`) emits the operand then widen::convert twice
            (operand->target->dest); convert's pointer head makes ptr<->ptr a
            value no-op (opaque `ptr`), ptr->intptr a `ptrtoint`, intptr->ptr an
            `inttoptr` — and asserts if a pointer ever reaches the SCALAR
            conversion path (an ungated ptr->non-intptr would store a `ptr` as an
            integer; classify rejects them, so this guards against a missed gate).
            A kConvertExpr (`(Type = expr)`) mirrors the cast shape: emit the
            operand at its own type, then widen::convertExplicit (operand->target)
            then widen::convert (target->dest). convertExplicit is the FULL value
            grid (sibling to convert, which only widens): trunc / sext / zext /
            fptrunc / fpext / fptosi / fptoui / sitofp / uitofp, a same-width int
            change is a no-op (sign reinterpret is free), `->bool` is a nonzero
            (`icmp`/`fcmp une`) / non-null test, a pointer source is `ptrtoint`
            (->intptr) or the non-null test (->bool). It never reports — classify
            pre-validated — and asserts on any state classify should have caught.
            kNewExpr: `new T` -> `call ptr @malloc(i64 sizeof(T))`; `new T[n]` ->
            `mul i64 n, sizeof(T)` then malloc; placement (children[1]) -> the
            address itself, no allocation (primitives construct nothing). An
            assert guards an unsized element (classify gated it). kDeleteStmt:
            load the pointer, `call void @free(ptr)`, store null back to its
            alloca. malloc / free are declared in the module preamble next to
            printf. No destructors run (Phase 5).

PRODUCT FILES (.h / .cpp pairs)

  token     Token { kind, text, file_id, line, col, length }. List owns the
            token vector AND a per-file registry { path, source, line_starts,
            imported_by }. APIs: add(), openFile(). Deque-backed file table
            so Stream pointers stay stable across imports.
  parse     parse-tree node types + tree storage + build/walk/annotate APIs.
            Nodes carry: nominal_type (literals, populated by constfold),
            inferred_type + op_type (expressions, populated by classify),
            resolved_entry_id (idents / lvalues / callees, populated by
            resolve), name_tok (ident token of named constructs — VarDecl,
            FunctionDef/Decl, Param; populated by grammar so entry.tok
            carets at the ident rather than the const/type keyword),
            is_const (kVarDeclStmt: declared with leading `const`),
            params (kFunctionDef/Decl: ordered kParam children),
            param_types (kCallStmt/kCallExpr: cached resolved-fn param-type
            strings driving each arg's emit dest_type). Owns the symbol table:
            Entry vector + frame stack + pushFrame / popFrame / addEntry /
            findInLiveScopes / findInFrame / entryType APIs that resolve
            calls. Function entries carry param_types alongside their
            return type, plus num_required (optional-param boundary) and a
            def_tok/def_file_id pair (the first DEFINITION's position,
            distinct from tok = the first DECLARATION) so "first defined
            here" and "first declared here" notes caret the right
            occurrence across a forward decl + later body; kConst entries
            carry literal_text + literal_kind
            (filled by constfold; read by constfold at substitution sites).
            Stage-vs-product rule: stages make decisions, parse owns
            storage and lookup.
  ast       ast node types (separate set from parse) + tree storage +
            build/walk/annotate APIs. Nodes carry nominal_type +
            inferred_type + op_type + resolved_entry_id + params +
            param_types propagated from parse; future: mangled names +
            layout offsets (populated by layout) + back-pointers to parse
            for source attribution.

PLUMBING

  main.cpp        Pipeline orchestrator. Parses argv (-o, -I), opens the
                  root via lex::run, walks the seven stages, prints
                  diagnostics + exits 1 if any errors, otherwise runs
                  codegen.
  diagnostic.h/.cpp  Record { file_id, tok, message, notes }, Note (same
                     shape), Sink (vector of records). APIs: report(),
                     hasErrors(), render(). render() walks the unified
                     token::List to look up source line/col/length and the
                     file registry for path + context lines + caret-sled +
                     imported-by chain. Caret-sled is bracketed at both
                     ends: length 1 → `^`; length 2 → `^^`; length N → `^---^`
                     (v1-style). Color-gated on isatty + NO_COLOR. Messages
                     are sentence-shaped throughout: capital + period.
  Makefile        -std=c++17 -Wall -Wextra -Werror -Wswitch-enum.
                  Builds to ../bin/slidsc. Objects in ../build/compiler_v2/.
