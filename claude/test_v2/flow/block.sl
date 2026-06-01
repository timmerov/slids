/*
test bare code blocks — a `{ stmts }` statement that opens a nested lexical
scope.

scope: a block-local declaration lives and dies with the block. shadowing an
enclosing name is allowed (the inner masks the outer via innermost-first
lookup; the outer is restored when the block ends).

definite assignment flows THROUGH a block (it is scoped, not isolated): a read
of an outer local inside a block is fine if the outer is initialized, and an
assignment to an outer local inside a block initializes it for after the block.

the unused-local sweep is per-block: a block-local never read errors at block
exit (negatives below).

a trailing block satisfies the must-end-with-return rule if its last statement
returns.
*/

/* a trailing block whose last statement returns satisfies return-correctness. */
int32 trailing_block() {
    {
        return 7;
    }
}

/*
deeply nested blocks: reads of enclosing locals across several levels, a shadow
introduced deep (a fresh `a` at level 3) and written deeper still, and the
outermost `a` left untouched. Exercises innermost-first lookup through many
frames and per-entry alloca naming (two distinct `a` entries must not collide).
*/
int32 deep_nest() {
    int a = 1;
    {
        int b = a + 1;              // 2
        {
            int c = b + 1;          // 3
            {
                int a = 100;        // shadows the outermost a
                int d = c + a;      // 3 + 100 = 103
                __println("d = " + d);
                {
                    int e = d + 1;      // 104
                    __println("e = " + e);
                    a = a + 1;          // writes the shadowing a -> 101
                }
                __println("inner a = " + a);   // 101
            }
            __println("c = " + c);  // 3
        }
        b = b + a;                  // writes outer b: 2 + 1 (outermost a) = 3
        __println("b = " + b);      // 3
    }
    __println("a = " + a);          // 1 — outermost untouched
    return 0;
}

/*
a block-local may shadow a PARAMETER (B-pure): the param is recoverable outside
the block, so the inner masks it only within. (A body-top local reusing a param
name is a duplicate — that negative lives in function/calls.sl.)
*/
int32 param_shadow(int p) {
    {
        int p = 2;
        __println("inner p = " + p);
    }
    __println("outer p = " + p);    // the param, restored: the call arg
    return 0;
}

/* an empty block parses, scopes, and emits nothing. */
int32 empty_block() {
    int a = 1;
    { }
    __println("empty: a = " + a);
    return 0;
}

/* sibling blocks reuse a name — each block-local dies at its block's end, so
   the second `s` is a fresh entry, not a redeclaration. */
int32 sibling_blocks() {
    { int s = 1; __println("s1 = " + s); }
    { int s = 2; __println("s2 = " + s); }
    return 0;
}

/* PPID inside a block: the bump splices WITHIN the block (desugar recurses into
   nested statement lists), not at function scope. */
int32 ppid_in_block() {
    { int k = 0; k++; __println("k = " + k); }
    return 0;
}

/* a block whose only content is a nested block. */
int32 nested_only() {
    { { int q = 7; __println("q = " + q); } }
    return 0;
}

int32 main() {
    int x = 1;

    /* read an outer local inside a block; assign the outer from inside. */
    {
        int y = x + 10;             // read outer x (init flows in)
        __println("y = " + y);
        x = x + 100;                // write outer x (init flows out)
    }
    __println("x = " + x);          // 101 — the block's assignment persisted

    /* shadowing: inner x masks the outer; the outer is restored after. */
    {
        int x = 2;
        __println("inner x = " + x);
    }
    __println("outer x = " + x);    // still 101

    /* a local declared inside a block, used inside it. */
    {
        int z = 5;
        __println("z = " + z);
    }

    __println("trailing = " + trailing_block());

    int dn = deep_nest();
    __println("deep_nest = " + dn);

    int ps = param_shadow(9);
    __println("param_shadow = " + ps);
    int eb = empty_block();
    __println("empty_block = " + eb);
    int sb = sibling_blocks();
    __println("sibling_blocks = " + sb);
    int pk = ppid_in_block();
    __println("ppid_in_block = " + pk);
    int no = nested_only();
    __println("nested_only = " + no);

    return 0;
}

/*
negatives — one //-block uncommented per run.
*/

/* a block-local declared, written, never read -> unused at block exit. */
//-EXPECT-ERROR: Local variable 'u' set but never used
//int32 neg_block_unused() {
//    {
//        int u = 5;
//    }
//    return 0;
//}

/* a block-local declared, never written, never read -> unused at block exit. */
//-EXPECT-ERROR: Unused local variable 'u'
//int32 neg_block_unused_noinit() {
//    {
//        int u;
//    }
//    return 0;
//}

/* use-before-init inside a block. */
//-EXPECT-ERROR: Use of uninitialized variable 'u'
//int32 neg_block_uninit() {
//    {
//        int u;
//        int v = u;
//        __println("v = " + v);
//    }
//    return 0;
//}

/* a block-local does not escape its scope — read after the block is undeclared. */
//-EXPECT-ERROR: Unresolved identifier 'z'
//int32 neg_block_escape() {
//    {
//        int z = 5;
//        __println("z = " + z);
//    }
//    int w = z;
//    __println("w = " + w);
//    return 0;
//}
