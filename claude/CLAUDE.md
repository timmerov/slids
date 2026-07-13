# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

It describes HOW TO WORK HERE — not how the compiler works. Nothing in this file describes
code, so nothing in it goes stale when code changes. For anything technical, read the file
that owns it (see "Where the truth lives"). Do not copy compiler internals back into here.

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

```
.sl source → lex → grammar → resolve → constfold → classify → desugar → codegen → LLVM IR (.ll)
```

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

A positive test compares its output byte-for-byte against `exp.<name>` in the same directory;
a negative case is an `//-EXPECT-ERROR:` marker followed by a `//`-commented block that the
runner uncomments. Before regenerating any `exp.*` golden, diff the SORTED line multiset
against the old one: a pure reordering means a lifetime moved (often the point of the change),
but a changed count means an object was lost or double-destroyed — a bug, not a new golden.

## Where the truth lives

Read the owner before reasoning about a subsystem. These files are maintained with the code;
this one is not the place to duplicate them.

- `compiler/readme.txt` — the pipeline and each stage's internals (lex → codegen), the type arena, the symbol table, diagnostics.
- `compiler/readme-classes.txt` — classes, inheritance, virtuals, operators, the class-operator chains, construction/destruction and temp lifetimes.
- `compiler/plan.txt` — the main quest: what has landed, phase by phase, with the rationale. `plan-declarator.txt`, `plan-evaluate.txt` — the deep design records for those two landings.
- `compiler/todo.txt` — everything OPEN: bugs, deferred items, reach goals. Landed items are removed.
- `slids_reference.md` — the language: full syntax and semantics.
- `v1/v1.md` — v1's designs. **v1 is a different compiler.** Nothing described there exists in v2 unless v2's own docs say so.

**Three invariants are load-bearing.** Read readme-classes.txt before touching construction,
assignment, or transfer code — the *declarator funnel* (every site that materializes a value
into storage goes through one set of helpers), the *destructor-balance invariant* (every
instance destroyed exactly once, in reverse order, on every exit), and the *transfer
invariant* (a whole-class copy/move/swap always calls the class's operator, never a blit).
Breaking one produces a leak, a double-free, or a silently skipped user operator.

## Not implemented — do not assume otherwise

- **Templates.** plan.txt Phase 9, unstarted. There is no template machinery in this compiler. (v1 had a full cross-TU template system; that is v1's, and it is not here.)
- **The cross-TU model.** plan.txt Phase 8, not landed. `import <module>;` is a LEXER-level textual include of `<module>.slh` — nothing more. There are no declare-only headers: the orphan check rejects any declared-but-never-defined function regardless of file.
