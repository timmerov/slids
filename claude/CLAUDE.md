# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Rules

Three modes govern how I work. The user sets the mode by what they ask for. I default to Discussion when the mode is unclear or has just shifted.
- Do not read or access any file outside of /home/timmer/Documents/code/slids/claude ever.
- Use of git is prohibited.
- Assume discussion mode. Always go to discussion mode when the user asks a question.

### Tool preferences

- Use of `cd` is not permitted. Use `make -C <dir>` instead of `cd <dir> && make`.

### Modes

- **Discussion** — clarifying intent, exploring the code, proposing approaches. Read files, run builds, run targets. Do **not** modify source files.
- **Bug/Fix** — a specific defect with a known scope (file, lines, or error message). Make the minimum change that fixes it. Don't refactor, generalize, or fix nearby unrelated issues.
- **Feature/Design** — a new capability or design change. Assume scope is broad. Actively apply stated principles beyond the literal example. Look for places the same rule should land.

### Mode transitions

- Switch to Discussion when the user changes scope, changes context, or points at a different test file.
- When the user says a test file was updated, reread it and switch to Discussion.

### Workflow within any mode

- After understanding the request, write a short summary of what will change. Wait for an explicit "go" / "do it" / "fix it" before editing files. (Provide a summary, wait for go — applies to all modes, including Bug/Fix.)
- Once the user has said "go", do the work without asking for incremental permission. Don't ask "should I also..." for cases the original instruction or a stated principle covers; just do them.
- Ask clarifying questions about *intent* (what should happen, what semantics are wanted). When handing a proposal back, phrase it as a statement the user confirms or redirects — "say go to apply", "tell me which subset" — not a question that puts the decision on them — "want me to make these changes?".
- In Bug/Fix mode, the test file may contain unrelated issues. Don't touch them.
- Don't modify any `.sl` file the user hasn't named as the context for this work, even if you spot issues in it. The user names which test file(s) are in scope when establishing context; everything else is out of bounds. (Safety net for stale files in the working tree.)

## What this is

Slids is a compiled, systems-level programming language. Source files (`.sl`) compile to LLVM IR (`.ll`), then to native object files via `llc`, then linked with `g++`. The compiler (`slidsc`) is written in C++17.

## Build commands

```bash

# Build only the compiler
make -C compiler

# Build only samples
make -C sample

# Build a single sample
make -C sample hello1

# Manually compile a .sl file end-to-end
./bin/slidsc foo.sl -o foo.ll
llc --filetype=obj --relocation-model=pic foo.ll -o foo.o
g++ foo.o -o foo
./foo
```

## Testing

Test nothing other than the test files in scope.

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
- `codegen_template.cpp` — template instantiation (function and class templates, expression cloning across TU)
- `codegen_helpers.h` — type predicates and constant evaluation utilities

## Language concepts

**Slids** are the one unified construct — every function, class, method, and constructor is a slid. Calling a slid executes its body and returns an instance of itself. Nested slids are methods.

**Pointer types:** `^` is a reference (no arithmetic); `[]` is an iterator (arithmetic allowed). Dereference with `^` suffix: `ptr^.field`.

**Memory:** `new Type(init)` / `new Type[n]` for heap allocation. `delete ptr` nullifies pointer after freeing. Destructors are called in reverse declaration order on return.

**File model:** `.slh` headers are public contracts; `.sl` files are private implementations. No forward declarations needed within a `.sl` file — the compiler does two passes.

**Templates across translation units:** When a template function or class is imported, its body is loaded from the adjacent `.sl` impl file at import time. Uses in the consumer TU emit only struct type definitions and `declare` stubs; the compiler records needed instantiations in a `.sli` file. After all sources are compiled, `slidsc --instantiate <build-dir> -o __instantiations.sl` aggregates the `.sli` files and writes a source file with explicit `instantiate Foo<Bar>;` statements. Compiling that file emits the full method bodies. Empty `.sli` files are not written. Exception: if any type argument is a locally-defined (non-importable) slid, that instantiation is emitted inline in the consumer TU instead.

**Enums, operator overloading, nested functions with capture, and labeled break/continue** are all supported. See `slids_reference.md` for full syntax. (`@foreign` for C interop is mentioned in the reference doc but is not yet implemented.)

## Codegen internals

The codegen class tracks:
- `locals_` — map from variable name to alloca register
- `slid_info_` — field name → index and type for each slid type
- `func_return_types_` / `func_param_types_` — function signatures
- `dtor_vars_` — stack of `DtorVar { var_name, slid_type, tuple_index }`. `tuple_index >= 0` means the dtor target is a GEP into the named tuple variable at that field index (used for slid-typed tuple elements). Plain locals have `tuple_index == -1`.
- `captures_` — variables captured by nested functions

Canonical helpers:
- `emitConstructAt(stype, ptr, args, overrides)` / `emitConstructAtPtrs(...)` — initializes a slid in place. Recurses into slid-typed fields: with no arg → default-construct; with a same-type arg → `emitSlidAssign` copy; with a non-matching arg → recurse using that single arg as the field-slid's ctor input (e.g. `NestedMove(42, ...)` → `a_ = Move(42)`).
- `emitSlidAssign(slid, dst, src, is_move)` — per-field copy/move dispatcher. For embedded slid fields, looks up `op<-` (move) or `op=` (copy) taking `Type^` and calls it if defined; otherwise recurses default. Pointer/iterator fields: load + store; if `is_move`, store null at source. Inline arrays of slids/pointers are walked element-wise with the same dispatch.

Template support:
- `template_funcs_` / `template_slids_` — template definitions indexed by base name
- `local_template_names_` / `local_slid_template_names_` — templates whose body is defined in this TU (always inlined)
- `template_func_modules_` / `slid_template_modules_` — imported template name → module (deferred to instantiator)
- `pending_slid_instantiations_` — concrete class template instances to emit full bodies for
- `pending_slid_declares_` — imported class template instances: emit struct type + declares only
- `pending_instantiations_` — concrete function template instances to emit
- `pending_declares_` — imported function template instances: emit declares only

The explicit-instantiation dispatch (`program_.instantiations`) runs before `collectStringConstants()` so that instantiated bodies are included in the string-constant pre-scan and in the ctor/dtor/method emit loops.

Type mapping: `int` → `i32`, `int64` → `i64`, `bool` → `i1`, `float32` → `float`, `float64` → `double`, pointer types → `ptr`, slid type name → `%struct.<Name>`, anonymous tuple `(t1,t2,...)` → literal struct `{ llvm(t1), llvm(t2), ... }`.
