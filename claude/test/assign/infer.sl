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

also see the convenience convention in typeconv.sl.
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
    println(String + ##type(a) + " a = " + a);

    /* inferred from a float literal -> float. */
    b = 3.5;
    println(String + ##type(b) + " b = " + b);

    /* inferred from a typed variable -> that variable's type, spelling kept. */
    int64 big = 7;
    c = big;
    println(String + ##type(c) + " c = " + c);

    /* inferred from an expression -> the expression's type. */
    d = big + 1;
    println(String + ##type(d) + " d = " + d);

    /* inferred from an alias-typed value -> the alias label rides along. */
    Integer e = 5;
    f = e;
    println(String + ##type(f) + " f = " + f);

    /* reuse: g is already in scope, so a typeless assign reassigns it. */
    int g = 1;
    g = 2;
    println(String + ##type(g) + " g = " + g);

    /* aug-assign on an inferred var (lvalue type re-read at classify). */
    aug = 10;
    aug += 5;
    println(String + ##type(aug) + " aug = " + aug);

    /* the literal-kind matrix: uint / char / bool / int64-by-magnitude. */
    u = 0xFF;
    println(String + ##type(u) + " u = " + u);
    ch = 'Q';
    println(String + ##type(ch) + " ch = " + ch);
    bo = true;
    println(String + ##type(bo) + " bo = " + bo);
    big2 = 5000000000;
    println(String + ##type(big2) + " big2 = " + big2);

    /* an inferred var feeds another inference (write-back then read). */
    n1 = 42;
    n2 = n1;
    println(String + ##type(n2) + " n2 = " + n2);

    /* an alias rides through an inferred EXPRESSION (e is Integer). */
    fe = e + 1;
    println(String + ##type(fe) + " fe = " + fe);

    /* inferred from a CONSTANT -> the constant's type (a typed const keeps width). */
    ci = kFive;
    println(String + ##type(ci) + " ci = " + ci);
    cb = kByte;
    println(String + ##type(cb) + " cb = " + cb);

    /* the typed-const width-keeping rides across families, not just int8. */
    cu = kU8;
    println(String + ##type(cu) + " cu = " + cu);
    cs = kS16;
    println(String + ##type(cs) + " cs = " + cs);
    cd = kD;
    println(String + ##type(cd) + " cd = " + cd);

    /* literal default tiers: uint64-by-magnitude + negative literals. */
    ubig = 18446744073709551615;
    println(String + ##type(ubig) + " ubig = " + ubig);
    neg = -5;
    println(String + ##type(neg) + " neg = " + neg);
    negbig = -3000000000;
    println(String + ##type(negbig) + " negbig = " + negbig);

    /* inferred from an ENUM member -> the enum type (label rides). */
    ec = Color:kGreen;
    println(String + ##type(ec) + " ec = " + ec);

    /* reuse-across-scope: a typeless assign inside a block reuses the outer
       var and writes through, so it holds 2 after the block. */
    int outer = 1;
    {
        outer = 2;
    }
    println(String + ##type(outer) + " outer = " + outer);

    /* an inferred var declared inside a block. */
    {
        blk = 7;
        println(String + ##type(blk) + " blk = " + blk);
    }

    /* infer runtime const types for arrays tuples pointers. */
    {
        int arr1[3] = (1,2,3);
        const arr2 = arr1;
        println(String + ##type(arr2) + " arr2 = [" + arr2[0] + "," + arr2[1] + "," + arr2[2] + "]");

        const tuple1 = (4,5,6);
        println(String + ##type(tuple1) + " tuple1 = (" + tuple1[0] + "," + tuple1[1] + "," + tuple1[2] + ")");

        const tuple2 = tuple1;
        println(String + ##type(tuple2) + " tuple2 = (" + tuple2[0] + "," + tuple2[1] + "," + tuple2[2] + ")");
    }

    /* infer const SCALARS: a foldable init substitutes, a runtime init becomes a
       runtime const local — both spell const (no seam between them). */
    {
        const cf = 6;
        println(String + ##type(cf) + " cf = " + cf);
        int rt = 7;
        const cr = rt;
        println(String + ##type(cr) + " cr = " + cr);
    }

    /* ##type of a file-scope constant is const-qualified too. */
    println(String + ##type(kFive) + " kFive = " + kFive);

    /* infer references and iterators from addresses (canon head examples). */
    {
        int array[3] = (1,2,3);
        int x = 0;
        int y = 1;

        /* bare inference from an array-element address -> the ITERATOR int[]. */
        p = ^array[2];
        println(String + ##type(p) + " p^ = " + p^);

        /* bare inference from a scalar address -> the reference int^. */
        q = ^y;
        println(String + ##type(q) + " q^ = " + q^);

        /* infer-as-reference: `ref^ =` binds a reference; writes flow back. */
        ref^ = ^array[x];
        ref^ = 55;
        println(String + ##type(ref) + " array[0] = " + array[0]);

        /* a reference-typed rhs: the fresh reference aliases the same target. */
        ref2^ = q;
        ref2^ = 8;
        println(String + ##type(ref2) + " y = " + y);

        /* const + infer-as-reference -> deep const (the explicit `const int^`
           twin's spelling); reading is fine, writing is a negative below. */
        const cref^ = ^array[2];
        println(String + ##type(cref) + " cref^ = " + cref^);
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
//    println(String + "x= " + x);
//    return 0;
//}

/* `ref^ =` infers a reference, so the rhs must be an address; a plain value
   has none. */
//-EXPECT-ERROR: is not an address
//int32 neg_ref_from_value() {
//    int y = 5;
//    ref^ = y;
//    println(String + "" + ref^);
//    return 0;
//}

/* a runtime-inferred const is still a constant — writes reject. */
//-EXPECT-ERROR: Cannot assign to constant
//int32 neg_runtime_const_write() {
//    int y = 5;
//    const x = y;
//    x = 6;
//    println(String + "" + x);
//    return 0;
//}

/* writing through a const-inferred reference. */
//-EXPECT-ERROR: Cannot write to a const value
//int32 neg_const_ref_write() {
//    int array[3] = (1,2,3);
//    const cref^ = ^array[0];
//    cref^ = 9;
//    println(String + "" + array[0]);
//    return 0;
//}
