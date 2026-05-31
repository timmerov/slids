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
            looksLikeQualifiedTypedDecl lookahead routes `Space:Dir x` to a
            var-decl (vs `Space:foo()` / `Space:kX = 1`, name-led statements);
            `alias Name = type;` + bare `alias Ns;` decls; namespace decls
            (`Name { members }`) and inline qualified member decls
            (`const int Space:kSix = 6;`); enum decls
            (`enum [type] [Name] ( m1 [= v], ... )`); function defs/decls with typed
            param lists; var-decls with optional leading `const` (file
            scope or function scope); statements (var-decl incl. the
            `<ident> <ident>` typed-decl shape, assign, aug-assign, alias,
            namespace decl, 0/1/N-arg call possibly qualified, bare inc/dec,
            return — a qualified name leading a statement routes through one
            parseNameLedStmt); expressions
            across the full C precedence ladder (literals + ident, unary
            `! ~ + -`, prefix/postfix ++/--, full binary set
            arith/bitwise/shift/comparison/logical, parens, postfix-call on
            a bare ident). Stamps (file_id, tok)
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
            types and aliases never reach the ast. A namespace-qualified type
            spelling (`Space:Dir`) resolves via resolveQualifiedType (the lead
            segments walk the shared ns chain, the leaf must be a type) before
            any downstream stage; the cycle-vs-resolution-failure suppression
            flag is named `reported`.
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
            itself. Infers every expression's inferred_type and every
            binary's op_type (computational type). Sharp rejections at
            the source: non-coercible operands for ! && || ^^, non-numeric
            shift sides, bitwise on float, no-common-type binaries.
            Per-arg type inference at call sites uses the resolved
            callee's param_types (cached on the kCallStmt/kCallExpr by
            resolve) as context. A kCallExpr's inferred_type is the
            callee's return type; a void return used as a value is rejected
            here. Future: overload resolution when multiple Function
            entries share a name.
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
            bump never fires. Future canonical-form rewrites (for-loop
            shapes, receiver shapes, stringify, operator dispatch) slot in
            as their phases land.
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
            the destination type. Mangled names and field offsets land with
            layout.

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
            return type; kConst entries carry literal_text + literal_kind
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
