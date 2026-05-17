/*
miscellaneous tests.
*/

/*
issue 1 — a namespace function body with a string literal: exercises the
string-constant scan reaching members parked in NamespaceDef until emit()
pulls them into program.functions.
*/
Space {
    void greet() {
        __println("hello from a namespace");
    }
}

/*
issue 2 — current_namespace_ must not leak from a namespace-function emit
into a later ctor/dtor body emit. Thing's ctor calls greet() unqualified;
it must resolve to the global greet, not Space:greet.
*/
void greet() {
    __println("global greet");
}

Thing() {
    _() {
        greet();
    }
    ~() {
    }
}

/*
issue 3 — two namespace functions with the same signature are not caught as
a redefinition; both reach free_func_overloads_ and emit, producing a
duplicate symbol at link time. The duplicate should be a slidsc error.
Body disabled so the file still compiles; the negative runner strips the
`//` to drive the case.
*/
//-EXPECT-ERROR: Function 'Dup:f' is redefined with the same signature.
// Dup {
//     void f() { }
//     void f() { }
// }

/*
the same collision across the two definition syntaxes: a function bodied
inside the `Ns { }` block and an external `void Ns:f()` definition.
*/
//-EXPECT-ERROR: Function 'Dup2:f' is redefined with the same signature.
// Dup2 {
//     void f() { }
// }
// void Dup2:f() { }

/*
issue 4 — the imported-function declare loop guards with has_body.count(fn.name),
a bare name. When this TU defines a local function and also imports a namespace
function of the same bare name, the local body suppresses the import's `declare`,
so a call to the imported Ns:fn references an undeclared symbol.

Not single-file reproducible — it needs a second TU. Shape:
  this .sl:      void greet() { ... }       local body → has_body holds "greet"
  imported .slh: Other { void greet(); }    declare expected for Other:greet
  a call to Other:greet() finds no declare → link error against the local greet.
Keying has_body by the `ns:name` form fixes it.
*/

/*
issue 5 — `ret Ns:fn(...) { body }` routes into the namespace only when Ns
already appears in program.namespaces, a source-order scan. An external
namespace-function definition placed before its `Ns { }` block misroutes to
external_methods as a method of a class Ns that does not exist. It should
work regardless of order, the way a class method's external definition does.
Enable the block below to reproduce — the forward def precedes its namespace.

void Forward:hello() {
    __println("hello from Forward");
}

Forward {
    void hello();
}
*/

int32 main() {
    Space:greet();
    Thing t;
    return 0;
}
