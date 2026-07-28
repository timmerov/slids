/*
test inferring types.

includes: primitive types, enums, constants,
iterators, references, classes, tuples, functions, etc.

infer const types and reference types.

    int array[3] = (1,2,3);
    int y = array[0];

    const x = y;            // const int
    ref^ = ^array[x];       // int^
    const ref^ = ^array[y]; // const int^
*/

/*
claude says:

a typeless decl `x = rhs` infers x's type from the rhs (classify), then writes
through. the inferred type IS the rhs's: an int literal -> int (the PREFERRED
spelling, not int32), float -> float; a typed value keeps its spelling (int64);
an expression takes its result type; an alias LABEL rides (Integer f, Integer fe).

covered here:
  - the literal-kind matrix / default tiers: int, float, 0xFF -> uint, 'Q' -> char,
    true -> bool, 5000000000 -> int64, the uint64-by-magnitude top tier, and the
    negative branch (-5 -> int, -3000000000 -> int64).
  - from a typed var (int64), an expression (int64), an alias var / expr (label
    rides); reuse (typeless assign to an in-scope var — same scope, across a block,
    and a block-local inferred decl); aug-assign on an inferred var; inferred feeds
    inference (n1 -> n2).
  - from a CONSTANT — an int const -> int, and a TYPED const keeps its width across
    families (`int8` -> int8 cb, `uint8` -> uint8 cu, `int16` -> int16 cs, `float64`
    -> float64 cd): the substituted literal carries the const's strong_type, and the
    typeless-decl inference uses it (classify.cpp).
  - from an ENUM member (Color:kGreen -> the enum type Color, value 1, label rides).
  - CONST inference: `const x = rhs` — a foldable rhs substitutes, a runtime rhs
    becomes a runtime deep-const LOCAL (classify flips the entry) — and BOTH spell
    const in ##type, as does a file-scope constant (kFive -> const int). Runtime
    const arrays / tuples (`const arr2 = arr1`, `const tuple1 = (4,5,6)`).
    A BARE inferred copy of a constant stays MUTABLE (ci/cb/... above are int/int8,
    not const) — const rides only when the decl spells it, or through an address.
  - REFERENCE inference: bare from an array-element address -> the ITERATOR
    (`p = ^array[2]` -> int[]); bare from a scalar address -> the reference
    (`q = ^y` -> int^); the infer-as-reference spelling `ref^ = addr` -> int^
    (an iterator rhs demotes to the element reference; writes flow back);
    `const cref^ = addr` -> deep const, spelled like the explicit `const int^`
    twin (`const (const int)^`).
  - negatives: a self-referential init (`h = h`, uninitialized); a type/namespace as
    a value (`x = Color`, "not a value"); a no-common-type rhs (`x = uint64 + int8`);
    `ref^ = value` (not an address); a write to a runtime const; a write through a
    const-inferred reference.

deferred (not landed): inferring from classes / functions.
*/

import string;

alias Integer = int;
const int kFive = 5;
const int8 kByte = 9;
const uint8 kU8 = 200;
const int16 kS16 = 300;
const float64 kD = 2.5;
enum Color ( kRed, kGreen, kBlue );

int32 main() {

    /* inferred from an int literal -> int (the preferred spelling, not int32). */
    a = 42;
    __println(##type(a) + " a = " + a);

    /* inferred from a float literal -> float. */
    b = 3.5;
    __println(##type(b) + " b = " + b);

    /* inferred from a typed variable -> that variable's type, spelling kept. */
    int64 big = 7;
    c = big;
    __println(##type(c) + " c = " + c);

    /* inferred from an expression -> the expression's type. */
    d = big + 1;
    __println(##type(d) + " d = " + d);

    /* inferred from an alias-typed value -> the alias label rides along. */
    Integer e = 5;
    f = e;
    __println(##type(f) + " f = " + f);

    /* reuse: g is already in scope, so a typeless assign reassigns it. */
    int g = 1;
    g = 2;
    __println(##type(g) + " g = " + g);

    /* aug-assign on an inferred var (lvalue type re-read at classify). */
    aug = 10;
    aug += 5;
    __println(##type(aug) + " aug = " + aug);

    /* the literal-kind matrix: uint / char / bool / int64-by-magnitude. */
    u = 0xFF;
    __println(##type(u) + " u = " + u);
    ch = 'Q';
    __println(##type(ch) + " ch = " + ch);
    bo = true;
    __println(##type(bo) + " bo = " + bo);
    big2 = 5000000000;
    __println(##type(big2) + " big2 = " + big2);

    /* an inferred var feeds another inference (write-back then read). */
    n1 = 42;
    n2 = n1;
    __println(##type(n2) + " n2 = " + n2);

    /* an alias rides through an inferred EXPRESSION (e is Integer). */
    fe = e + 1;
    __println(##type(fe) + " fe = " + fe);

    /* inferred from a CONSTANT -> the constant's type (a typed const keeps width). */
    ci = kFive;
    __println(##type(ci) + " ci = " + ci);
    cb = kByte;
    __println(##type(cb) + " cb = " + cb);

    /* the typed-const width-keeping rides across families, not just int8. */
    cu = kU8;
    __println(##type(cu) + " cu = " + cu);
    cs = kS16;
    __println(##type(cs) + " cs = " + cs);
    cd = kD;
    __println(##type(cd) + " cd = " + cd);

    /* literal default tiers: uint64-by-magnitude + negative literals. */
    ubig = 18446744073709551615;
    __println(##type(ubig) + " ubig = " + ubig);
    neg = -5;
    __println(##type(neg) + " neg = " + neg);
    negbig = -3000000000;
    __println(##type(negbig) + " negbig = " + negbig);

    /* inferred from an ENUM member -> the enum type (label rides). */
    ec = Color:kGreen;
    __println(##type(ec) + " ec = " + ec);

    /* reuse-across-scope: a typeless assign inside a block reuses the outer
       var and writes through, so it holds 2 after the block. */
    int outer = 1;
    {
        outer = 2;
    }
    __println(##type(outer) + " outer = " + outer);

    /* an inferred var declared inside a block. */
    {
        blk = 7;
        __println(##type(blk) + " blk = " + blk);
    }

    /* infer runtime const types for arrays tuples pointers. */
    {
        int arr1[3] = (1,2,3);
        const arr2 = arr1;
        __println(##type(arr2) + " arr2 = [" + arr2[0] + "," + arr2[1] + "," + arr2[2] + "]");

        const tuple1 = (4,5,6);
        __println(##type(tuple1) + " tuple1 = (" + tuple1[0] + "," + tuple1[1] + "," + tuple1[2] + ")");

        const tuple2 = tuple1;
        __println(##type(tuple2) + " tuple2 = (" + tuple2[0] + "," + tuple2[1] + "," + tuple2[2] + ")");
    }

    /* infer const SCALARS: a foldable init substitutes, a runtime init becomes a
       runtime const local — both spell const (no seam between them). */
    {
        const cf = 6;
        __println(##type(cf) + " cf = " + cf);
        int rt = 7;
        const cr = rt;
        __println(##type(cr) + " cr = " + cr);
    }

    /* ##type of a file-scope constant is const-qualified too. */
    __println(##type(kFive) + " kFive = " + kFive);

    /* infer references and iterators from addresses (canon head examples). */
    {
        int array[3] = (1,2,3);
        int x = 0;
        int y = 1;

        /* bare inference from an array-element address -> the ITERATOR int[]. */
        p = ^array[2];
        __println(##type(p) + " p^ = " + p^);

        /* bare inference from a scalar address -> the reference int^. */
        q = ^y;
        __println(##type(q) + " q^ = " + q^);

        /* infer-as-reference: `ref^ =` binds a reference; writes flow back. */
        ref^ = ^array[x];
        ref^ = 55;
        __println(##type(ref) + " array[0] = " + array[0]);

        /* a reference-typed rhs: the fresh reference aliases the same target. */
        ref2^ = q;
        ref2^ = 8;
        __println(##type(ref2) + " y = " + y);

        /* const + infer-as-reference -> deep const (the explicit `const int^`
           twin's spelling); reading is fine, writing is a negative below. */
        const cref^ = ^array[2];
        __println(##type(cref) + " cref^ = " + cref^);
    }

    return 0;
}

/*
negatives — one //-block uncommented per run.
*/

/* self-reference: the inferred decl is created, then its own rhs reads it
   before it is initialized. */
//-EXPECT-ERROR: Use of uninitialized variable 'h'
//int32 neg_self() {
//    h = h;
//    return 0;
//}

/* an enum / type name is not a value — there's no type to infer from it. */
//-EXPECT-ERROR: 'Color' is a namespace, not a value
//int32 neg_type() {
//    x = Color;
//    return 0;
//}

/* a no-common-type expression has no type, so nothing to infer. (x is read so the
   unused-sweep does not fire first and short-circuit before classify.) */
//-EXPECT-ERROR: No common type for 'uint64' and 'int8'
//int32 neg_nct() {
//    uint64 p = 1;
//    int8 q = 2;
//    x = p + q;
//    __println("x= " + x);
//    return 0;
//}

/* `ref^ =` infers a reference, so the rhs must be an address; a plain value
   has none. */
//-EXPECT-ERROR: is not an address
//int32 neg_ref_from_value() {
//    int y = 5;
//    ref^ = y;
//    __println("" + ref^);
//    return 0;
//}

/* a runtime-inferred const is still a constant — writes reject. */
//-EXPECT-ERROR: Cannot assign to constant
//int32 neg_runtime_const_write() {
//    int y = 5;
//    const x = y;
//    x = 6;
//    __println("" + x);
//    return 0;
//}

/* writing through a const-inferred reference. */
//-EXPECT-ERROR: Cannot write to a const value
//int32 neg_const_ref_write() {
//    int array[3] = (1,2,3);
//    const cref^ = ^array[0];
//    cref^ = 9;
//    __println("" + array[0]);
//    return 0;
//}
