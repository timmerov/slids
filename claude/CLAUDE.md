# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Rules

Three modes govern how I work. The user sets the mode by what they ask for. I default to Discussion when the mode is unclear or has just shifted.
- Do not read or access any file outside of /home/timmer/Documents/code/slids/claude ever.
- Use of git is prohibited.
- Assume discussion mode. Always go to discussion mode when the user asks a question.
- A `?` anywhere in the user's message is shorthand for "switch to discussion mode" — even if the message isn't grammatically a question.
- Act more like a tool than a senior developer.

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

### Session shape

Default arc of a session:
1. **User presents file(s).** Bug repros or feature stubs. **Only the user's own comment(s) in those files are canon** — the test code is not. Barring typos, the user's comment is the source of truth — the test code, memories, TODO.md, and existing compiler code are all subordinate. When any of those conflicts with the user's comment, the comment wins.
2. Discussion.
3. Likely: tests get added to the presented file.
4. Compiler change lands.
5. User runs tests against files you can't see. Trust the report.
6. *If asked*, discuss documentation.

Tests are **not** canon — only the user's comment is. Tests still go first in the work order (written before the compiler change). Documentation is the closing item, and only when the user raises it. Do not volunteer doc updates, do not propose them mid-flow, do not spend turns explaining what the docs should say. The default time budget for documentation is zero.

## What this is

Slids is a compiled, systems-level programming language. Source files (`.sl`) compile to LLVM IR (`.ll`), then to native object files via `llc`, then linked with `g++`. The compiler (`slidsc`) is written in C++17.

## Build commands

```bash

# Build only the compiler
make -C compiler

# Build only tests
make -C test

# Build a single test
make -C test/misc hello

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

see readme.txt.

## Language concepts

**Slids** are the one unified construct — every function, class, method, and constructor is a slid. Calling a slid executes its body and returns an instance of itself. Nested slids are methods.

**Pointer types:** `^` is a reference (no arithmetic); `[]` is an iterator (arithmetic allowed). Dereference with `^` suffix: `ptr^.field`.

**Memory:** `new Type(init)` / `new Type[n]` for heap allocation. `delete ptr` nullifies pointer after freeing. Destructors are called in reverse declaration order on return.

**File model:** `.slh` headers are public contracts; `.sl` files are private implementations. No forward declarations needed within a `.sl` file — the compiler does two passes.

**Templates across translation units:** When a template function or class is imported, its body is loaded from the adjacent `.sl` impl file at import time. Uses in the consumer TU emit only struct type definitions and `declare` stubs; the compiler records needed instantiations in a `.sli` file. After all sources are compiled, `slidsc --instantiate <build-dir> -o __instantiations.sl` aggregates the `.sli` files and writes a source file with explicit `Foo<Bar>(ParamTypes);` statements (a bare-name template-call shape — there is no `instantiate` keyword). Compiling that file emits the full method bodies. Empty `.sli` files are not written. Exception: if any type argument is a locally-defined (non-importable) slid, that instantiation is emitted inline in the consumer TU instead.

**Enums, operator overloading, nested functions with capture, and labeled break/continue** are all supported. See `slids_reference.md` for full syntax.

## Codegen internals

The codegen class tracks:
- `locals_` — map from variable name to alloca register
- `slid_info_` — field name → index and type for each slid type
- `func_return_types_` / `func_param_types_` — function signatures
- `dtor_vars_` — stack of `DtorVar { var_name, slid_type, tuple_index }`. `tuple_index >= 0` means the dtor target is a GEP into the named tuple variable at that field index (used for slid-typed tuple elements). Plain locals have `tuple_index == -1`.
- `captures_` — variables captured by nested functions

The declarator codegen funnel — every site that MATERIALIZES A VALUE INTO STORAGE constructs / assigns through this one set of helpers, so field-init, ctor hooks, and dtor registration can never drift apart per site (the destructor-balance invariant, enforced structurally). This is NOT limited to NAMED declarators: named bindings (var-decl, sret return slot, global, the assignment forms) AND nameless construct/temp sites route through it. `new` is routed at every shape (delete owns the dtor, so `register_dtor=false, scope=nullptr`): a single object `new T(init)` calls `emitConstructAt`; an array with a size-matched initializer `new T[k](...)` (literal `k`) is typed `T[k]` in classify and built with ONE whole-array `emitConstructAt` (the array↔tuple bridge distributes it — same path as the stack `T arr[k](...)`, no new init semantics); a no-initializer array keeps the evaluate-once default broadcast, finalized per element via `emitConstructed`. Still hand-rolled (fold-in pending): the call-arg rvalue materialization in `emitCall`.
- `emitInitFill(addr, type, llty, init, is_move, ...)` — FILL storage from an initializer: the array↔tuple bridge (`emitArrayFromTuple`), a per-leaf aggregate widen (`emitImplicitAggregateConvert`), a move from an lvalue (load + convert + store, then null the source's pointer leaves), or a whole-value copy. Emits only stores — no hooks, no registration. Shared by construction and the live-storage assign sites (`kAssignStmt` / `kStoreStmt` / `kMoveStmt`). It does NOT dispatch per-field `op=` / `op<--`: classify already rewrote a user operator to a method call, so codegen fills whole-value.
- `emitConstructed(addr, type, register_dtor, scope)` — FINALIZE raw storage as a constructed object: run the recursive ctor hooks (`emitConstructHooks`) and register the destructor in `scope`, unless the caller owns destruction (`register_dtor`=false: an sret return slot / NRVO local / a global whose dtor runs through the global registry). The SINGLE place hooks and registration are paired — one `typeNeedsHook` flag governs both, so they can't diverge into a leak or an orphan dtor.
- `emitConstructAt(addr, type, llty, init, is_move, sret_in_place, register_dtor, scope, ...)` — CONSTRUCT into raw storage: an exact-typed sret CALL init builds in place (the callee's ctor runs at `addr`, only the dtor is registered) when `sret_in_place`; otherwise `emitInitFill` then `emitConstructed`. `init`==nullptr default-constructs (no field-init, ctor still runs).

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
