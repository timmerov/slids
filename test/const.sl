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


    /* compile error: cannot truncate constants. */
    //-EXPECT-ERROR: truncate constant
    // const int bad_pi = 3.14;
    //-EXPECT-ERROR: overflows declared type
    // const int32 overflow = 10_000_000_000;
    //-EXPECT-ERROR: negative constant
    // const uint sign_err = -1;

    /* compile error: rhs references a name that doesn't resolve. */
    //-EXPECT-ERROR: unknown name
    // const int unk = unknown_name;

    /* compile error: rhs is not a constant expression. */
    //-EXPECT-ERROR: not allowed in a constant initializer
    // const int nf = foo();

    /* compile error: bitwise operation on a float. */
    //-EXPECT-ERROR: Bitwise
    // const int bf = 3.14 & 1;

    /* compile error: division by zero. */
    //-EXPECT-ERROR: Division by zero
    // const int dz = 10 / 0;

    return 0;
}

/* compile error: cycle in const initializers (file scope). */
//-EXPECT-ERROR: cyclic initializer
// const int aa = bb;
// const int bb = aa;

/* compile error: duplicate global const name. */
//-EXPECT-ERROR: already declared
// const int dup = 1;
// const int dup = 2;
