/*
test inferring types.

includes: primitive types, enums, constants,
(and when they land)
iterators, references, classes, tuples, functions, etc.

*/

alias Integer = int;

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
