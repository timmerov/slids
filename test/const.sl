/*
test constants

first usage: straight substitutions.
the const value has no storage.
the value is substituted wherever it appears.
rhs is a const expression.

pi is substituted;
const float pi = 3.14;

second usage: const methods
do not modify self or anything accessible
via self.

String's size method does not modify self.
intptr const String:size();

third usage: const syntax.
semantics are not enforced.

const/mutable before thing modified.
const/mutable are part of the type.
const/mutable bind after dereference.
const-ness is inherited by everything accessible
via the const thing.
mutable within a const type is a compile error.
const can be added/removed by casting.

a,b,c variables; abc is immutable:
const int abc = a * b * c;

begin returns an immutable pointer to char.
const char^ const String:begin();

end returns a mutable pointer to an immutable char.
(const char)^ const String:end();

adding/removing constness via casting:
const_ptr = mut_ptr;
const_ptr = <const> mut_ptr;
mut_ptr = <mutable> const_ptr;
compile error:
mut_ptr = const_ptr;

compile error:
const (mutable char)^ const next(char^);

compile errors:
const String s;
s.storage_ = nullptr;
s.storage_[0] = 0;

##type(x) is const aware.
##type(pi) is const float
##type(abc) is const int
##type(const_ptr) is const type^
##type(mut_ptr) is type^
##type(s) is const String
iter = s.begin()
stop = s.end();
ch = iter^;
##type(iter) is const char^
##type(stop) is (const char)^
##type(ch) = is const char
*/

/* global scope. */
const float pi = 3.14;

/* forward reference: consumer above producer in source order. */
const float circle_full = tau;
const float tau = 2 * pi;

/* function scope. */
int foo() {
    const int x = 42;
    return x;
}

/* nested function scope: const inside a nested function body. */
int outerFn() {
    int innerFn() {
        const int five = 5;
        return five * 2;
    }
    return innerFn();
}

/* class scope, no ctor/dtor */
NoTors(int x_ = 0) {
    /* may widen constants. */
    const float size = 37;

    float foo() {
        return size;
    }

    /* method scope shadows class. */
    int bar() {
        /* infered type. */
        const size = 24;
        return size;
    }
}

/* template function: body-scope const inside a template-free function. */
T doubler<T>(T a) {
    const int factor = 2;
    return a * factor;
}

/* class with a method bearing the const-marker. parse-only this scope —
   no enforcement that the body doesn't modify self. */
ConstMethodHolder(int n_ = 0) {
    int const probe(int x) {
        return x + n_;
    }
}

/* runtime const variable: rhs is not foldable, so it lowers to a regular
   alloca with the const-qualified type carried in the local's type string. */
int runtimeConstFn(int a, int b) {
    const int abc = a * b;
    return abc;
}

/* template class: class-scope const with rhs independent of T. */
Container<T>(int n = 0) {
    const int magic = 42;

    int getMagic() {
        return magic;
    }
}

/* nested slid: a method of an inner class declares its own const. */
Outer(int dummy = 0) {
    Inner(int dummy2 = 0) {
        int innerMethod() {
            const int eight = 8;
            return eight;
        }
    }
}

/* class scope, with dtors. */
WithTors(int x_ = 0) {
    /* rhs is const expression. */
    const float fred = 17 * pi;

    _() {}
    ~() {}

    float foo() {
        return fred;
    }
}

/* const method mismatch */
//-EXPECT-ERROR: not const but its declaration is
// Mismatch(int x_ = 0) {
//     void const print();
// }
// Mismatch {
//     void print() {
//         __println("compile error: const declaration mutable definition.");
//     }
// }

int32 main() {
    NoTors nt;
    WithTors wt;

    __println("pi=" + pi);
    __println("foo()=" + foo());
    __println("nt.size=" + nt.size);
    __println("nt.foo()=" + nt.foo());
    __println("nt.bar()=" + nt.bar());
    __println("wt.fred=" + wt.fred);
    __println("wt.foo()=" + wt.foo());

    /* forward reference (consumer above producer in source). */
    __println("tau=" + tau);
    __println("circle_full=" + circle_full);

    /* class-scope const accessed via Type qualifier (no instance). */
    __println("NoTors.size=" + NoTors.size);

    /* inferred type via type conversion: tt is int8. */
    const tt = (int8 = 4);
    __println("tt=" + tt);

    /* inferred bool from comparison. */
    const bb = (3 > 5);
    __println("bb=" + bb);

    /* int64 upgrade for an out-of-range literal. */
    const big = 10_000_000_000;
    __println("big=" + big);

    /* float round-trip inference: 0.5 -> float, 3.14 -> float64. */
    const r1 = 0.5;
    const r2 = 3.14;
    __println("r1=" + r1);
    __println("r2=" + r2);

    /* char literal. */
    const cc = 'A';
    __println("cc=" + cc);

    /* operator sweep. */
    const int op_unary_neg  = -5;
    const int op_unary_not  = !0;
    const int op_unary_bnot = ~0;
    const int op_add  = 1 + 2;
    const int op_sub  = 5 - 3;
    const int op_mul  = 6 * 7;
    const int op_div  = 10 / 3;
    const int op_mod  = 10 % 3;
    const int op_band = 0xF0 & 0x0F;
    const int op_bor  = 0xF0 | 0x0F;
    const int op_bxor = 0xF0 ^ 0xFF;
    const int op_shl  = 1 << 4;
    const int op_shr  = 256 >> 2;
    const bool op_eq  = (3 == 3);
    const bool op_lt  = (3 < 5);
    const bool op_and = (3 > 1) && (5 > 1);
    const bool op_or  = (3 > 5) || (5 > 1);
    const bool op_xor = (3 > 5) ^^ (5 > 1);
    __println("unary=" + op_unary_neg + "," + op_unary_not + "," + op_unary_bnot);
    __println("arith=" + op_add + "," + op_sub + "," + op_mul + "," + op_div + "," + op_mod);
    __println("bits=" + op_band + "," + op_bor + "," + op_bxor + "," + op_shl + "," + op_shr);
    __println("logic=" + op_eq + "," + op_lt + "," + op_and + "," + op_or + "," + op_xor);

    /* nested function with body-scope const. */
    __println("outerFn()=" + outerFn());

    /* nested slid: const declared in inner class's method. */
    Outer:Inner inner_obj;
    __println("inner_obj.innerMethod()=" + inner_obj.innerMethod());

    /* template class: class-scope const, accessed inside method and via instance. */
    Container<int> c;
    __println("c.magic=" + c.magic);
    __println("c.getMagic()=" + c.getMagic());

    /* template function with body-scope const. */
    __println("doubler<int>(7)=" + doubler<int>(7));

    /* method-const marker parses; no enforcement. */
    ConstMethodHolder cmh;
    __println("cmh.probe(5)=" + cmh.probe(5));

    /* runtime const var (rhs not foldable): lowers to alloca. */
    __println("runtimeConstFn(3,4)=" + runtimeConstFn(3, 4));

    /* const-arg / mutable-param positive cases. */
    __println("callMut()=" + callMut());

    /* qualifier-only cast on a value. ##type carries the qualifier. */
    ck = <const> (int=3);
    __println("ck=" + ck);
    __println("##type(ck)=" + ##type(ck));

    /* ##type fidelity on a const-typed local. */
    const int abc = 10;
    __println("##type(abc)=" + ##type(abc));

    /* template T deduction from a foldable const (block_const_stack_).
       T binds to the canonical (unqualified) form — `int`, not `const int`. */
    __println("doubler(abc)=" + doubler(abc));


    /* compile error: cannot truncate constants. */
    //-EXPECT-ERROR: truncate constant
    // const int bad_pi = 3.14;
    //-EXPECT-ERROR: overflows declared type
    // const int32 overflow = 10_000_000_000;
    //-EXPECT-ERROR: negative constant
    // const uint sign_err = -1;

    /* compile error: bitwise operation on a float. */
    //-EXPECT-ERROR: Bitwise
    // const int bf = 3.14 & 1;

    /* compile error: division by zero. */
    //-EXPECT-ERROR: Division by zero
    // const int dz = 10 / 0;

    return 0;
}

/* compile error: rhs references a name that doesn't resolve (file scope —
   block-scope non-foldable rhs becomes a runtime alloca). */
//-EXPECT-ERROR: unknown name
// const int unk = unknown_name;

/* compile error: rhs is not a constant expression at file scope. */
//-EXPECT-ERROR: not allowed in a constant initializer
// const int nf = pi + foo();

/* compile error: cycle in const initializers (file scope). */
//-EXPECT-ERROR: cyclic initializer
// const int aa = bb;
// const int bb = aa;

/* compile error: duplicate global const name. */
//-EXPECT-ERROR: already declared
// const int dup = 1;
// const int dup = 2;

ConstOps(int x_ = 0) {
    /* valid syntax. */
    const _() { }
    const ~() { }

    /* compile error: no-return op cannot be const. */
    //-EXPECT-ERROR: cannot be const
    // const op=(ConstOps^ rhs) { }

    /* compile error: no-return op cannot have an explicit return type. */
    //-EXPECT-ERROR: cannot have an explicit return type
    // void const op=(ConstOps^ rhs) { }
}

/* ----------------------------------------------------------------------
   const-arg → mutable-param rejection (call-site overload match).

   `mutable T^` is the only place const/mutable affects matching today;
   every other matcher canonicalizes (`const T^` and `T^` collapse). The
   check fires at: free-fn call, method call (single & overloaded),
   template-fn call. Diagnostic carets the offending arg with a note
   pointing at the `mutable` keyword on the param.
   ---------------------------------------------------------------------- */

/* free function with a mutable pointer param. */
void freeMut(mutable int^ p) { p^ = 99; }

/* method with a mutable pointer param. */
Bag(int n_ = 0) {
    void setVia(mutable int^ p) { p^ = n_; }
}

/* overloaded methods — mutable bit picks the slot at the call site.
   per the matcher, `foo(int^)` and `foo(mutable int^)` collide on
   canonical key, so distinct overloads must differ in some other slot. */
Pair(int a_ = 0, int b_ = 0) {
    void store(int^ q, int idx)         { q^ = a_ + idx; }
    void store(mutable int^ q, char[] s) { q^ = b_; }
}

/* template function with a mutable pointer param. */
void tmplMut<T>(mutable T^ p) { p^ = p^; }

int callMut() {
    int x = 0;
    int^ mp = ^x;

    /* positive: mutable T^ caller binds to mutable T^ param. */
    freeMut(mp);

    /* positive: T^ (canonically mutable) caller binds to mutable T^. */
    freeMut(^x);

    /* positive: method dispatch, mutable caller. */
    Bag b(7);
    b.setVia(mp);

    /* positive: overload pick — mutable slot, two-arg overload. */
    Pair pr(1, 2);
    pr.store(mp, "tag");

    /* positive: template instantiation with mutable caller. */
    tmplMut<int>(mp);

    return x;
}

/* compile error: const arg → mutable free-fn param. */
//-EXPECT-ERROR: const argument
// int callConst_free() {
//     int x = 0;
//     const int^ cp = ^x;
//     freeMut(cp);
//     return 0;
// }

/* compile error: const arg → mutable method param. */
//-EXPECT-ERROR: const argument
// int callConst_method() {
//     int x = 0;
//     const int^ cp = ^x;
//     Bag b(3);
//     b.setVia(cp);
//     return 0;
// }

/* compile error: const arg → mutable overloaded method param. */
//-EXPECT-ERROR: const argument
// int callConst_overload() {
//     int x = 0;
//     const int^ cp = ^x;
//     Pair pr(1, 2);
//     pr.store(cp, "tag");
//     return 0;
// }

/* compile error: const arg → mutable template-fn param. */
//-EXPECT-ERROR: const argument
// int callConst_template() {
//     int x = 0;
//     const int^ cp = ^x;
//     tmplMut<int>(cp);
//     return 0;
// }

/* ----------------------------------------------------------------------
   body-level const enforcement.

   const T x       — rvalue-categorized; no writes through x.
   const T^ p      — const reference: both p (rebind) and p^ (deref-write) rejected.
   (const T)^ p    — reference to const: p rebindable; p^ rejected.

   `mutable` is unaffected — already rejected at call sites by rejectConstToMutable.
   ---------------------------------------------------------------------- */

/* positive: reads of const locals + rebind-allowed reference-to-const.
   `(const T)^` is not a directly-spelled declaration shape today — obtain
   one via address-of of a const local (^xc produces (const int)^). */
int readConst() {
    int a = 7;
    int b = 8;
    const int rt_c = a * b;       /* runtime-const local */
    int sum = rt_c + 1;
    const int xc = a + b;         /* another runtime-const local */
    rtc = ^xc;                    /* inferred type: (const int)^ */
    int via = rtc^;               /* read through reference-to-const */
    const int yc = a - b;
    rtc = ^yc;                    /* rebind the (mutable) handle */
    via = via + rtc^;
    return sum + via;
}

/* compile error: write to const local. */
//-EXPECT-ERROR: const
// int writeConstLocal() {
//     int a = 7; int b = 8;
//     const int rt_c = a * b;
//     rt_c = 99;
//     return rt_c;
// }

/* compile error: compound-assign to const local. */
//-EXPECT-ERROR: const
// int compoundConstLocal() {
//     int a = 7; int b = 8;
//     const int rt_c = a * b;
//     rt_c += 1;
//     return rt_c;
// }

/* compile error: pre-increment on const local. */
//-EXPECT-ERROR: const
// int preIncConstLocal() {
//     int a = 7; int b = 8;
//     const int rt_c = a * b;
//     ++rt_c;
//     return rt_c;
// }

/* compile error: rebind of const reference. */
//-EXPECT-ERROR: const
// int rebindConstRef() {
//     int a = 0; int b = 0;
//     const int^ p = ^a;
//     p = ^b;
//     return 0;
// }

/* compile error: deref-write through const reference. */
//-EXPECT-ERROR: const
// int writeThroughConstRef() {
//     int a = 0;
//     const int^ p = ^a;
//     p^ = 5;
//     return 0;
// }

/* compile error: deref-write through reference-to-const. */
//-EXPECT-ERROR: const
// int writeThroughRefToConst() {
//     int a = 1; int b = 2;
//     const int xc = a + b;
//     rtc = ^xc;
//     rtc^ = 5;
//     return 0;
// }

/* compile error: delete of const reference. */
//-EXPECT-ERROR: const
// int deleteConstRef() {
//     int a = 0;
//     const int^ p = ^a;
//     delete p;
//     return 0;
// }

/* compile error: delete through reference-to-const. */
//-EXPECT-ERROR: const
// int deleteRefToConst() {
//     int a = 0; int b = 1;
//     const int xc = a + b;
//     rp = ^xc;
//     delete rp;
//     return 0;
// }

/* compile error: write to const by-value param. */
//-EXPECT-ERROR: const
// int writeConstParam(const int p) {
//     p = 0;
//     return p;
// }

/* ----------------------------------------------------------------------
   Phase 3: const-method `self` enforcement.

   `is_const_method` sets self's type to `const SlidName` at method entry.
   Field writes (`self.field = ...`, `field_ = ...`), self-rebinding
   (`self = ...`), and any other write through self are rejected.
   ---------------------------------------------------------------------- */

/* positive: const method that only reads is fine. */
ConstSelfReader(int n_ = 0, int m_ = 0) {
    int const total() {
        return n_ + m_;             /* read-only: allowed */
    }
}

/* compile error: const method writes to a field. */
//-EXPECT-ERROR: const method
// ConstSelfWriter(int n_ = 0) {
//     void const setN(int v) {
//         n_ = v;
//     }
// }

/* compile error: const method pre-increments a field. */
//-EXPECT-ERROR: const
// ConstSelfIncer(int n_ = 0) {
//     void const bump() {
//         ++n_;
//     }
// }

/* compile error: const method compound-assigns a field. */
//-EXPECT-ERROR: const
// ConstSelfCompoundEr(int n_ = 0) {
//     void const addN(int v) {
//         n_ += v;
//     }
// }

/* compile error: const method assigns self. */
//-EXPECT-ERROR: const
// ConstSelfReplacer(int n_ = 0) {
//     void const replace(ConstSelfReplacer^ other) {
//         self = other^;
//     }
// }
