/*
test nested functions.

a nested function has read/write visibility to the local variables
and the parameters of the host function.

    int foo(int x) {
        int y = 0;
        z = bar(x);

        int bar(int a) {
            int b = y;
            return b;
        }
        return z;
    }

nested functions may recurse on themselves.

notes:

mulitple nesting is not currently supported.
needs to be revisited.

calling sibling nested functions is not currently supported.
needs to be revisited.

*/

/*
claude says (scope):

a nested function may be declared in ANY scope of its host -- the body's top
level, or a nested block / if-arm / loop body / switch case. It belongs to the
frame it is written in: visible within that scope (including before its own
definition), and gone when the scope closes. It captures that scope's locals.

that is sound for the same reason a top-level capture is: a capture is the host
alloca's ADDRESS, every alloca lives in the ONE function frame, and the function
can only be CALLED from inside the scope where its captures are live.

(before 2026-07-12 the signature pre-pass scanned only a function body's DIRECT
children, so a function written inside a block was never registered: calling it
said "Unknown function", and NOT calling it left its body unresolved and CRASHED
the compiler on an unstamped identifier. Both passes -- signatures, then deferred
bodies -- now run per SCOPE, like the local-class pre-pass beside them.)

a function may be nested in a FUNCTION, a METHOD or an OPERATOR (and in a ctor /
dtor) -- all of them ARE functions, so all of them take the same path. there is no
such thing as a "method nested in a method": a nested function inside a method is
just a FUNCTION, one that INHERITS self IF IT NEEDS TO. it needs to when it names a
field (a bare `v_` rewrites to `self.v_`) or names `self` outright; either way
`self` is an ordinary local of the method's frame, so it captures like any other.
*/

/*
claude says:

- a nested function is lifted to a top-level function; captured host
  locals/params are passed by reference (the host alloca's address), so reads
  see the host's current value at call time and writes propagate back. (A
  read-only capture is observably the same as by-value; the by-value form is a
  deferred optimization.)
- visible in the host body (incl. before its definition) and its own body
  (self-recursion). Deep nesting + sibling calls are unsupported.

- A NESTED BODY IS LOWERED LIKE ANY OTHER BODY (fixed 2026-07-12). Every one of
  desugar's four passes -- the sret/construction lift, aggregate-copy lowering,
  PPID (++/--), and NRVO -- now runs on a nested function, with its OWN return
  type. Before, NONE of them did: they looped over PROGRAM-scope functions, and a
  nested function is a STATEMENT inside its host's body, so its statements reached
  codegen unlowered and asserted. A bare `i++` in a nested body was enough to kill
  the compiler ("inc/dec survived desugar's PPID pass") -- no classes needed. The
  four cases below pin the four passes; each also crosses a CAPTURE, which is the
  part that could not be settled by reading (a captured host var is a by-reference
  param, so `^capture` is the host's address -- writes propagate back, including a
  chain's move into a captured class).
*/

/* a nested function using only its own parameter (no capture). */
int basic(int x) {
    int dbl(int a) {
        return a + a;
    }
    return dbl(x);
}

/* a read-only capture of a host local. */
int read_cap(int x) {
    int y = 10;
    int bar(int a) {
        return a + y;
    }
    return bar(x);
}

/* a WRITTEN capture (by reference) — the host sees the accumulated change. */
int write_cap(int x) {
    int acc = 0;
    void add(int a) {
        acc = acc + a;
    }
    add(x);
    add(x);
    return acc;
}

/* a nested function recursing on itself. */
int recurse(int n) {
    int fact(int k) {
        if (k <= 1) {
            return 1;
        }
        return k * fact(k - 1);
    }
    return fact(n);
}

/* the canon shape: the nested function is called before its definition, and
   reads a host local. */
int call_before(int x) {
    int y = 0;
    z = bar(x);
    int bar(int a) {
        int b = y;
        return b + a;
    }
    return z;
}

/* a void nested function with a bare `return;`, capturing host vars by read and
   by write. */
int void_nested(int n) {
    int r = 0;
    void classify() {
        if (n > 0) {
            r = 1;
            return;
        }
        r = -1;
    }
    classify();
    return r;
}

/* a nested function with an optional (default) parameter. */
int with_def(int x) {
    int g(int a, int b = 10) {
        return a + b;
    }
    return g(x);
}

/* a nested forward declaration (signature only), defined later in the body —
   like a file-scope forward declaration. */
int fwd_nested(int x) {
    int bar(int a);
    z = bar(x);
    int bar(int a) {
        return a + 1;
    }
    return z;
}

/* call nested function with local class. */
void fn_local_class() {

    LocalClass obj(42);
    nested_fn(^obj);
    obj.x_ = 37;
    nested_fn(obj);

    void nested_fn(LocalClass^ lc) {
        __println("fn_local_class:nested_fn: " + obj.x_);
    }

    LocalClass(int x_) { }
}

/* A class whose ctor/dtor PRINT, so a temporary inside a nested body is COUNTABLE
   and its lifetime is visible. Its operators let a chain run there too. */
Acc(int v_) {
    _() { __println("Acc:ctor: " + v_); }
    ~() { __println("Acc:dtor: " + v_); }
    op=(Acc^ r)           { v_ = r^.v_; }
    op<--(mutable Acc^ r) { __println("Acc:op<--: " + r^.v_); v_ = r^.v_; }
    op+=(Acc^ r)          { v_ += r^.v_; }
    op+(Acc^ x, Acc^ y)   { v_ = x^.v_ + y^.v_; }
}

int take(Acc^ a) { return a^.v_; }

/* PPID (the ++/-- pass) inside a nested body, on a CAPTURED host local -- the bumps
   write back through the capture. This is the case that needs no classes at all: a
   bare `n++` here used to assert. */
int nested_ppid(int x) {
    int n = x;
    int bump() {
        int t = n++;            // post: t takes the OLD n, n becomes x+1
        ++n;                    // pre:  n becomes x+2
        return t;
    }
    int t = bump();
    return t * 100 + n;         // x*100 + (x+2)
}

/* the sret / construction LIFT inside a nested body -- the class temp is built,
   passed, and destroyed at the SEMICOLON (its dtor prints before the return). */
int nested_temp() {
    int inner() {
        int v = take(Acc(9));   // Acc(9) dies here, not at the function's end
        return v;
    }
    return inner();
}

/* the AGGREGATE-COPY pass inside a nested body -- a cross-form array -> tuple copy
   lowers by slot. */
int nested_agg() {
    int arr[3] = (1, 2, 3);
    int sum() {
        (int, int, int) t = arr;
        return t[0] + t[1] + t[2];
    }
    return sum();
}

/* a class-OPERATOR CHAIN inside a nested body, over CAPTURED host objects. A fresh
   local destination IS the accumulator (zero temps). A CAPTURED host destination is a
   LIVE target, so the chain builds a statement-scoped temp and MOVES it in through the
   capture -- op<-- prints, the temp dies at the semicolon, and the HOST sees the write. */
int nested_chain() {
    Acc ha = Acc(1);
    Acc hb = Acc(2);
    Acc hsink = Acc(0);
    int inner() {
        Acc loc = ha + hb;      // fresh local -> the accumulator. zero temps.
        hsink = ha + hb;        // captured host target -> one temp, moved in.
        return loc.v_;
    }
    int r = inner();
    return r * 100 + hsink.v_;  // 3 -> 303
}

/* NRVO inside a nested body -- the fourth pass. A nested function returning a class by
   value gets the same return-slot analysis as any other function: the returned local is
   aliased to the caller's sret slot, so a chain that builds into it builds into the
   CALLER's storage. ONE object for the whole thing -- no temp, no copy, one ctor/dtor. */
Acc nested_nrvo() {
    Acc ha = Acc(1);
    Acc hb = Acc(2);
    Acc make() {
        Acc r = ha + hb;        // r IS the caller's slot; the chain elides into it
        return r;
    }
    Acc got = make();
    return got;
}

/* a nested function declared inside a BLOCK. It lives in the block's frame, captures the
   block's locals, and (like any nested function) may be called before its definition. */
int block_nested(int x) {
    int out = 0;
    {
        int n = x;
        out = bump();               // called BEFORE its definition, inside this block
        int bump() {
            int t = n++;            // captures the BLOCK's local; the write propagates
            return t;
        }
        out = out * 100 + n;        // n was incremented through the capture
    }
    return out;
}

/* a nested function inside a LOOP body -- registered per iteration's scope, and its
   captured loop-body local is fresh each time round. */
int loop_nested(int x) {
    int total = 0;
    int i = 0;
    while (i < 3) {
        int step = x + i;
        int scaled() {
            return step * 10;
        }
        total = total + scaled();
        i = i + 1;
    }
    return total;
}

/* a nested function inside a SWITCH CASE body, and inside a FOR body -- the remaining two
   scope kinds (block / if-arm / while-body are covered above). */
int switch_nested(int x) {
    int r = 0;
    switch (x) {
        1: {
            int m = 5;
            int sc() {
                return m * 2;
            }
            r = sc();
        }
        default: { }
    }
    return r;
}

int for_nested(int x) {
    int total = 0;
    for (int n : 0 .. 3) {
        int fb() {
            return n + x;       // captures the FOR's loop variable
        }
        total = total + fb();
    }
    return total;
}

/* a nested function FORWARD-DECLARED inside a block, defined later in the same block. */
int fwd_in_block(int x) {
    int out = 0;
    {
        int n = x;
        int fwd(int a);         // signature only
        out = fwd(2);           // called before the definition
        int fwd(int a) {
            return a + n;
        }
    }
    return out;
}

/* a nested function inside an IF arm -- same rule, a different scope. */
int if_nested(int x) {
    int r = 0;
    if (x > 0) {
        int base = x;
        int twice() {
            return base + base;
        }
        r = twice();
    } else {
        int neg = 0 - x;
        int twice() {           // a SEPARATE function: the two scopes are disjoint,
            return neg + neg;   // so the same name is not a duplicate
        }
        r = twice();
    }
    return r;
}

/* NESTED FUNCTIONS INSIDE CLASS MEMBERS. A method, an operator and a ctor/dtor are all
   FUNCTIONS, so a nested function works in any of them -- and in any scope of them. A nested
   function inside a method INHERITS self IF IT NEEDS TO: a bare field name rewrites to
   `self.field` and captures, and `self` itself captures, because both are ordinary locals of
   the method's frame. */
Meth(int v_) {
    /* a nested function inside a CONSTRUCTOR body. */
    _() {
        int seed() {
            return 7;
        }
        v_ = v_ + seed();
    }
    ~() { }

    /* captures a METHOD LOCAL -- no self needed. */
    int use_local(int x) {
        int base = x;
        int add() {
            return base + 1;
        }
        return add();
    }

    /* captures a FIELD. The bare `v_` rewrites to `self.v_`, so the nested function
       inherits self. */
    int use_field() {
        int grab() {
            return v_ + 100;
        }
        return grab();
    }

    /* names `self` outright -- the same capture, spelled explicitly. */
    int use_self() {
        int grab() {
            return self.v_ + 200;
        }
        return grab();
    }

    /* declared inside a BLOCK of a method -- the per-scope rule reaches a method body like
       any other. */
    int in_block(int x) {
        int out = 0;
        {
            int n = x;
            int bump() {
                int t = n++;
                return t;
            }
            out = bump() * 100 + n;
        }
        return out;
    }

    /* a nested function inside an OPERATOR body -- an operator is a method is a function. */
    op+=(Meth^ r) {
        int fold(int a, int b) {
            return a + b;
        }
        v_ = fold(v_, r^.v_);
    }
}

/* a class nested in a NAMESPACE, whose method holds a nested function. */
Space {
    Inner(int w_) {
        int go(int x) {
            int helper() {
                return w_ + x;     // captures the field AND the method's param
            }
            return helper();
        }
    }
}

/* a LOCAL class (defined in a function body) whose method holds a nested function -- itself
   declared inside a block. Every scope-registration rule composes. */
int local_class_method(int x) {
    Held(int h_) {
        int run(int a) {
            int out = 0;
            {
                int k = a;
                int twice() {
                    return k + k + h_;
                }
                out = twice();
            }
            return out;
        }
    }
    Held hd(100);
    return hd.run(x);
}

int32 main() {
    __println("basic(5) = " + basic(5));              // 10
    __println("read_cap(5) = " + read_cap(5));        // 15
    __println("write_cap(5) = " + write_cap(5));      // 10
    __println("recurse(5) = " + recurse(5));          // 120
    __println("call_before(7) = " + call_before(7));  // 7
    __println("void_nested(5) = " + void_nested(5));      // 1
    __println("void_nested(-5) = " + void_nested(-5));    // -1
    __println("with_def(5) = " + with_def(5));            // 15
    __println("fwd_nested(5) = " + fwd_nested(5));        // 6
    fn_local_class();                                     // 42 then 37

    // the four desugar passes, inside a nested body, each crossing a capture.
    __println("nested_ppid(5) = " + nested_ppid(5));      // 507
    __println("nested_temp() = " + nested_temp());        // 9
    __println("nested_agg() = " + nested_agg());          // 6
    __println("nested_chain() = " + nested_chain());      // 303
    {
        Acc n = nested_nrvo();
        __println("nested_nrvo() = " + n.v_);             // 3, built in the caller's slot
    }

    // a nested function may live in ANY scope of its host, not just the body's top level.
    __println("block_nested(5) = " + block_nested(5));    // 506
    __println("loop_nested(2) = " + loop_nested(2));      // 20+30+40 = 90
    __println("if_nested(4) = " + if_nested(4));          // 8
    __println("if_nested(-4) = " + if_nested(-4));        // 8
    __println("switch_nested(1) = " + switch_nested(1));  // 10
    __println("for_nested(10) = " + for_nested(10));      // 10+11+12 = 33
    __println("fwd_in_block(5) = " + fwd_in_block(5));    // 7

    // a nested function in a METHOD / OPERATOR / CTOR -- incl. inheriting self.
    {
        Meth mo(1);                                          // ctor: 1 + seed() = 8
        __println("Meth ctor nested = " + mo.v_);            // 8
        __println("Meth use_local(5) = " + mo.use_local(5)); // 6
        __println("Meth use_field() = " + mo.use_field());   // 108  (inherits self)
        __println("Meth use_self() = " + mo.use_self());     // 208  (inherits self)
        __println("Meth in_block(5) = " + mo.in_block(5));   // 506
        Meth mp(2);                                          // ctor: 2 + seed() = 9
        mo += mp;                                            // 8 + 9 = 17
        __println("Meth op+= nested = " + mo.v_);            // 17
    }
    Space:Inner si(7);
    __println("ns-class method nested = " + si.go(3));               // 7 + 3 = 10
    __println("local_class_method(5) = " + local_class_method(5));   // 5 + 5 + 100 = 110
    return 0;
}

/* deep nesting (a nested function inside a nested function) is unsupported. */
//-EXPECT-ERROR: Nested functions may not contain further nested functions.
//int neg_deep(int x) {
//    int mid(int a) {
//        int inner(int b) {
//            return b;
//        }
//        return inner(a);
//    }
//    return mid(x);
//}

/* deep nesting is still rejected when the inner function hides in a BLOCK — the check is
   "am I inside a nested function's body", not "am I a direct child of one". */
//-EXPECT-ERROR: Nested functions may not contain further nested functions.
//int neg_deep_block(int x) {
//    int mid(int a) {
//        {
//            int inner(int b) {
//                return b;
//            }
//        }
//        return a;
//    }
//    return mid(x);
//}

/* a non-void nested function must return on all paths, like any function. */
//-EXPECT-ERROR: Function 'bar' must end with a return statement.
//int neg_noret(int x) {
//    int bar(int a) {
//    }
//    return bar(x);
//}

/* one nested function calling another (a sibling) is unsupported. */
//-EXPECT-ERROR: Calling a sibling nested function is not supported.
//int neg_sibling(int x) {
//    int a(int v) {
//        return b(v);
//    }
//    int b(int v) {
//        return v + 1;
//    }
//    return a(x);
//}

/* a captured host variable must be definitely-assigned at the call. */
//-EXPECT-ERROR: Use of uninitialized variable 'y'.
//int neg_cap_uninit() {
//    int y;
//    int bar() {
//        return y;
//    }
//    return bar();
//}

/* a nested forward declaration with no matching definition is an orphan. */
//-EXPECT-ERROR: Function 'bar' is declared but never defined.
//int neg_decl_no_def(int x) {
//    int bar(int a);
//    return x;
//}

/* two definitions of one nested function is a duplicate definition. */
//-EXPECT-ERROR: Duplicate definition of 'bar'.
//int neg_decl_two_defs(int x) {
//    int bar(int a) {
//        return a;
//    }
//    int bar(int a) {
//        return a + 1;
//    }
//    return bar(x);
//}

/* nested functions do not overload — a same-named nested function with a
   conflicting signature is a duplicate declaration. */
//-EXPECT-ERROR: Duplicate declaration of 'bar'.
//int neg_decl_sig_mismatch(int x) {
//    int bar(int a);
//    int bar(int a, int b) {
//        return a + b;
//    }
//    return x;
//}
