compiler_v2 — slids compiler rewrite, in-progress

PIPELINE (each stage consumes its predecessor's output, produces its successor's input):

    source text
      => lex      => token::List
      => grammar  => parse::Tree
      => classify => parse::Tree annotated (symbol refs + types)
      => desugar  => ast::Tree
      => optimize => ast::Tree annotated (perf rewrites in place)
      => layout   => ast::Tree annotated (mangled names + offsets)
      => codegen  => .ll text

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
            return. (Done.)
  grammar   tokens -> parse tree. Pure syntax; every identifier is just a
            name. (TODO stub.)
  classify  parse tree -> annotated parse tree. Resolves every identifier
            to a symbol-table entry; infers every expression's type;
            overload resolution. (TODO stub.)
  desugar   parse tree -> ast (separate node-type set). Collapses syntactic
            variants (for-loop shapes, receiver shapes, PPID, stringify,
            operator dispatch) into canonical forms. AST nodes hold back-
            pointers to parse for source attribution. (TODO stub.)
  optimize  ast -> ast in place. Slids-aware perf rewrites LLVM can't do
            (compound-fuse, NRVO, identity-temp adoption, build-into-target).
            (TODO stub.)
  layout    ast -> ast in place. LLVM mangled names, field offsets, struct
            sizes, vtable layouts. (TODO stub.)
  codegen   ast -> .ll text. No string-keyed lookups; consumes symbol
            references + pre-computed mangled names. (TODO stub.)

PRODUCT FILES (.h / .cpp pairs)

  token     Token { kind, text, file_id, line, col, length }. List owns the
            token vector AND a per-file registry { path, source, line_starts,
            imported_by }. APIs: add(), openFile(). Deque-backed file table
            so Stream pointers stay stable across imports.
  parse     parse-tree node types + tree storage + build/walk/annotate APIs.
            Nodes will carry optional resolved-symbol refs + inferred types
            (populated by classify). (TODO stub.)
  ast       ast node types (separate set from parse) + tree storage +
            build/walk/annotate APIs. Nodes will carry mangled names + layout
            offsets (populated by layout) + back-pointers to parse for
            source attribution. (TODO stub.)

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
