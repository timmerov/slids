/*
test using class fields in statements and expressions.
test inferring field types from default values.

the default values for fields must be foldable constants or aggregates of
foldable constants.

claude says (presumably blessed):
a field with no explicit type infers its type from its default value, like an
inferred local / param / const: a = 1 -> int, b = 3.14 -> float, c = 'q' -> char,
d = true -> bool. the WIDTH follows the value (9999999999 exceeds int32 -> int64).
a const-expr default folds FIRST, then infers (f = K). inferred and explicitly-typed
fields mix freely. a typeless field with NO default is an error (nothing to infer).
*/

/*
claude says:

a typeless field's type is inferred from its default value. construction is field-
init from a compatible tuple, so an inferred field's type IS its default literal's
preferred type. The slot is a kSlid LAYOUT slot, so inference patches it in a classify
pre-pass (classifyClassSignature) — after constfold folds the default to a literal,
before any method body / construction reads the field type — and re-interns the same
(name+def_id-keyed) handle. An inferred field is always PRIMITIVE (a const-expr
default can't be a class), so the resolve needs-ctor/dtor fixpoint, which ran with a
kNoType slot and contributed nothing, stays correct. Field DEFAULTS are applied at
construction: an omitted field takes its default (def below), a partial init tuple
overrides the lead and defaults the rest (part below).
*/

import string;

const int K = 5;

Class(
    a = 1,            // int
    b = 3.14,         // float
    c = 'q',          // char
    d = true,         // bool
    e = 9999999999,   // int64 (width follows the value)
    f = K,            // int  (const-expr default, folded before inference)
    int g = 7         // explicitly typed, default 7 — mixed with the inferred fields
) {
    void show() {
        // each field read by its BARE name — both as a ##type operand (the self-field
        // rewrite reaches ##type) and as a value.
        println(String + ##type(a) + " " + ##type(b) + " " + ##type(c) + " " + ##type(d)
                  + " " + ##type(e) + " " + ##type(f) + " " + ##type(g));
        println(String + "" + a + " " + b + " " + c + " " + d + " "
                  + e + " " + f + " " + g);
    }

    // bare fields used as EXPRESSIONS — arithmetic, a comparison condition, and
    // mixed with a local.
    int expr() {
        int s = a + g;          // a + g  (int + int)
        if (a < g) {            // bare field in a comparison
            s = s * g;
        }
        return s;
    }

    // spot-check: bare fields across assorted expression positions — as an array
    // index, under address-of (^field) with a write through the reference, in a
    // compound assignment, and bool/char fields in a logical condition.
    int more() {
        int arr[10];
        arr[g] = a * 2;         // field as an array index; field in arithmetic
        int^ p = ^a;            // address-of a bare field
        p^ = p^ + 100;          // write the field through the reference
        f += g;                 // compound assignment on a field
        int sum = arr[g] + a;
        if (d && c == 'q') {    // bool + char fields in a logical condition
            sum = sum + f;
        }
        return sum;
    }
}

/* class-typed fields with CONSTANT defaults — the by-slot recursive fill:
   a scalar default, a tuple default, a HOOKED field class (its ctor runs at
   the fill), and a tuple default on a field class that HAS a matching user
   op= (a field default field-lists; the op= is never consulted). */
Pt(int x_ = 0, int y_ = 0) {
    int psum() { return x_ + y_; }
}
Hooked(int h_ = 0) {
    _() { println(String + "Hooked:ctor: " + h_); }
    ~() { println(String + "Hooked:dtor: " + h_); }
}
Oped(int p_ = 0, int q_ = 0) {
    op=( (int, int)^ t ) {
        p_ = t^[0];
        q_ = t^[1];
        println(String + "Oped:op=: must not run for a field default");
    }
}
Nest(
    Pt a_ = 3,          // scalar constant -> by-slot fill (x_=3, y_ defaults)
    Pt b_ = (4, 5),     // tuple of constants -> by-slot fill
    Hooked c_ = 9,      // hooked field class: fills, then its ctor hook runs
    Oped d_ = (6, 7)    // matching user op= exists; the default still field-lists
) {
    int nsum() { return a_.psum() + b_.psum() + c_.h_ + d_.p_ + d_.q_; }
}

/* the remaining CONSTANT default shapes: a folded negative literal, an enum
   value (folds to its literal), an array field's constant aggregate, a
   nullptr pointer default, and a class field from a NAMED const (folded
   before the check — the fold-then-check order is load-bearing). */
enum int Ez ( ezOne = 1, ezTwo );
Mix(
    int n_ = -3,
    Ez e_ = Ez:ezTwo,
    int a_[2] = (4, 5),
    int^ r_ = nullptr,
    Pt k_ = K,
    (const char)[] i_ = "a",   // a string literal IS a constant (2024-07-24) —
                               // and the SLOT must say so (the strict flow
                               // rule, 2026-07-26: a mutable `char[]` slot
                               // aliasing the read-only pool is a const drop)
    char s_[6] = "hello"       // ...the sized-array spelling COPIES: owned,
                               // writable storage, no const needed
) {
    int msum() { return n_ + e_ + a_[0] + a_[1] + k_.psum(); }
    bool nullr() { return r_ == nullptr; }
    void strs() { println(String + "strs = " + i_ + " " + s_); }
}

/* the fill recurses to ANY depth: a full-tuple default for a field whose
   class itself holds class-typed fields. */
Deep(Nest n_ = ((1, 2), (3, 4), 8, (5, 6))) {
    int dsum() { return n_.nsum(); }
}

int32 main() {

    // every field default-initialized.
    Class def;
    def.show();

    // a partial init tuple overrides the leading fields; the rest take defaults.
    Class part(10, 2.0);
    part.show();

    // fields used in expressions.
    println(String + "def.expr = " + def.expr());     // 56  (1+7=8; 1<7 -> 8*7)
    println(String + "part.expr = " + part.expr());   // 17  (10+7=17; 10<7 false)

    // a wider spot-check of fields-in-expressions (index, ^field, compound assign).
    println(String + "def.more = " + def.more());
    println(String + "part.more = " + part.more());

    // fields read as expressions from OUTSIDE the class (obj.field), incl. a write.
    // (compute into a local — a parenthesized `+` inside a print arg is a separate
    // pre-existing print-concatenation gap, not a field issue.)
    def.a = def.a + 1000;          // obj.field write, reading obj.field
    int outsum = def.a + def.g;    // obj.field + def.g in arithmetic
    println(String + "def.outsum = " + outsum);

    // class-typed fields fill from CONSTANT defaults, by slot recursively; the
    // hooked field's ctor runs at the fill; Oped's op= never prints.
    {
        Nest ns;
        println(String + "nest = " + ns.nsum());
        Nest np((10, 20));
        println(String + "npart = " + np.nsum());
    }

    // the remaining constant shapes: negative literal, enum value, array
    // aggregate, nullptr, class-from-named-const. -3 + 2 + 4 + 5 + 5 = 13.
    Mix mx;
    println(String + "mix = " + mx.msum());
    println(String + "mixr = " + mx.nullr());
    mx.strs();
    Mix mo(, , , , , "zz", "world");
    mo.strs();

    // the by-slot fill recurses through a class-of-classes default.
    // (1+2) + (3+4) + 8 + (5+6) = 29.
    {
        Deep dp;
        println(String + "deep = " + dp.dsum());
    }

    return 0;
}

/* a TRAILING COMMA in the field tuple separates nothing. Rejected at the comma —
   the token to delete. (An empty slot is meaningful when SUPPLYING values, in a
   construction `Pt p(,2)` or a destructure `(x,,z) = t`, but a declaration list has
   no name to omit. And `(int x, ...)` is the incomplete form, which this is a
   half-typed version of.) */
//-EXPECT-ERROR: Expected a field after ','.
//TrailComma(int x, ) { }

/* a LEADING or INTERIOR empty slot already failed on the missing name — only the
   trailing one could slip past the loop's emptiness test. */
//-EXPECT-ERROR: Expected parameter name.
//MidComma(int x, , int y) { }

/* a typeless field with NO default has nothing to infer from. */
//-EXPECT-ERROR: Field 'x' needs an explicit type
//NoType(x) {
//    void p() { println(String + "" + x); }
//}

/* a field default that CONSTRUCTS is code, not data — a default must be a
   foldable constant or an aggregate of foldable constants. */
//-EXPECT-ERROR: is not a constant expression
//BadC(Pt p_ = Pt(7)) { }
//int badc() { BadC b; return b.p_.psum(); }

/* the zero-arg construction spelling is still a construction, not data — an
   ABSENT default already means "default-construct the field". */
//-EXPECT-ERROR: is not a constant expression
//BadZ(Pt p_ = Pt()) { }
//int badz() { BadZ b; return b.p_.psum(); }

/* the check recurses into aggregate slots. */
//-EXPECT-ERROR: is not a constant expression
//BadT((int, Pt) t_ = (1, Pt(2))) { }
//int badt() { BadT b; return b.t_[0]; }

/* a CALL default is code too. */
//-EXPECT-ERROR: is not a constant expression
//int five() { return 5; }
//BadF(int f_ = five()) { }
//int badf() { BadF b; return b.f_; }

/* the rule reaches a template FLAVOR's fields (checked per instance). */
//-EXPECT-ERROR: is not a constant expression
//BadTm<T>(T t_ = 1, Pt p_ = Pt(3)) { }
//int badtm() { BadTm<int> b; return b.t_; }

/* ...a BLOCK-SCOPE class... */
//-EXPECT-ERROR: is not a constant expression
//int badl() {
//    BadL(Pt p_ = Pt(4)) { }
//    BadL b;
//    return b.p_.psum();
//}

/* ...and a field APPENDED by an incomplete class's re-open. */
//-EXPECT-ERROR: is not a constant expression
//Inc(int a_ = 1, ...) { }
//Inc(Pt p_ = Pt(5)) { }
//int badi() { Inc b; return b.a_; }

/* heap allocation in a default is code twice over. */
//-EXPECT-ERROR: is not a constant expression
//BadH(int^ h_ = new int) { }
//int badh() { BadH b; return b.h_^; }

/* (a string-literal default is a CONSTANT — positives in Mix above.) */

/* a TYPELESS field cannot infer from an aggregate (inference is scalar-only
   — its own, earlier diagnostic). */
//-EXPECT-ERROR: must be a constant expression
//BadY(y = (1, 2)) { }
//int bady() { BadY b; return b.y[0]; }
