compiler_v2 — slids compiler rewrite, in-progress

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
            (built-in primitives + T[]); function defs/decls with typed
            param lists; var-decls with optional leading `const` (file
            scope or function scope); statements (var-decl, assign,
            aug-assign, 0/1/N-arg call, return); expressions across the
            full C precedence ladder (literals + ident, unary `! ~ + -`,
            full binary set arith/bitwise/shift/comparison/logical,
            parens, postfix-call on a bare ident). Stamps (file_id, tok)
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
            class / namespace as Phase 2+ land). Pass 1a collects
            program-scope entries (Functions + Consts) without walking
            init expressions; pass 1b walks file-scope const init rhs
            (so globals can reference each other regardless of decl
            order); pass 2 walks function bodies. Validates declared /
            return / parameter type spellings (widen::isKnownType).
            Caches lvalue type on AugAssignStmts (s.return_type) and
            return type + param_types on CallStmts/CallExprs (one shared
            resolveUserCall) so downstream stages don't have to re-walk the
            entry table. Sharp diagnostics at the source: wrong-kind entry
            (assign to function / assign to constant / call a variable),
            duplicate decls, return-type mismatch, parameter-type mismatch,
            duplicate definition, arity mismatch, multi-arg print intrinsic,
            print intrinsic used as an expression. Multi-source
            notes point at prior decls. Owns the "what does this name
            refer to" decision; types are not resolve's job.
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
            Pending: algebraic identity simplifications and logical-
            with-constant (both deferred until purity tracking lands
            with PPID).
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
            params, param_types, file_id, tok). One real rewrite is live:
            aug-assign (`lhs op= rhs`) -> `lhs = lhs op rhs`, with the
            synthesized IdentExpr + BinaryExpr inheriting the aug-assign's
            classify-stamped types so codegen sees the rewrite as if it
            were classified directly. Future canonical-form rewrites
            (for-loop shapes, receiver shapes, PPID, stringify, operator
            dispatch) slot in as their phases land.
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
