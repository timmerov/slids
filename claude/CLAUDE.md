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

**File model:** No forward declarations are needed within a `.sl` file — resolve does a names pass, then a bodies pass. `import <module>;` is a LEXER-level textual include of `<module>.slh` (lex.cpp). The full cross-TU model (`.slh` as a public contract, `.sl` as a private implementation, declare-only headers, symbol visibility) is **plan.txt Phase 8, NOT landed** — the orphan check still rejects any declared-but-never-defined function regardless of file.

**Templates: NOT IMPLEMENTED.** plan.txt Phase 9, unstarted. There is no template machinery in this compiler — no `template_funcs_`, no `.sli` files, no `--instantiate`. (v1 HAD a full cross-TU template system; that design is preserved in `v1/v1.md` → "Templates", with the source in `v1/compiler/codegen_template.cpp`. Do not assume any of it exists here.)

**Enums, operator overloading, nested functions with capture, and labeled break/continue** are all supported. See `slids_reference.md` for full syntax.

## Codegen internals

Codegen is NODE-DRIVEN and string-free: every ident / lvalue carries a `resolved_entry_id` stamped by classify, and its structured type rides on `widen::TypeRef`. State is passed down, not held on a class:
- `SymTab` = `std::map<int, VarInfo>` keyed by `parse::Tree::entries` index (NOT by name). `VarInfo` = `{ alloca_name, llvm_type, slids_type, touch_symbol }`; `alloca_name` is either a local `%x.<id>` or a global `@__global_x`, both `ptr`, so load/store/GEP treat them identically. `touch_symbol` is a lazy global's first-touch thunk.
- `DtorScope` — a chain of `{ objs, outer }`; a hook-bearing local registers here at construction and the scope emits its dtors in reverse at exit (a `return` unwinds the whole chain; `break`/`continue` unwind to the target loop's boundary). This is the destructor-balance invariant.
- Nested functions are lifted to top-level LLVM by `collectNestedFunctions`; captures are passed as extra by-reference params (the host alloca's address).

The declarator codegen funnel — every site that MATERIALIZES A VALUE INTO STORAGE constructs / assigns through this one set of helpers, so field-init, ctor hooks, and dtor registration can never drift apart per site (the destructor-balance invariant, enforced structurally). This is NOT limited to NAMED declarators: named bindings (var-decl, sret return slot, global, the assignment forms) AND nameless construct/temp sites route through it. `new` is routed at every shape (delete owns the dtor, so `register_dtor=false, scope=nullptr`): a single object `new T(init)` calls `emitConstructAt`; an array with a size-matched initializer `new T[k](...)` (literal `k`) is typed `T[k]` in classify and built with ONE whole-array `emitConstructAt` (the array↔tuple bridge distributes it — same path as the stack `T arr[k](...)`, no new init semantics); a no-initializer array keeps the evaluate-once default broadcast, finalized per element via `emitConstructed`. The call-arg rvalue pass temp in `emitCall` also fills via `emitInitFill` (no dtor — a transient temp freed by the stacksave/stackrestore bracket), so every construct/fill site — named declarators and nameless temps alike — now routes through the funnel. Two feed paths reach the helpers: DIRECT codegen construct sites (var-decl / sret / global / `new` / call-arg temp) call them, and nameless class temps (a `(Type=src)` conversion `_$cret`, an inline `Class(args)`, an arity-1 unary's `_$optmp`) reach them VIA the desugar lift (`liftSretCallExprs` rewrites each into a named `kVarDeclStmt` that takes the var-decl route). A class-operator CHAIN's accumulator is NOT one of these — desugar's chain lowering (`expandOpChainStmt` / `lowerOpChain`) either makes the DESTINATION the accumulator (zero temps) or mints a statement-scoped `_$optmp` var-decl itself; see readme-classes.txt "CLASS-OPERATOR CHAINS".
- `emitInitFill(addr, type, llty, init, is_move, ...)` — FILL storage from an initializer: the array↔tuple bridge (`emitArrayFromTuple`), a per-leaf aggregate widen (`emitImplicitAggregateConvert`), a same-type transfer from an lvalue, or a whole-value store. No hooks, no registration. Shared by construction and the live-storage assign sites (`kAssignStmt` / `kStoreStmt` / `kMoveStmt`). A same-type transfer of a CLASS-BEARING value from an lvalue dispatches PER LEAF through the class's `@__$copy` / `@__$move` (see the transfer invariant below) — only pure POD is a whole-value load/store.
- `emitConstructed(addr, type, register_dtor, scope)` — FINALIZE raw storage as a constructed object: run the recursive ctor hooks (`emitConstructHooks`) and register the destructor in `scope`, unless the caller owns destruction (`register_dtor`=false: an sret return slot / NRVO local / a global whose dtor runs through the global registry). The SINGLE place hooks and registration are paired — one `typeNeedsHook` flag governs both, so they can't diverge into a leak or an orphan dtor.
- `emitConstructAt(addr, type, llty, init, is_move, sret_in_place, register_dtor, scope, ...)` — CONSTRUCT into raw storage: an exact-typed sret CALL init builds in place (the callee's ctor runs at `addr`, only the dtor is registered) when `sret_in_place`; otherwise `emitInitFill` then `emitConstructed`. `init`==nullptr default-constructs (no field-init, ctor still runs).

**The transfer invariant:** a whole-class copy / move / swap ALWAYS calls the class's `@<Class>__$copy` / `__$move` / `__$swap` — NEVER a blit. Every class has one (the user's `op=`/`op<--`/`op<-->(Self^)`, else resolve's synthesized memberwise default; `methodSymbol` renames both to the same symbol). The gate at all three transfer sites (`emitInitFill`'s move arm, its copy arm, `kSwapStmt`) is STRUCTURAL — `widen::hasInPlaceClass`, "is a class physically in this storage?" — not behavioral (`typeNeedsHook`, "does a ctor body exist?"), which a hook-less class answers NO to. Corollary: a transfer synthesized AFTER classify (in desugar/codegen) must NAME the operator, never emit a bare move/copy node. `typeNeedsHook` is still correct where it remains (`emitConstructed`, dtor registration, the sret hook decisions) — those really are asking "is there a ctor body to call?". The two predicates are NOT interchangeable.

Type mapping: `int` → `i32`, `int64` → `i64`, `bool` → `i1`, `float32` → `float`, `float64` → `double`, pointer types → `ptr`, slid type name → `%struct.<Name>`, anonymous tuple `(t1,t2,...)` → literal struct `{ llvm(t1), llvm(t2), ... }`.
