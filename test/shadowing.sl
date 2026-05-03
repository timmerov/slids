/*
Shadowing & identifier-uniqueness rule — example catalogue.

shadowing is a difficult problem to describe rigorously.
in a very real sense, it's a form of code obfuscation.
not necessarily intentional, but the unintentional cases are
often very hard to find.
some cases are easy:
shadowing causes parse/compile ambiguities: not allowed.
pretty much everything else is subjective.
what's allowed and what isn't allowed is left pretty much
to the experience of the human author (timmer).

this file is a catalog of shadowing cases binned into allowed
and not allowed.

reopening a class merges symbols as if it was a single unit.
same for everything that can be reopened: namespaces, templates, etc.

the rules for imported headers are the same as if the header was
inline with the source.
testing import is out of scope of this file.

Each example uses its own unique identifiers (Cls01, Cls02, ...) so
the cases do not collide with each other. The examples should compile
today and fail once the corresponding shadow ban is implemented.
*/


/*==========================================================
(P1) — vexing parses, mechanical bans
==========================================================*/

/* NOT ALLOWED: type and variable share an identifier in one scope. */
Cls01(int x_ = 0) { }
void test_p1_01() {
    // variable name equals type name in same declaration — vexing parse
    // Cls01 Cls01;
}

/*
NOT ALLOWED: method shares its enclosing class's name (a1).
Class scope merges name + body; the class name is bound twice.
*/
Cls02(int x_ = 0) { }
Cls02 {
    // method name equals enclosing class name — class scope merges name + body
    // int Cls02() { return 0; }
}

/* NOT ALLOWED: slid-name and tuple-param share scope (header+body merge). */
// tuple-param name equals enclosing class name
// Cls03(int Cls03 = 0) { }

/* NOT ALLOWED: function-name and parameter share scope. */
// parameter name equals enclosing function name
// void f04(int f04) { }

/* NOT ALLOWED: tuple has two fields with the same name. */
// duplicate tuple-param names within one class
// Cls05(int x_ = 0, int x_ = 0) { }

/*
A bare enum injects its values into the enclosing scope as siblings.
Same-scope sibling collisions are (P1) violations; inner scopes may
shadow per the standard rule.
*/
enum Dir06 (
    kNorth06,
    kSouth06
)
void test_p1_06() {
    // allowed. different scope.
    int kNorth06;
}
// not allowed, same scope. function name equals bare-enum value injected at file scope
// void kNorth06() {
// }

/*
NOT ALLOWED: reopens compose into one class scope; same method twice.
Current compiler emits duplicate LLVM symbol — llc fails. Once the
parser-level (P1) ban lands the duplicate becomes a slidsc error.
*/
Cls07(int x_ = 0) { }
Cls07 {
    void bar07() { }
}
Cls07 {
    // duplicate method across reopens — same name and signature
    // void bar07() { }
}

/*
NOT ALLOWED: built-in type keyword cannot be rebound. Caught at the
lexer (keyword reservation), not the shadow check, but listed for
completeness — `int int;` is the canonical vexing parse for built-ins.
*/
void test_p1_15() {
    // built-in type keyword cannot be used as an identifier
    // int int;
}

/*
NOT ALLOWED: two enums in one scope share a value name. Both enums
inject their values as siblings; kFoo16 collides.
*/
enum Color16 (
    kFoo16,
    kRed16
)
// enum value collides with sibling enum's value at file scope
// enum Shape16 (
//     kFoo16,
//     kSquare16
// )

/*
NOT ALLOWED: typed enum follows the same rules as bare enum. Default
int type or explicit type — values are injected the same way.
TODO: blocked on typed-enum parser support; re-enable once available.
*/
/*
enum Status17 : int32 (
    kOk17 = 0,
    kErr17 = 1
)
void kOk17() { }
*/

/*
NOT ALLOWED: two functions with same name and signature in one scope.
*/
void f18() { }
// duplicate function with same signature at file scope
// void f18() { }

/*
NOT ALLOWED: two nested functions with same name and signature in
the same enclosing scope.
*/
void test_p1_19() {
    void inner19() { }
    // duplicate nested function with same signature in same block
    // void inner19() { }
}

/*
NOT ALLOWED: two op overloads with identical signatures.
*/
Cls20(int v_ = 0) {
    op=(int x) { v_ = x; }
    // duplicate op overload with identical signature
    // op=(int x) { v_ = x; }
}

/*
NOT ALLOWED: constructor (or destructor) declared twice.
*/
Cls21(int v_ = 0) {
    _() { }
    // duplicate constructor
    // _() { }
    ~() { }
}

/*
NOT ALLOWED: namespace fn shares the namespace name. Namespace scope
merges name + body.
*/
Space22 {
    // namespace fn name equals enclosing namespace name
    // void Space22() { }
}

/*
NOT ALLOWED: same fn defined twice across namespace reopens.
*/
Space23 {
    void greet23() { }
}
Space23 {
    // duplicate fn across namespace reopens
    // void greet23() { }
}

/*
NOT ALLOWED: `self` is a reserved keyword and cannot be used as a
parameter, local, or field.
*/
// 'self' is reserved and cannot be used as a parameter
// void test_p1_24a(int self) { }
void test_p1_24b() {
    // 'self' is reserved and cannot be used as a local variable
    // int self = 5;
}
// 'self' is reserved and cannot be used as a field
// Cls24(int self = 0) { }


/*==========================================================
Shadowing across nested scopes — ALLOWED
==========================================================*/

/* ALLOWED: outer and inner block bind `x` to different meanings. */
void example_block_shadow() {
    int x = 0;
    {
        float32 x = 1.0;
    }
}

/* ALLOWED (weird, but legal): block-local variable shadows a class. */
Cls08(int len_ = 0) { }
void example_class_shadow() {
    int Cls08;
}

/* ALLOWED: parameter name differs from type name, no collision. */
void render(Cls08^ s) { }

/*
ALLOWED: for-loop variable shadows an outer local of the same name.
Currently a codegen bug — emits duplicate `%var_i` allocas. Once
that's fixed this becomes live.
*/
void example_loop_shadows_outer() {
    int i = 0; // allowed
    // the scope for the for loop begins at the for
    // the for 'i' is in a different scope.
    // this currently is a compile error.
    // but should be allowed.
    for (int i : 0..10) { // allowed
        // this 'i' is in the same scope as the for 'i' — same-scope dup
        // int i = 42; // not allowed
        {
            // this 'i' is in its own scope.
            // shadowing the for 'i' is legal.
            // sadowing the first 'i' is legal.
            int i = 37; // allowed
        }
    }
    // for scope ends at the brace
}

/*
NOT ALLOWED: for-loop variable shadowed by a body declaration.
for-header+body is a single scope, distinct from the enclosing
function scope. The body decl `int i = 5` collides with the loop
var `int i` in that one merged scope. Same codegen bug as
example_loop_shadows_outer above — duplicate `%var_i` alloca.
*/
void example_for_body_shadow() {
    for (int i : 0..10) {
        // for-body local duplicates the for-loop variable in same merged scope
        // int i = 5;
    }
}

/*
ALLOWED: nested for's with the same index.
*/
void example_nested_for_same_index() {
    for (int i : 0..10) {
        for (int i : 0..10) {
            // it's impossible to reach the outer 'i'
            // but that's okay.
        }
    }
}

/*
ALLOWED: nested loops reuse a block label. Each loop has its own
label scope; innermost match wins. The outer label becomes
unreachable from inside the inner loop, which mirrors how an outer
local becomes unreachable when shadowed by an inner local.
*/
void example_block_label_shadow() {
    for (int i : 0..3) {
        for (int j : 0..3) {
            break outer; // breaks inner per innermost-match
        } :outer;
    } :outer;
}

/*
ALLOWED: sibling loops share a block label. Each loop's label lives
with that loop; from inside loop A, `break outer` reaches A's outer;
from inside loop B, B's outer. Unambiguous.
*/
void example_block_label_siblings() {
    for (int i : 0..3) {
    } :outer;
    for (int j : 0..3) {
    } :outer;
}


/*==========================================================
(P2) — foot-gun bans inside methods (fields only)
==========================================================*/

/*
Class fields are visible inside every method on the class. No inner
binding in a method (param, local, loop var, block-decl, nested slid)
may shadow a field.
*/
Cls09(int x_ = 0) {
    // method parameter collides with field name (P2)
    // void via_param(int x_) { }

    void via_local() {
        // method local collides with field name (P2)
        // int x_ = 5;
    }

    void via_loop() {
        // for-loop index collides with field name (P2)
        // for (int x_ : 0..10) {
        // }
    }

    void via_block() {
        {
            // block-local collides with field name (P2)
            // int x_ = 1;
        }
    }

    void via_nested() {
        // nested function name collides with field name (P2)
        // void x_() { }
    }

    void via_method_local_vs_param(int p) {
        // method local collides with method parameter (same scope)
        // int p = 5;
    }
}

/*
NOT ALLOWED: function local collides with function parameter.
Function header+body is one scope.
*/
void f25(int p) {
    // function local duplicates function parameter (same scope)
    // int p = 5;
}


/*==========================================================
Inheritance — derived class extends base class scope
==========================================================*/

/*
NOT ALLOWED: derived class field has same name as a base class field.
Per spec, field-name collision is a compile error.
*/
Cls26B(int x_ = 0) { }
// derived field collides with base field
// Cls26B : Cls26D(int x_ = 0) { }

/*
ALLOWED: derived method shadows a base method. Per spec, this is the
intended way to override behavior on the derived class.
*/
Cls27B(int n_ = 0) {
    void speak() { }
}
Cls27B : Cls27D(int t_ = 0) {
    void speak() { }
}

/*
NOT ALLOWED: derived field has same name as a base method. The
inherited method `bar` is reached as obj.bar(); the derived field
`bar` is reached as obj.bar. Vexing.
*/
Cls28B(int n_ = 0) {
    void bar() { }
}
// derived field name collides with base method name
// Cls28B : Cls28D(int bar = 0) { }

/*
NOT ALLOWED: derived method has same name as a base field. The
inherited field `f` is reached as obj.f; the derived method `f()`
is reached as obj.f(). Vexing.
*/
Cls29B(int f = 0) { }
Cls29B : Cls29D(int n_ = 0) {
    // derived method name collides with base field name
    // void f() { }
}


/*==========================================================
ALLOWED duplicates — same name, syntactically unambiguous
==========================================================*/

/*
Also applies to overloaded functions and methods: same name,
different signatures, dispatch is unambiguous.
*/

/*
ALLOWED: operator overloads on different argument types.
Multiple `op=` discriminate by signature.
*/
Val11(int value_ = 0) {
    _() { }
    ~() { }
    op=(Val11^ rhs) { value_ = rhs^.value_; }
    op=(int x)      { value_ = x; }
}

/*
ALLOWED ((P2) symmetry — methods, unlike fields, may be shadowed):
a method on the class is reached as `self.bar12()`; a local `bar12` is
reached as bare `bar12`. The trailing `()` disambiguates.
*/
Cls12(int v_ = 0) {
    void bar12() { }
    void baz12() {
        // allowed, method bar12() is unreachable.
        int bar12 = 5;
    }
}

/*
ALLOWED: the slid name, the class type, and the constructor are
one binding with multiple roles, not a duplicate.
*/
Cls13(int len_ = 0) { }

void example_one_binding() {
    Cls13 s;
    Cls13 t(5);
}

/*
ALLOWED: a hoisted nested class is an independent type. Foo:x_ and
Foo:Bar:x_ are different bindings reached through different instances,
so the inner field does not shadow the outer.
*/
Cls14(int x_ = 0) {
    Inner14(int x_ = 0) {
        Deep14(int x_ = 0) { }
    }
}

/*
ALLOWED: overloaded functions. Same name, different signatures
disambiguate at the call site.
*/
void g31(int x) { }
void g31(char[] s) { }

/*
ALLOWED: overloaded non-op methods. Same name, different signatures.
*/
Cls32(int v_ = 0) {
    void m() { }
    void m(int x) { }
}

/*
ALLOWED: forward declaration followed by definition with the same
signature. The decl + def pair is one binding, not a duplicate.
*/
int f44();
int f44() { return 0; }

/*
ALLOWED: template forward declaration followed by template definition
with the same signature. Same rule as #44, applied to templates.
*/
T t45<T>(T a);
T t45<T>(T a) { return a; }

int32 main() {
    int discard45 = t45(0); // instantiate so the template is actually emitted
    __println("01: Not allowed: variable name may not equal type name in same declaration.");
    __println("02: Not allowed: method name may not equal enclosing class name.");
    __println("03: Not allowed: tuple-param name may not equal enclosing class name.");
    __println("04: Not allowed: parameter name may not equal enclosing function name.");
    __println("05: Not allowed: tuple-param names may not be duplicated within one class.");
    __println("06: Allowed: inner-scope variable may shadow a bare-enum value.");
    __println("07: Not allowed: file-scope identifier may not equal a bare-enum value at file scope.");
    __println("08: Not allowed: methods may not duplicate across class reopens with the same signature.");
    __println("09: Not allowed: built-in type keyword may not be used as an identifier.");
    __println("10: Not allowed: two enums in one scope may not share a value name.");
    __println("11: Not allowed: file-scope function may not be redefined with the same signature.");
    __println("12: Not allowed: nested function may not be redefined with the same signature in the same block.");
    __println("13: Not allowed: op overload may not be repeated with the same signature.");
    __println("14: Not allowed: constructor or destructor may not be declared twice in one class.");
    __println("15: Not allowed: function in namespace may not equal enclosing namespace name.");
    __println("16: Not allowed: namespace fn may not be duplicated across reopens with same signature.");
    __println("17: Not allowed: 'self' is reserved and may not be used as a parameter, local, or field.");
    __println("18: Allowed: outer and inner block may bind the same name to different meanings.");
    __println("19: Allowed: block-local variable may shadow an outer class name.");
    __println("20: Allowed: parameter name may differ from its type name without collision.");
    __println("21: Allowed: for-loop variable may shadow an outer local of the same name.");
    __println("22: Not allowed: for-body local may not duplicate the for-loop variable.");
    __println("23: Allowed: inner block may shadow both the for-loop var and an outer local.");
    __println("24: Allowed: nested for-loops may reuse the same index name.");
    __println("25: Allowed: nested loops may reuse a block label; innermost match wins.");
    __println("26: Allowed: sibling loops may share a block label.");
    __println("27: Not allowed: method parameter may not shadow a field of the enclosing class.");
    __println("28: Not allowed: method local may not shadow a field of the enclosing class.");
    __println("29: Not allowed: for-loop index in a method may not shadow a field of the enclosing class.");
    __println("30: Not allowed: block-local in a method may not shadow a field of the enclosing class.");
    __println("31: Not allowed: nested function name in a method may not shadow a field of the enclosing class.");
    __println("32: Not allowed: method local may not duplicate a method parameter.");
    __println("33: Not allowed: function local may not duplicate a function parameter.");
    __println("34: Not allowed: derived field may not equal a base field name.");
    __println("35: Allowed: derived method may shadow a base method.");
    __println("36: Not allowed: derived field may not equal a base method name.");
    __println("37: Not allowed: derived method may not equal a base field name.");
    __println("38: Allowed: operator overloads on different argument types.");
    __println("39: Allowed: method local may shadow a method name on the same class.");
    __println("40: Allowed: slid name, class type, and constructor are one binding with multiple roles.");
    __println("41: Allowed: hoisted nested classes have independent field scopes.");
    __println("42: Allowed: overloaded functions with different signatures.");
    __println("43: Allowed: overloaded non-op methods with different signatures.");
    __println("44: Allowed: forward declaration followed by definition with the same signature.");
    __println("45: Allowed: template forward declaration followed by template definition.");
    return 0;
}
