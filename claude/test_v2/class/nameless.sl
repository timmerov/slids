/*
test nameless class instantiation.

the author can instantiate a class object without a name.
there are two forms.

form 1: statement or local variable form:

    Class(1,2,3);

like a named class, the unnamed variable object is initialized at site
and the ctor is called.
the dtor is called at end of scope.

the class of an unnamed object used in a statement must have ctor/dtor
defined - either explicitly or synthesized.
otherwise it's a compile error.

form 2: expression or temporary variable form:

    x = Class(1,2,3).x_;
    y = Class(1,2,3).method();
    Class(1,2,3).method();
    Class fn() {
        return Class(1,2,3);
    }

like a temporary object, the unnamed temporary object is initialized at site
and the ctor is called.
the dtor is called at end of the statement containing the expression.
in the first cases, at the semicolon.

note:
class assignment and overloaded operators aren't implmented yet.
so we can't test much of the above.
*/

/*
claude says:

a `Class(args)` call whose callee resolves to a CLASS (not a function) is a
CONSTRUCTION, not a call: resolve flags it (is_construction); classify spreads the
args into the per-field construction tuple via classifyClassInit (defaults / zeros,
recursion into class-typed fields, arity-checked) and types it as the class. it
then reuses the existing class-construction machinery — there is no new codegen.

- FORM 1 (a bare `Class(args);` statement): desugar rewrites it to a synthetic
  unnamed `_$nameless` var-decl in the current scope. the kVarDeclStmt path
  field-inits, runs the ctor, and registers the object for the enclosing scope's
  reverse-order dtor. a class with NO ctor/dtor (a trivial class) used this way is
  a no-op, which is a compile error.
- FORM 2 (a construction used inline — a method receiver, a field read, a call
  arg): desugar lifts it into a `_$cret` temp. for a kCallStmt / kExprStmt the
  temp's decl is block-wrapped with the statement, so its dtor runs at the
  SEMICOLON; as a call argument it keeps enclosing-scope lifetime.
- a construction as the rhs of a DECLARATION (`Class x = Class(...)`, incl. the
  `<--` move-init form) or a RETURN (`return Class(x)`) builds in place (RVO) —
  one ctor, one dtor, no temp.
- a construction in any OTHER position is rejected cleanly (no silent miscompile):
  an if / while condition, a store / move / swap operand, a re-assignment to an
  existing variable (`w = Class(...)`), and a method-call VALUE
  (`x = Class(...).method()`, which is unimplemented — only the statement form
  `Class(...).method();` and a field READ `x = Class(...).field` work).

qualified construction (`Space:Nested(args)`) and a class-typed field (the inner
ctor runs first, torn down last) both work.
*/

Class(int c_) {
    _() {
        __println("Class:ctor: " + c_);
    }
    ~() {
        __println("Class:dtor: " + c_);
    }

    void print() {
        __println("Class:print: " + c_);
    }
    int get() {
        return c_;
    }
}

// a multi-field class — exercises a nameless construction with several args.
Pair(int a_, int b_) {
    _() {
        __println("Pair:ctor: " + a_ + "," + b_);
    }
    ~() {
        __println("Pair:dtor: " + a_ + "," + b_);
    }
}

// a class with DEFAULT field values — partial-arg construction fills the rest.
Def(int a_ = 10, int b_ = 20) {
    _() {
        __println("Def:ctor: " + a_ + "," + b_);
    }
    ~() {
        __println("Def:dtor: " + a_ + "," + b_);
    }
}

// a class with a CLASS-typed field — construction recurses into the inner ctor,
// and teardown is the wrapper first, then the contained object.
Wrap(Class in_) {
    _() {
        __println("Wrap:ctor");
    }
    ~() {
        __println("Wrap:dtor");
    }
}

// a class with a NESTED class — qualified nameless construction `Host:Inner(n)`
// builds only the inner object.
Host(int h_) {
    _() {
        __println("Host:ctor: " + h_);
    }
    ~() {
        __println("Host:dtor: " + h_);
    }
    Inner(int i_) {
        _() {
            __println("Inner:ctor: " + i_);
        }
        ~() {
            __println("Inner:dtor: " + i_);
        }
    }
}

// returns a constructed temporary by value (return-form construction).
Class fn(int x) {
    return Class(x);
}

// receives a constructed temporary passed by reference.
int sink(Class^ p) {
    __println("sink: " + p^.c_);
    return 0;
}

// receives two constructed temporaries — exercises destruction order at the ';'.
int sink2(Class^ p, Class^ q) {
    __println("sink2: " + p^.c_ + "," + q^.c_);
    return 0;
}

int32 main() {

    // FORM 1 — statement form: scope lifetime, dtor in reverse declaration order.
    __println("== 1: form-1, two objects, reverse dtor at scope end ==");
    {
        Class(1);
        Class(2);
        __println("-- end 1 (dtor 2,1 next) --");
    }

    // FORM 1 — a multi-field class constructed by a nameless statement.
    __println("== 2: form-1 multi-arg ==");
    {
        Pair(3, 4);
        __println("-- end 2 (dtor 3,4 next) --");
    }

    // FORM 1 — no constructor args: the field takes its default/zero.
    __println("== 3: form-1 no args (default field 0) ==");
    {
        Class();
        __println("-- end 3 (dtor 0 next) --");
    }

    // a nameless statement-form object and a NAMED local share a scope: both die
    // at scope end in reverse declaration order.
    __println("== 4: nameless beside a named local ==");
    {
        Class a(5);
        Class(6);
        __println("-- end 4 (dtor 6,5 next) --");
    }

    // RETURN-form construction: fn returns Class(x), built directly into the
    // caller's named local (one ctor, one dtor at scope end).
    __println("== 5: return-construction into a named local ==");
    {
        Class cls = fn(7);
        __println("-- end 5 (dtor 7 next) --");
    }

    // DIRECT construction as a named-local initializer: built in place (one ctor),
    // dtor at scope end.
    __println("== 6: direct construction into a named local ==");
    {
        Class y = Class(8);
        __println("-- end 6 (dtor 8 next) --");
    }

    // construction-init with no args.
    __println("== 7: construction-init, no args ==");
    {
        Class z = Class();
        __println("-- end 7 (dtor 0 next) --");
    }

    // FORM 2 — method call on a direct construction temporary: dtor at the
    // semicolon (before the next statement).
    __println("== 8: form-2 method call on a direct temp ==");
    {
        Class(9).print();
        __println("-- end 8 (dtor 9 already ran) --");
    }

    // FORM 2 — method call on a RETURNED temporary: dtor at the semicolon.
    __println("== 9: form-2 method call on a returned temp ==");
    {
        fn(10).print();
        __println("-- end 9 (dtor 10 already ran) --");
    }

    // FORM 2 — read a field off a construction temporary in an initializer. The
    // value is read; the temp is destroyed when the enclosing scope ends.
    __println("== 10: form-2 field read in an initializer ==");
    {
        int v = Class(11).c_;
        __println("v= " + v);
        __println("-- end 10 (dtor 11 next) --");
    }

    // FORM 2 — a construction passed as a function argument: built, passed by
    // reference, destroyed at the semicolon.
    __println("== 11: construction as a function argument ==");
    {
        sink(Class(12));
        __println("-- end 11 (dtor 12 already ran) --");
    }

    // form 1 and form 2 in one scope: the form-2 temporary (14) dies at its
    // semicolon, BEFORE the form-1 object (13) dies at scope end.
    __println("== 12: form-1 and form-2 interacting ==");
    {
        Class(13);
        Class(14).print();
        __println("-- mid 12 (dtor 14 ran; dtor 13 at scope end) --");
    }

    // FORM 1 in a loop: each iteration constructs and destroys its own object at
    // the end of the loop body — no stack growth, balanced ctor/dtor per pass.
    __println("== 13: form-1 in a loop ==");
    {
        int i = 0;
        while (i < 3) {
            Class(i);
            i = i + 1;
        }
        __println("-- end 13 --");
    }

    // multi-arg DIRECT construction into a named local.
    __println("== 14: multi-arg construction-init ==");
    {
        Pair p = Pair(30, 40);
        __println("p.a_= " + p.a_);
    }

    // move-init from a construction temporary — one ctor, one dtor (no temp).
    __println("== 15: move-init from a construction ==");
    {
        Class y <-- Class(50);
        __println("y= " + y.c_);
    }

    // move-init from a returned temporary.
    __println("== 16: move-init from a returned temp ==");
    {
        Class cls <-- fn(51);
        __println("cls= " + cls.c_);
    }

    // two FORM-2 temporaries in one statement: reverse destruction order at ';'.
    __println("== 17: two temporaries in one call ==");
    {
        sink2(Class(60), Class(61));
        __println("-- end 17 (dtor 61,60 already ran) --");
    }

    // a construction whose ARGUMENT is itself a construction's field.
    __println("== 18: nested construction argument ==");
    {
        Class(Class(70).c_).print();
        __println("-- end 18 --");
    }

    // partial-arg and all-default construction of a class with field defaults.
    __println("== 19: default-field construction ==");
    {
        Def(5);
        Def();
        __println("-- end 19 (dtor 10,20 then 5,20) --");
    }

    // a class with a CLASS-typed field: the inner ctor runs first, the wrapper is
    // torn down before the contained object.
    __println("== 20: class-typed field ==");
    {
        Wrap(80);
        __println("-- end 20 --");
    }

    // qualified nameless construction of a nested class (only the inner object).
    __println("== 21: qualified nested-class construction ==");
    {
        Host:Inner(90);
        __println("-- end 21 --");
    }

    // a field read used inside a larger expression.
    __println("== 22: field read in a larger expression ==");
    {
        int v = Class(100).c_ + 1;
        __println("v= " + v);
        __println("-- end 22 --");
    }

    return 0;
}

/* ------------------------------------------------------------------------- *
 * negatives — each uncommented in isolation by the negative runner.
 * ------------------------------------------------------------------------- */

/* a construction with too many arguments — the class field arity check. */
//-EXPECT-ERROR: has 1 field(s) but 2 initializer(s)
//void neg_arity() {
//    Class(1, 2);
//}

/* a construction with an argument of the wrong type. */
//-EXPECT-ERROR: Cannot implicitly cast 'char[]' to 'int'
//void neg_wrong_arg_type() {
//    Class("hi");
//}

/* a nameless STATEMENT-form object whose class has no constructor or destructor
   does nothing — a compile error (form 1 requires ctor/dtor). */
//-EXPECT-ERROR: A nameless class statement has no effect
//Trivial(int t_) {
//}
//void neg_trivial() {
//    Trivial(5);
//}

/* a method call on a value (a construction temporary) mid-expression is not yet
   supported — reject it clearly, do not mis-parse. */
//-EXPECT-ERROR: A method call is not supported in an expression here
//void neg_method_on_value() {
//    int x = Class(2).get();
//    __println("x= " + x);
//}

/* a construction in an if condition is an unsupported position — reject it
   clearly rather than crashing in codegen. */
//-EXPECT-ERROR: Constructing a class in this position is not yet supported
//void neg_construction_in_if() {
//    if (Class(3).c_ > 0) {
//        __println("never");
//    }
//}

/* a construction in a while condition — same unsupported position. */
//-EXPECT-ERROR: Constructing a class in this position is not yet supported
//void neg_construction_in_while() {
//    while (Class(1).c_ > 99) {
//        __println("never");
//    }
//}

/* a construction through a dereference store target. */
//-EXPECT-ERROR: Constructing a class in this position is not yet supported
//void neg_deref_store() {
//    Class w(1);
//    Class^ r = ^w;
//    r^ = Class(2);
//}

/* a construction stored into an array element. */
//-EXPECT-ERROR: Constructing a class in this position is not yet supported
//void neg_element_store() {
//    Class arr[2];
//    arr[0] = Class(2);
//}

/* a construction as the source of a move onto an existing variable. */
//-EXPECT-ERROR: Constructing a class in this position is not yet supported
//void neg_move_source() {
//    Class w(1);
//    w <-- Class(2);
//}

/* a field that the class does not declare. */
//-EXPECT-ERROR: has no field 'nope'
//void neg_unknown_field() {
//    int x = Class(1).nope;
//    __println("x= " + x);
//}

/* constructing a name that is not a class. */
//-EXPECT-ERROR: Unknown function 'Bogus'
//void neg_unknown_class() {
//    Bogus(1);
//}

/* re-assigning a construction to an ALREADY-declared class variable is not
   supported — it would store the fields without running the constructor (and
   without destroying the old value). Declare a new variable instead. (A fresh
   `Class w = Class(7)` declaration is fine — block 6.) */
//-EXPECT-ERROR: Constructing a class in this position is not yet supported
//void neg_reassign_construction() {
//    Class w(5);
//    w = Class(7);
//}
