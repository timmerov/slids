# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Rules

- Never run any git command unless the user explicitly asks.
- **Scope is strictly limited to the test file or specific lines named. Reading or modifying anything outside that scope is not permitted.**
- **Do not build or run anything outside the named scope.** If the scope is a single sample, build and run only that sample. If the scope is specific lines, do not consider issues outside those lines.
- **Discussion is not permission to change code.** Explicit instruction is required: "do it", "make the changes", "go ahead", etc. "We will do X" and "X needs to happen" are not instructions to act.
- During discussion, reading files and summarizing findings is permitted. Nothing else.
- After reading, state what needs to change — then stop.
- Do not ask for permission to change code. Ask clarifying questions about intent instead.
- Test code may have unrelated issues. Ignore them. Fix one thing at a time.

## What this is

Slids is a compiled, systems-level programming language. Source files (`.sl`) compile to LLVM IR (`.ll`), then to native object files via `llc`, then linked with `g++`. The compiler (`slidsc`) is written in C++17.

## Build commands

```bash
# Build everything (compiler + all samples)
./make.sh

# Build only the compiler
cd compiler && make

# Build only samples
cd sample && make

# Build a single sample
cd sample && make hello1

# Manually compile a .sl file end-to-end
./bin/slidsc foo.sl -o foo.ll
llc --filetype=obj --relocation-model=pic foo.ll -o foo.o
g++ foo.o -o foo
./foo
```

## Testing

There is no automated test suite. Sample programs in `sample/` are the regression tests. Build them with `cd sample && make` and run the binaries in `bin/`. Each sample exercises specific language features (see the Makefile for the full list).

Work-in-progress tests live in `work/`. Build with `cd work && make`. The `vector1` target exercises cross-TU template class instantiation.

## Compiler pipeline

```
.sl source → Lexer → Token stream → Parser → AST → Codegen → LLVM IR (.ll)
```

- `main.cpp` — entry point; reads source, runs lexer → parser → codegen
- `lexer.cpp` / `token.h` — tokenizer; handles keywords, literals, comments, line tracking
- `parser.cpp` / `parser.h` — recursive descent parser; produces AST (`Expr`, `Stmt`, `SlidDef`, `FunctionDef`, etc.)
- `codegen.cpp` — orchestrates IR emission; Phase 1: collect string constants + signatures; Phase 2: analyze nested function captures; Phase 3: emit LLVM IR
- `codegen_expr.cpp` — expression codegen (binary/unary ops, calls, field access, allocation)
- `codegen_stmt.cpp` — statement codegen (declarations, assignments, control flow, destructors)
- `codegen_helpers.h` — type predicates and constant evaluation utilities

## Language concepts

**Slids** are the one unified construct — every function, class, method, and constructor is a slid. Calling a slid executes its body and returns an instance of itself. Nested slids are methods.

**Pointer types:** `^` is a reference (no arithmetic); `[]` is an iterator (arithmetic allowed). Dereference with `^` suffix: `ptr^.field`.

**Memory:** `new Type(init)` / `new Type[n]` for heap allocation. `delete ptr` nullifies pointer after freeing. Destructors are called in reverse declaration order on return.

**File model:** `.slh` headers are public contracts; `.sl` files are private implementations. No forward declarations needed within a `.sl` file — the compiler does two passes.

**Templates across translation units:** When a template function or class is imported, its body is loaded from the adjacent `.sl` impl file at import time. Uses in the consumer TU emit only struct type definitions and `declare` stubs; the compiler records needed instantiations in a `.sli` file. After all sources are compiled, `slidsc --instantiate <build-dir> -o __instantiations.sl` aggregates the `.sli` files and writes a source file with explicit `instantiate Foo<Bar>;` statements. Compiling that file emits the full method bodies. Empty `.sli` files are not written. Exception: if any type argument is a locally-defined (non-importable) slid, that instantiation is emitted inline in the consumer TU instead.

**Enums, operator overloading, nested functions with capture, labeled break/continue, and `@foreign` for C interop** are all supported. See `slids_reference.md` for full syntax.

## Codegen internals

The codegen class tracks:
- `locals_` — map from variable name to alloca register
- `slid_info_` — field name → index and type for each slid type
- `func_return_types_` / `func_param_types_` — function signatures
- `dtor_vars_` — stack of variables needing cleanup on return
- `captures_` — variables captured by nested functions

Template support:
- `template_funcs_` / `template_slids_` — template definitions indexed by base name
- `local_template_names_` / `local_slid_template_names_` — templates whose body is defined in this TU (always inlined)
- `template_func_modules_` / `slid_template_modules_` — imported template name → module (deferred to instantiator)
- `pending_slid_instantiations_` — concrete class template instances to emit full bodies for
- `pending_slid_declares_` — imported class template instances: emit struct type + declares only
- `pending_instantiations_` — concrete function template instances to emit
- `pending_declares_` — imported function template instances: emit declares only

The explicit-instantiation dispatch (`program_.instantiations`) runs before `collectStringConstants()` so that instantiated bodies are included in the string-constant pre-scan and in the ctor/dtor/method emit loops.

Type mapping: `int` → `i32`, `int64` → `i64`, `bool` → `i1`, `float32` → `float`, `float64` → `double`, pointer types → `ptr`.
