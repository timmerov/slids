compiler_v2 — slids compiler rewrite, in-progress

PIPELINE (each stage consumes its predecessor's output, produces its successor's input):

    source text
      => lex       => token::List
      => numeric   => token::List (literal tokens canonicalized + validated)
      => grammar   => parse::Tree
      => constfold => parse::Tree (literal sub-trees collapsed + nominal-typed)
      => classify  => parse::Tree annotated (symbol refs + types)
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
            (Done; numeric handoff TODO.)
  numeric   tokens -> tokens. Validates and canonicalizes literal tokens:
            interprets char escapes ('A' -> 65, '\\n' -> 10); parses hex/
            binary to decimal (0xFF -> 255, 0b1010 -> 10); canonicalizes
            float text (%.17g). Detects overflow assuming uint64 / float64
            storage; more overflow detection happens downstream at target-
            type fit checks. Classify operates on the canonical-string
            tokens; desugar is the first stage to materialize typed values
            on AST nodes. (TODO stub.)
  grammar   tokens -> parse tree. Pure syntax; every identifier is just a
            name. (TODO stub.)
  constfold parse tree -> parse tree. Post-order walker. Assigns
            nominal_type to every literal per fold.sl:16-23 (bool=uint1,
            char=uint8, integer/unsigned by smallest-bit-tier, float by
            float32-round-trip). Folds unary on literal (rules 1a-1f) and
            binary on two integer-class literals (signed int64 arithmetic
            for + - * / % & | ^). Rejects div/mod by literal zero and
            `~float` literal; flips `-uint_lit` kind to kIntLiteral per
            rule 1d. Pending: float binary fold, shift fold, comparison
            fold, algebraic identity simplifications, rule-6 overflow-to-
            unsigned exception.
  classify  parse tree -> annotated parse tree. Resolves every identifier
            to a symbol-table entry (parse::Entry; LocalVar + Function kinds
            today); infers every expression's type (parse::Node::inferred_type)
            and every binary's computational type (parse::Node::op_type).
            Validates declared/return type spellings (widen::isKnownType).
            Sharp rejections at the source: wrong-kind entry (assign to
            function / call a variable), non-coercible operands for ! && || ^^,
            non-numeric shift sides, bitwise on float, no-common-type binaries,
            duplicate decls, signature mismatch. Multi-source notes point at
            the prior decl for the duplicate / mismatch families. Overload
            resolution unimplemented (no overloads today).
  desugar   parse tree -> ast (separate node-type set). Collapses syntactic
            variants (for-loop shapes, receiver shapes, PPID, stringify,
            operator dispatch) into canonical forms. AST nodes hold back-
            pointers to parse for source attribution. (TODO stub.)
  optimize  ast -> ast in place. Slids-aware perf rewrites LLVM can't do
            (compound-fuse, NRVO, identity-temp adoption, build-into-target).
            (TODO stub.)
  layout    ast -> ast in place. LLVM mangled names, field offsets, struct
            sizes, vtable layouts. (TODO stub.)
  codegen   ast -> .ll text. Reads inferred_type / op_type stamped by
            classify (no longer derives or recomputes types). String-keyed
            SymTab lookups for ident loads remain today; resolved_entry_id
            is present on every ident but not yet consumed (deferred
            refactor). Mangled names and field offsets land with layout.

PRODUCT FILES (.h / .cpp pairs)

  token     Token { kind, text, file_id, line, col, length }. List owns the
            token vector AND a per-file registry { path, source, line_starts,
            imported_by }. APIs: add(), openFile(). Deque-backed file table
            so Stream pointers stay stable across imports.
  parse     parse-tree node types + tree storage + build/walk/annotate APIs.
            Nodes carry: nominal_type (literals, populated by constfold),
            inferred_type + op_type (expressions, populated by classify),
            resolved_entry_id (idents / lvalues / callees, populated by
            classify). Owns the symbol table: Entry vector + frame stack +
            pushFrame / popFrame / addEntry / findInLiveScopes / findInFrame /
            entryType APIs that classify calls. Stage-vs-product rule:
            classify makes decisions, parse owns storage and lookup.
  ast       ast node types (separate set from parse) + tree storage +
            build/walk/annotate APIs. Nodes carry nominal_type +
            inferred_type + op_type + resolved_entry_id propagated from
            parse; future: mangled names + layout offsets (populated by
            layout) + back-pointers to parse for source attribution.

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
