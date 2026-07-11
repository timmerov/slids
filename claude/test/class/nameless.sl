/*
test nameless class instantiation.

the author can instantiate a class object without a name.
there are two forms.

form 1: statement or local variable form:

    Class;
    Class();
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
  SEMICOLON. a nested arg / receiver temp in a var-decl / assign / return rhs whose
  VALUE is a SCALAR is folded into a kSeqExpr wrapping the rhs, so its dtor also runs
  at the STATEMENT. (a rhs whose value is a CLASS built in place keeps enclosing-
  scope lifetime — the seq wrap is scalar-only.)
- a construction as the rhs of a DECLARATION (`Class x = Class(...)`, incl. the
  `<--` move-init form) or a RETURN (`return Class(x)`) builds in place (RVO) —
  one ctor, one dtor, no temp.
- a construction used as a METHOD-CALL VALUE (`x = Class(...).method()`) or a field
  READ (`x = Class(...).field`) yields a SCALAR: the temp is lifted and, via the rhs
  seq wrap, destroyed at the STATEMENT (built once). in a CONDITION (if/while/for/
  switch — including under `&&`/`||`) it is instead built and destroyed per
  EVALUATION, so a loop or short-circuit rebuilds or skips it (the short-circuit RHS
  lifts into its OWN sub-seq, so a skipped branch runs no ctor/dtor).
- a construction in any OTHER position is rejected cleanly (no silent miscompile):
  a store / move / swap operand, and a re-assignment to an existing variable
  (`w = Class(...)`).

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
    // value is read; the temp is destroyed at the end of the DECL statement (the
    // scalar rhs is seq-wrapped), before the next statement.
    __println("== 10: form-2 field read in an initializer ==");
    {
        int v = Class(11).c_;
        __println("v= " + v);
        __println("-- end 10 (dtor 11 already ran) --");
    }

    // FORM 2 — a METHOD CALL on a construction temporary, used as a VALUE: the temp
    // is lifted (built, the method called on it, its scalar result read), then
    // destroyed at the end of the DECL statement (the scalar rhs is seq-wrapped). A
    // loop/if CONDITION instead rebuilds it per evaluation — see 10c/10d.
    __println("== 10b: form-2 method call as a value ==");
    {
        int g = Class(15).get();
        __println("g= " + g);
        __println("-- end 10b (dtor 15 already ran) --");
    }

    // FORM 2 — a construction-temporary method call in an IF CONDITION: the
    // condition is evaluated once, so the temp is lifted before the if and
    // destroyed right after it.
    __println("== 10c: form-2 method call in an if condition ==");
    {
        if (Class(16).get() > 0) {
            __println("positive");
        }
        __println("-- end 10c (dtor 16 ran BEFORE the body) --");
    }

    // FORM 2 — a construction-temporary method call in a WHILE condition: re-built
    // AND destroyed EACH iteration (the condition seq re-evaluates per pass), so the
    // ctor/dtor are balanced per iteration.
    __println("== 10d: form-2 method call in a while condition ==");
    {
        int n = 0;
        while (Class(n).get() < 2) {
            __println("loop " + n);
            n += 1;
        }
        __println("-- end 10d --");
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

    // FORM 2 — a construction under `&&` whose LHS is FALSE: the short-circuit skips
    // the RHS, so the construction's ctor/dtor must NOT run (it is lifted into the
    // RHS's own conditional sub-seq, not the unconditional condition pre).
    __println("== 23: && short-circuit, rhs construction SKIPPED ==");
    {
        if (false && Class(23).get() > 0) {
            __println("unreachable 23");
        }
        __println("-- end 23 (no ctor/dtor 23) --");
    }

    // FORM 2 — a construction under `&&` whose LHS is TRUE: the RHS is evaluated, so
    // the temp is built and destroyed inside the condition (ctor/dtor before the body).
    __println("== 24: && rhs evaluated, ctor/dtor balanced ==");
    {
        if (true && Class(24).get() > 0) {
            __println("body 24");
        }
        __println("-- end 24 (ctor/dtor 24 ran before body) --");
    }

    // FORM 2 — a construction under `||` whose LHS is TRUE: the short-circuit skips
    // the RHS, so no ctor/dtor.
    __println("== 25: || short-circuit, rhs construction SKIPPED ==");
    {
        if (true || Class(25).get() > 0) {
            __println("body 25");
        }
        __println("-- end 25 (no ctor/dtor 25) --");
    }

    // FORM 2 — a construction under `||` whose LHS is FALSE: the RHS is evaluated.
    __println("== 26: || rhs evaluated ==");
    {
        if (false || Class(26).get() > 0) {
            __println("body 26");
        }
        __println("-- end 26 (ctor/dtor 26 ran before body) --");
    }

    // FORM 2 — a construction in the RHS of `&&` in a WHILE condition: rebuilt each
    // pass while the LHS holds, and SKIPPED on the exit test when the LHS is false
    // (no stray ctor 2).
    __println("== 27: && in a while condition, rebuilt per pass, skipped on exit ==");
    {
        int i = 0;
        while (i < 2 && Class(i).get() >= 0) {
            __println("loop 27: " + i);
            i = i + 1;
        }
        __println("-- end 27 (no ctor/dtor on the exit test) --");
    }

    // FORM 2 — a construction in the LHS of `&&`: the LHS runs UNCONDITIONALLY, so it
    // is lifted into the condition pre (ctor/dtor around the whole evaluation).
    __println("== 28: lhs construction always runs ==");
    {
        if (Class(28).get() > 0 && true) {
            __println("body 28");
        }
        __println("-- end 28 (ctor/dtor 28 ran before body) --");
    }

    // FORM 2 — a construction under a NESTED short-circuit: `(true && false)` is false,
    // so the outer `&&` skips its RHS construction.
    __println("== 29: nested short-circuit skips the construction ==");
    {
        if (true && false && Class(29).get() > 0) {
            __println("unreachable 29");
        }
        __println("-- end 29 (no ctor/dtor 29) --");
    }

    // NON-CONDITION position — a construction in the RHS of `&&` in a DECL initializer
    // whose LHS is FALSE: the RHS is still conditionally evaluated, so it is skipped.
    __println("== 30: && rhs construction in a decl initializer, SKIPPED (lhs false) ==");
    {
        bool r = false && Class(30).get() > 0;
        __println("r= " + r);
        __println("-- end 30 (no ctor/dtor 30) --");
    }

    // NON-CONDITION position — same, LHS TRUE: the RHS construction runs and is torn
    // down inside the initializer's `&&` sub-seq.
    __println("== 31: && rhs construction in a decl initializer, evaluated (lhs true) ==");
    {
        bool r = true && Class(31).get() > 0;
        __println("r= " + r);
        __println("-- end 31 (ctor/dtor 31 ran) --");
    }

    // NON-CONDITION position — a construction in the RHS of `||` in a DECL initializer
    // whose LHS is FALSE: the RHS runs.
    __println("== 32: || rhs construction in a decl initializer, evaluated (lhs false) ==");
    {
        bool r = false || Class(32).get() > 100;
        __println("r= " + r);
        __println("-- end 32 (ctor/dtor 32 ran) --");
    }

    // FORM 2 — a construction in the RHS of `&&` in a FOR-LONG condition: rebuilt each
    // pass while the LHS holds, skipped on the exit test.
    __println("== 33: && in a for-long condition, rebuilt per pass ==");
    {
        for (int i = 0) (i < 2 && Class(i).get() >= 0) { i = i + 1; } {
            __println("loop 33: " + i);
        }
        __println("-- end 33 (no ctor/dtor on the exit test) --");
    }

    {
        NoInitClass(int x_) {
            _() { __println("NoInitClass:ctor"); }
            ~() { __println("NoInitClass:dtor"); }
        }
        NoInitClass;
        NoInitClass();
        NoInitClass(1);
        tuple = (NoInitClass, 7);
        NoInitClass array[2] = (NoInitClass, NoInitClass);
        __println(tuple[0].x_ + " " + array[0].x_);
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

/* a construction temporary in a CONDITION — an if/while/for condition or a switch
   discriminant — IS supported (positives "10c"-"10d"): the temp is lifted into the
   condition's seq, constructed and destroyed per evaluation (so a loop rebuilds it
   each iteration). */

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
