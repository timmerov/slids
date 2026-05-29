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
            kind to kUintLiteral here. Two open items remain on the
            migration table (proper attribution for uint/char literal-fit
            errors; float32 hex bit-pattern emit at codegen).
  grammar   tokens -> parse tree. Pure syntax; every identifier is just a
            name string. Hand-written recursive descent. Parses: types
            (built-in primitives + T[]); function defs/decls with typed
            param lists; statements (var-decl, assign, aug-assign, 0/1/N-
            arg call, return); expressions across the full C precedence
            ladder (literals + ident, unary `! ~ + -`, full binary set
            arith/bitwise/shift/comparison/logical, parens). Stamps
            (file_id, tok) on every node for source attribution. No
            identifier resolution, no scope tracking, no type inference,
            no literal folding — all deferred to later stages. Errors are
            single-shot ("expected '...'") with caret at the offending
            token; sets fatal + early-returns up the call chain.
  resolve   parse tree -> annotated parse tree. Builds the symbol table
            (parse::Entry vector on parse::Tree) and resolves every
            identifier-use to a resolved_entry_id. Pushes/pops frames at
            scope-opening nodes (program, function-body today; block /
            class / namespace as Phase 2+ land). Two passes per scope to
            preserve forward-decl semantics (collect decls, then walk
            bodies). Substitutes kIdentExpr -> literal node in place
            when the resolved entry is kConst, so downstream constfold
            sees the literal already. Sharp diagnostics at the source:
            wrong-kind entry (assign to function / call a variable),
            duplicate decls, signature mismatch, multi-source notes
            pointing at prior decls. Owns the bool "this name resolves
            to X" decision; types are not resolve's job. (Planned
            restructure — today its responsibilities live in classify;
            the split lets constfold consume named constants.)
  constfold parse tree -> parse tree. Post-order walker. Assigns
            nominal_type to every literal per fold.sl:16-23 (bool=uint1,
            char=uint8, integer/unsigned by smallest-bit-tier, float by
            float32-round-trip). Folds unary on literal (rules 1a-1f) and
            binary on two literals across all op families: int arith /
            bitwise (signed int64 with rule-6 overflow-to-uint64), int
            shifts (count >= width → 0; uint64 reinterpret to avoid UB),
            int comparisons (int64 path with uint64 fallback), float
            arith / shift / comparison (double + %.17g canonical text;
            pow2 mul/div lowering for float shifts). Rejects div/mod by
            literal zero, `~float`, float `& | ^`, shift with float rhs
            or negative count. Pending: algebraic identity simplifications
            and logical-with-constant (both deferred until purity tracking
            lands with PPID / function calls).
  classify  parse tree -> annotated parse tree. Designed scope: type
            inference + overload resolution. Reads resolved_entry_id +
            entry data stamped by resolve; never builds entries or pushes
            frames itself. Infers every expression's inferred_type and
            every binary's op_type (computational type). Validates
            declared / return / parameter type spellings (widen::isKnownType).
            Sharp rejections at the source: non-coercible operands for
            ! && || ^^, non-numeric shift sides, bitwise on float,
            no-common-type binaries, arity mismatch against the resolved
            callee's param_types, multi-arg print intrinsic. Future:
            overload resolution (when multiple Function entries share a
            name, picks one by arg types).

            (Today's reality — pending the resolve split: classify still
            owns the symbol-resolution job too. The split is documented in
            the pipeline so future restructuring lands against the design,
            not against the current code.)
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
            into named registers; call statements emit `call <ret> @name(
            <typed args>)` using the classify-stamped return_type and
            param_types from the resolved Function entry. Mangled names
            and field offsets land with layout.

PRODUCT FILES (.h / .cpp pairs)

  token     Token { kind, text, file_id, line, col, length }. List owns the
            token vector AND a per-file registry { path, source, line_starts,
            imported_by }. APIs: add(), openFile(). Deque-backed file table
            so Stream pointers stay stable across imports.
  parse     parse-tree node types + tree storage + build/walk/annotate APIs.
            Nodes carry: nominal_type (literals, populated by constfold),
            inferred_type + op_type (expressions, populated by classify),
            resolved_entry_id (idents / lvalues / callees, populated by
            resolve), params (kFunctionDef/Decl: ordered kParam children),
            param_types (kCallStmt: cached resolved-fn param-type strings
            driving each arg's emit dest_type). Owns the symbol table:
            Entry vector + frame stack + pushFrame / popFrame / addEntry /
            findInLiveScopes / findInFrame / entryType APIs that resolve
            calls. Function entries carry param_types alongside their
            return type. Stage-vs-product rule: stages make decisions,
            parse owns storage and lookup.
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
                     file registry for path + context lines + caret +
                     imported-by chain. Color-gated on isatty + NO_COLOR.
  Makefile        -std=c++17 -Wall -Wextra -Werror -Wswitch-enum.
                  Builds to ../bin/slidsc. Objects in ../build/compiler_v2/.
