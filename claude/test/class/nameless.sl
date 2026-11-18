/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
test nameless class instantiation.

the author can instantiate a class object without a name.
there are two forms.

form 1: statement or local variable form:

    Class;
    Class();
    Class(1,2,3);
    Class.x_ = 1;
    Class = cls; // default assignment.

like a named class, the unnamed variable object is initialized at site
and the ctor is called.
the dtor is called at end of scope.

the class of an unnamed object used in a statement must have ctor/dtor
defined - either explicitly or synthesized.
otherwise it's a compile error.

form 2: expression or temporary variable form:

    x = Class.x_;
    y = Class().method();
    Class(1).method();
    Class fn() {
        return Class(1,2);
    }
    Class(1,2,3) = rhs; // user defined assignment

like a temporary object, the unnamed temporary object is initialized at site
and the ctor is called.
the dtor is called at end of the statement containing the expression.
in the first cases, at the semicolon.

note:
in all cases, the temporary must at least be able to have a side effect.
in form 1, the side effect is in the ctor and/or dtor.
in form 2, the side effect is the assignment or the method.

the initializer list is optional in all cases.
if Class is accepted then so is Class(), Class(1,2,3), etc.
and vice versa.
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
- a construction as a STORE TARGET's root (classify's liftNamelessStoreTarget):
  a FIELD store (`Class.f = v`, `Class(1).f[0] = v`, `Class.f += v`) is form 1 — the
  construction becomes a `_$nameless` local in the statement's prelude (scope
  lifetime), the root a read of it. a WHOLE-object store (`Class = rhs`, which resolve
  rewrites from the assignment) follows the op= it selects: a USER op= is form 2
  (`Class.op=(rhs);`, a method call on a temp); the SYNTHESIZED op= is form 1. form 1
  with a trivial class is the "has no effect" error. a bare field READ statement
  (`Class(1).f;`) is "A nameless class expression has no effect."
- `name (group) = rhs;` is also the tuple conversion shorthand `t (T,...) = e`. the
  grammar reads the group both ways: values only -> the nameless assignment (a
  kStoreStmt rooted at `name(args)`); types only -> the shorthand. a group of NAMES
  parses both ways (an ambiguous_group kAssignStmt carrying both); resolve keeps the
  reading the names fit — all types, all values, or a mix (an error). a store rooted
  at a FUNCTION call is "Cannot assign to the result of a function call."

qualified construction (`Space:Nested(args)`) and a class-typed field (the inner
ctor runs first, torn down last) both work.
*/

import slids.string;
alias slids;

Class(int c_) {
    _() {
        println(String + "Class:ctor: " + c_);
    }
    ~() {
        println(String + "Class:dtor: " + c_);
    }

    void print() {
        println(String + "Class:print: " + c_);
    }
    int get() {
        return c_;
    }
}

// a multi-field class — exercises a nameless construction with several args.
Pair(int a_, int b_) {
    _() {
        println(String + "Pair:ctor: " + a_ + "," + b_);
    }
    ~() {
        println(String + "Pair:dtor: " + a_ + "," + b_);
    }
}

// a class with DEFAULT field values — partial-arg construction fills the rest.
Def(int a_ = 10, int b_ = 20) {
    _() {
        println(String + "Def:ctor: " + a_ + "," + b_);
    }
    ~() {
        println(String + "Def:dtor: " + a_ + "," + b_);
    }
}

// a class with a CLASS-typed field — construction recurses into the inner ctor,
// and teardown is the wrapper first, then the contained object.
Wrap(Class in_) {
    _() {
        println(String + "Wrap:ctor");
    }
    ~() {
        println(String + "Wrap:dtor");
    }
}

// a class with a NESTED class — qualified nameless construction `Host:Inner(n)`
// builds only the inner object.
Host(int h_) {
    _() {
        println(String + "Host:ctor: " + h_);
    }
    ~() {
        println(String + "Host:dtor: " + h_);
    }
    Inner(int i_) {
        _() {
            println(String + "Inner:ctor: " + i_);
        }
        ~() {
            println(String + "Inner:dtor: " + i_);
        }
    }
}

// a class with an ARRAY field — a nameless element store `Vec.v_[0] = 1;`.
Vec(int v_[2]) {
    _() {
        println(String + "Vec:ctor: " + v_[0] + "," + v_[1]);
    }
    ~() {
        println(String + "Vec:dtor: " + v_[0] + "," + v_[1]);
    }
}

// a class with a USER op= — `Asg = rhs;` is form 2: the op= is the side effect.
Asg(int a_) {
    _() {
        println(String + "Asg:ctor: " + a_);
    }
    ~() {
        println(String + "Asg:dtor: " + a_);
    }
    op=(int rhs) {
        a_ = rhs;
        println(String + "Asg:op=: " + a_);
    }
}

// a TRIVIAL class with a USER op= — form 2 needs no ctor/dtor.
TrivAsg(int t_) {
    op=(int rhs) {
        t_ = rhs;
        println(String + "TrivAsg:op=: " + t_);
    }
}

// returns a constructed temporary by value (return-form construction).
Class fn(int x) {
    return Class(x);
}

// receives a constructed temporary passed by reference.
int sink(Class^ p) {
    println(String + "sink: " + p^.c_);
    return 0;
}

// receives two constructed temporaries — exercises destruction order at the ';'.
int sink2(Class^ p, Class^ q) {
    println(String + "sink2: " + p^.c_ + "," + q^.c_);
    return 0;
}

int32 main() {

    // FORM 1 — statement form: scope lifetime, dtor in reverse declaration order.
    println(String + "== 1: form-1, two objects, reverse dtor at scope end ==");
    {
        Class(1);
        Class(2);
        println(String + "-- end 1 (dtor 2,1 next) --");
    }

    // FORM 1 — a multi-field class constructed by a nameless statement.
    println(String + "== 2: form-1 multi-arg ==");
    {
        Pair(3, 4);
        println(String + "-- end 2 (dtor 3,4 next) --");
    }

    // FORM 1 — no constructor args: the field takes its default/zero.
    println(String + "== 3: form-1 no args (default field 0) ==");
    {
        Class();
        println(String + "-- end 3 (dtor 0 next) --");
    }

    // a nameless statement-form object and a NAMED local share a scope: both die
    // at scope end in reverse declaration order.
    println(String + "== 4: nameless beside a named local ==");
    {
        Class a(5);
        Class(6);
        println(String + "-- end 4 (dtor 6,5 next) --");
    }

    // RETURN-form construction: fn returns Class(x), built directly into the
    // caller's named local (one ctor, one dtor at scope end).
    println(String + "== 5: return-construction into a named local ==");
    {
        Class cls = fn(7);
        println(String + "-- end 5 (dtor 7 next) --");
    }

    // DIRECT construction as a named-local initializer: built in place (one ctor),
    // dtor at scope end.
    println(String + "== 6: direct construction into a named local ==");
    {
        Class y = Class(8);
        println(String + "-- end 6 (dtor 8 next) --");
    }

    // construction-init with no args.
    println(String + "== 7: construction-init, no args ==");
    {
        Class z = Class();
        println(String + "-- end 7 (dtor 0 next) --");
    }

    // FORM 2 — method call on a direct construction temporary: dtor at the
    // semicolon (before the next statement).
    println(String + "== 8: form-2 method call on a direct temp ==");
    {
        Class(9).print();
        println(String + "-- end 8 (dtor 9 already ran) --");
    }

    // FORM 2 — method call on a RETURNED temporary: dtor at the semicolon.
    println(String + "== 9: form-2 method call on a returned temp ==");
    {
        fn(10).print();
        println(String + "-- end 9 (dtor 10 already ran) --");
    }

    // FORM 2 — read a field off a construction temporary in an initializer. The
    // value is read; the temp is destroyed at the end of the DECL statement (the
    // scalar rhs is seq-wrapped), before the next statement.
    println(String + "== 10: form-2 field read in an initializer ==");
    {
        int v = Class(11).c_;
        println(String + "v= " + v);
        println(String + "-- end 10 (dtor 11 already ran) --");
    }

    // FORM 2 — a METHOD CALL on a construction temporary, used as a VALUE: the temp
    // is lifted (built, the method called on it, its scalar result read), then
    // destroyed at the end of the DECL statement (the scalar rhs is seq-wrapped). A
    // loop/if CONDITION instead rebuilds it per evaluation — see 10c/10d.
    println(String + "== 10b: form-2 method call as a value ==");
    {
        int g = Class(15).get();
        println(String + "g= " + g);
        println(String + "-- end 10b (dtor 15 already ran) --");
    }

    // FORM 2 — a construction-temporary method call in an IF CONDITION: the
    // condition is evaluated once, so the temp is lifted before the if and
    // destroyed right after it.
    println(String + "== 10c: form-2 method call in an if condition ==");
    {
        if (Class(16).get() > 0) {
            println(String + "positive");
        }
        println(String + "-- end 10c (dtor 16 ran BEFORE the body) --");
    }

    // FORM 2 — a construction-temporary method call in a WHILE condition: re-built
    // AND destroyed EACH iteration (the condition seq re-evaluates per pass), so the
    // ctor/dtor are balanced per iteration.
    println(String + "== 10d: form-2 method call in a while condition ==");
    {
        int n = 0;
        while (Class(n).get() < 2) {
            println(String + "loop " + n);
            n += 1;
        }
        println(String + "-- end 10d --");
    }

    // FORM 2 — a construction passed as a function argument: built, passed by
    // reference, destroyed at the semicolon.
    println(String + "== 11: construction as a function argument ==");
    {
        sink(Class(12));
        println(String + "-- end 11 (dtor 12 already ran) --");
    }

    // form 1 and form 2 in one scope: the form-2 temporary (14) dies at its
    // semicolon, BEFORE the form-1 object (13) dies at scope end.
    println(String + "== 12: form-1 and form-2 interacting ==");
    {
        Class(13);
        Class(14).print();
        println(String + "-- mid 12 (dtor 14 ran; dtor 13 at scope end) --");
    }

    // FORM 1 in a loop: each iteration constructs and destroys its own object at
    // the end of the loop body — no stack growth, balanced ctor/dtor per pass.
    println(String + "== 13: form-1 in a loop ==");
    {
        int i = 0;
        while (i < 3) {
            Class(i);
            i = i + 1;
        }
        println(String + "-- end 13 --");
    }

    // multi-arg DIRECT construction into a named local.
    println(String + "== 14: multi-arg construction-init ==");
    {
        Pair p = Pair(30, 40);
        println(String + "p.a_= " + p.a_);
    }

    // move-init from a construction temporary — one ctor, one dtor (no temp).
    println(String + "== 15: move-init from a construction ==");
    {
        Class y <-- Class(50);
        println(String + "y= " + y.c_);
    }

    // move-init from a returned temporary.
    println(String + "== 16: move-init from a returned temp ==");
    {
        Class cls <-- fn(51);
        println(String + "cls= " + cls.c_);
    }

    // two FORM-2 temporaries in one statement: reverse destruction order at ';'.
    println(String + "== 17: two temporaries in one call ==");
    {
        sink2(Class(60), Class(61));
        println(String + "-- end 17 (dtor 61,60 already ran) --");
    }

    // a construction whose ARGUMENT is itself a construction's field.
    println(String + "== 18: nested construction argument ==");
    {
        Class(Class(70).c_).print();
        println(String + "-- end 18 --");
    }

    // partial-arg and all-default construction of a class with field defaults.
    println(String + "== 19: default-field construction ==");
    {
        Def(5);
        Def();
        println(String + "-- end 19 (dtor 10,20 then 5,20) --");
    }

    // a class with a CLASS-typed field: the inner ctor runs first, the wrapper is
    // torn down before the contained object.
    println(String + "== 20: class-typed field ==");
    {
        Wrap(80);
        println(String + "-- end 20 --");
    }

    // qualified nameless construction of a nested class (only the inner object).
    println(String + "== 21: qualified nested-class construction ==");
    {
        Host:Inner(90);
        println(String + "-- end 21 --");
    }

    // a field read used inside a larger expression.
    println(String + "== 22: field read in a larger expression ==");
    {
        int v = Class(100).c_ + 1;
        println(String + "v= " + v);
        println(String + "-- end 22 --");
    }

    // FORM 2 — a construction under `&&` whose LHS is FALSE: the short-circuit skips
    // the RHS, so the construction's ctor/dtor must NOT run (it is lifted into the
    // RHS's own conditional sub-seq, not the unconditional condition pre).
    println(String + "== 23: && short-circuit, rhs construction SKIPPED ==");
    {
        if (false && Class(23).get() > 0) {
            println(String + "unreachable 23");
        }
        println(String + "-- end 23 (no ctor/dtor 23) --");
    }

    // FORM 2 — a construction under `&&` whose LHS is TRUE: the RHS is evaluated, so
    // the temp is built and destroyed inside the condition (ctor/dtor before the body).
    println(String + "== 24: && rhs evaluated, ctor/dtor balanced ==");
    {
        if (true && Class(24).get() > 0) {
            println(String + "body 24");
        }
        println(String + "-- end 24 (ctor/dtor 24 ran before body) --");
    }

    // FORM 2 — a construction under `||` whose LHS is TRUE: the short-circuit skips
    // the RHS, so no ctor/dtor.
    println(String + "== 25: || short-circuit, rhs construction SKIPPED ==");
    {
        if (true || Class(25).get() > 0) {
            println(String + "body 25");
        }
        println(String + "-- end 25 (no ctor/dtor 25) --");
    }

    // FORM 2 — a construction under `||` whose LHS is FALSE: the RHS is evaluated.
    println(String + "== 26: || rhs evaluated ==");
    {
        if (false || Class(26).get() > 0) {
            println(String + "body 26");
        }
        println(String + "-- end 26 (ctor/dtor 26 ran before body) --");
    }

    // FORM 2 — a construction in the RHS of `&&` in a WHILE condition: rebuilt each
    // pass while the LHS holds, and SKIPPED on the exit test when the LHS is false
    // (no stray ctor 2).
    println(String + "== 27: && in a while condition, rebuilt per pass, skipped on exit ==");
    {
        int i = 0;
        while (i < 2 && Class(i).get() >= 0) {
            println(String + "loop 27: " + i);
            i = i + 1;
        }
        println(String + "-- end 27 (no ctor/dtor on the exit test) --");
    }

    // FORM 2 — a construction in the LHS of `&&`: the LHS runs UNCONDITIONALLY, so it
    // is lifted into the condition pre (ctor/dtor around the whole evaluation).
    println(String + "== 28: lhs construction always runs ==");
    {
        if (Class(28).get() > 0 && true) {
            println(String + "body 28");
        }
        println(String + "-- end 28 (ctor/dtor 28 ran before body) --");
    }

    // FORM 2 — a construction under a NESTED short-circuit: `(true && false)` is false,
    // so the outer `&&` skips its RHS construction.
    println(String + "== 29: nested short-circuit skips the construction ==");
    {
        if (true && false && Class(29).get() > 0) {
            println(String + "unreachable 29");
        }
        println(String + "-- end 29 (no ctor/dtor 29) --");
    }

    // NON-CONDITION position — a construction in the RHS of `&&` in a DECL initializer
    // whose LHS is FALSE: the RHS is still conditionally evaluated, so it is skipped.
    println(String + "== 30: && rhs construction in a decl initializer, SKIPPED (lhs false) ==");
    {
        bool r = false && Class(30).get() > 0;
        println(String + "r= " + r);
        println(String + "-- end 30 (no ctor/dtor 30) --");
    }

    // NON-CONDITION position — same, LHS TRUE: the RHS construction runs and is torn
    // down inside the initializer's `&&` sub-seq.
    println(String + "== 31: && rhs construction in a decl initializer, evaluated (lhs true) ==");
    {
        bool r = true && Class(31).get() > 0;
        println(String + "r= " + r);
        println(String + "-- end 31 (ctor/dtor 31 ran) --");
    }

    // NON-CONDITION position — a construction in the RHS of `||` in a DECL initializer
    // whose LHS is FALSE: the RHS runs.
    println(String + "== 32: || rhs construction in a decl initializer, evaluated (lhs false) ==");
    {
        bool r = false || Class(32).get() > 100;
        println(String + "r= " + r);
        println(String + "-- end 32 (ctor/dtor 32 ran) --");
    }

    // FORM 2 — a construction in the RHS of `&&` in a FOR-LONG condition: rebuilt each
    // pass while the LHS holds, skipped on the exit test.
    println(String + "== 33: && in a for-long condition, rebuilt per pass ==");
    {
        for (int i = 0) (i < 2 && Class(i).get() >= 0) { i = i + 1; } {
            println(String + "loop 33: " + i);
        }
        println(String + "-- end 33 (no ctor/dtor on the exit test) --");
    }

    {
        NoInitClass(int x_) {
            _() { println(String + "NoInitClass:ctor"); }
            ~() { println(String + "NoInitClass:dtor"); }
        }
        NoInitClass;
        NoInitClass();
        NoInitClass(1);
        tuple = (NoInitClass, 7);
        NoInitClass array[2] = (NoInitClass, NoInitClass);
        println(String + tuple[0].x_ + " " + array[0].x_);
    }

    // FORM 2 — a construction as the SOURCE OF A TRANSFER into LIVE storage. These four
    // were negatives ("Constructing a class in this position is not yet supported"): a live
    // target has no fresh slot to BUILD into, so the construction was rejected. But that is
    // the reason for the ELIDE, not a reason to reject — a live target wants a TRANSFER, and
    // the source only has to be materialized first. It is built as a temporary (its ctor
    // runs), copied / moved into the target through the target's operator, and destroyed at
    // the SEMICOLON — the same lifetime any other construction temporary has.
    println(String + "== 34: a construction transferred into live storage ==");
    {
        Class w = Class(1);
        w = Class(11);                       // re-assign an existing variable
        println(String + "34 assign: w= " + w.get());              // 11

        Class m = Class(2);
        m <-- Class(22);                     // move onto an existing variable
        println(String + "34 move: m= " + m.get());                // 22

        Class^ r = ^w;
        r^ = Class(33);                      // through a dereference store target
        println(String + "34 deref: w= " + w.get());               // 33

        Class arr[2] = (4, 5);
        arr[0] = Class(44);                  // into an array element
        println(String + "34 element: arr= " + arr[0].get() + " " + arr[1].get());   // 44 5
        println(String + "-- end 34 --");
    }

    // FORM 1 — a field store into a nameless object: `Class.c_ = 1;` is an unnamed local
    // VARIABLE. ctor at site, the store, dtor at SCOPE END (it sees the stored value).
    // The initializer list is optional: every spelling of it is accepted.
    println(String + "== 35: form-1 field store ==");
    {
        Class.c_ = 1;
        Class().c_ = 2;
        Class(3).c_ = 4;
        Pair(0, 5).a_ = 6;
        println(String + "-- end 35 (dtor 6,5 then 4, 2, 1 next) --");
    }

    // FORM 1 — the other store shapes: an element store, a compound store, a nested field.
    println(String + "== 36: form-1 element, compound, nested field stores ==");
    {
        Vec.v_[1] = 7;
        Class(8).c_ += 1;
        Wrap.in_.c_ = 10;
        println(String + "-- end 36 (Wrap dtor, Class dtor 10, then 9, then Vec 0,7 next) --");
    }

    // FORM 1 — a nameless assignment through the DEFAULT op=: the default copy has no side
    // effect of its own, so it is an unnamed local VARIABLE — dtor at SCOPE END.
    println(String + "== 37: form-1 default assignment ==");
    {
        Class src(11);
        Class = src;
        Class(12) = src;
        println(String + "-- end 37 (dtor 11, 11, then src 11 next) --");
    }

    // FORM 2 — a nameless assignment through a USER op=: `Asg = 13;` is `Asg.op=(13);`, a
    // method call on a temporary — dtor at the SEMICOLON.
    println(String + "== 38: form-2 user assignment ==");
    {
        Asg = 13;
        println(String + "-- mid 38 (dtor 13 already ran) --");
        Asg(14) = 15;
        println(String + "-- end 38 (dtor 15 already ran) --");
    }

    // FORM 2 — a TRIVIAL class with a user op=: the op= is the side effect, no ctor/dtor.
    println(String + "== 39: form-2 user assignment, trivial class ==");
    {
        TrivAsg = 16;
        TrivAsg() = 17;
        TrivAsg(1) = 18;
        println(String + "-- end 39 --");
    }

    // FORM 1 and FORM 2 assignments in one scope: the form-2 temp dies at its semicolon,
    // the form-1 object at scope end.
    println(String + "== 40: form-1 and form-2 assignments interacting ==");
    {
        Class src(19);
        Class(20) = src;
        Asg(21) = 22;
        println(String + "-- end 40 (dtor 22 ran; dtor 19, src 19 next) --");
    }

    // `Class(group) = rhs;` vs the tuple conversion shorthand `t (T,...) = e;`: a value
    // group is never types, a type group never values. A group of NAMES parses both ways —
    // what the names ARE decides (resolve). Here `n` is a variable: a nameless assignment.
    // `pr (Num, Num) = (23, 24);` names types: the conversion shorthand, unchanged.
    println(String + "== 41: a group of names decides by what the names are ==");
    {
        int n = 24;
        Asg(n) = 25;
        alias Num = int;
        (int, int) pr = (0, 0);
        pr (Num, Num) = (23, 24);
        println(String + "pr= " + pr[0] + "," + pr[1]);
        println(String + "-- end 41 --");
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

/* a construction with an argument of the wrong type. (a string literal is
   `const char[N]`, N counting the NUL — so "hi" is const char[3].) */
//-EXPECT-ERROR: Cannot assign 'const char[3]' to 'int'
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

/* a construction as the source of a TRANSFER into live storage — a deref store target, an
   array element, a move onto an existing variable, a re-assignment — is SUPPORTED (block 34).
   The construction is materialized as a temporary and transferred in through the target's
   operator; it does NOT field-init over a live object, which is what the old rejection was
   guarding against. */

/* a nameless FIELD STORE is form 1: a trivial class gives it no chance of a side effect. */
//-EXPECT-ERROR: A nameless class statement has no effect
//Trivial(int t_) {
//}
//void neg_trivial_store() {
//    Trivial.t_ = 5;
//}

/* the same, with an initializer list — every spelling of it is accepted or rejected alike. */
//-EXPECT-ERROR: A nameless class statement has no effect
//Trivial(int t_) {
//}
//void neg_trivial_store_args() {
//    Trivial(1).t_ = 5;
//}

/* a nameless assignment through the DEFAULT op= is form 1: a trivial class has no effect. */
//-EXPECT-ERROR: A nameless class statement has no effect
//Trivial(int t_) {
//}
//void neg_trivial_default_assign() {
//    Trivial src(1);
//    Trivial = src;
//}

/* the same, with an initializer list. */
//-EXPECT-ERROR: A nameless class statement has no effect
//Trivial(int t_) {
//}
//void neg_trivial_default_assign_args() {
//    Trivial src(1);
//    Trivial(2) = src;
//}

/* `name(args) = rhs` assigns a nameless object only when name is a class. */
//-EXPECT-ERROR: Cannot assign to the result of a function call
//int ret(int x) { return x; }
//void neg_assign_call() {
//    ret(1) = 2;
//}

/* a group of names that mixes a type and a value is neither reading. (a type KEYWORD,
   `Asg(n, int32)`, never gets here: the tokens alone make that group a type.) */
//-EXPECT-ERROR: mixes types and values
//void neg_mixed_group() {
//    int n = 1;
//    alias Num = int;
//    Asg(n, Num) = 2;
//}

/* a bare FIELD READ of a nameless object is neither an assignment nor a method: no effect.
   (the parenless spelling is rejected by the GRAMMAR: a name-led chain with no `=` is a
   syntax error for any name — `obj.f;` too — so it never reaches the nameless check.) */
//-EXPECT-ERROR: Expected '=' after
//void neg_bare_field_read() {
//    Class.c_;
//}

/* the same, with an initializer list. */
//-EXPECT-ERROR: A nameless class expression has no effect
//void neg_bare_field_read_args() {
//    Class(1).c_;
//}

/* a field that the class does not declare. */
//-EXPECT-ERROR: has no field 'nope'
//void neg_unknown_field() {
//    int x = Class(1).nope;
//    println(String + "x= " + x);
//}

/* constructing a name that is not a class. */
//-EXPECT-ERROR: Unknown function 'Bogus'
//void neg_unknown_class() {
//    Bogus(1);
//}

