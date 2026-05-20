/*
test initialization of objects by tuple.

this:
SomeType x = (list, of, (initial, values));
and this:
SomeType x(list, of, (initial, values));
should do the exact same thing.
and ideally, follow the same code path.

this:
x = (list, of, (initial, values));
should be similar.
the difference is missing elements
are skipped instead of given default
values.

tuples are flat in memory.
they have organizational structure.
*/

Inner(int x_ = 1, int y_ = 2) {
    void print() {
        __print("(" + x_ + "," + y_ + ")");
    }
}

Outer(int a_ = 3, Inner inj_, Inner ink_, int b_) {
    void println(char[] what) {
        __print(what + "=( " + a_ + " ");
        inj_.print();
        __print(", ");
        ink_.print();
        __println(", " + b_ + ")");
    }
}

/* tuple-returning function — slot types are tuple-shape (no slids), so the
   call-RHS test stays inside scope (slid-RHS is out of scope). Slot names
   are required by the tuple-return signature grammar. */
(int a, (int,int) i1, (int,int) i2, int b) make_outer_tuple() {
    return (61, (62,63), (64,65), 66);
}

int32 main() {
    /* equivalence. */
    Outer otn = (11, (12,13), (14,15), 16);
    Outer otm(21, (22,23), (24,25), 26);
    otn.println("otn");
    otm.println("otm");

    /* assignment. */
    Outer otp;
    otp = (31, (32,33), (34,35), 36);
    otp.println("otp");

    /* missing optional parameters. */
    Outer otq = (41, 42, 43);
    /* expected: otq=( 41 (42,2), (43,2), 0) — Inner default y_=2; b_ zero */
    otq.println("otq");

    /* slid LHS — tuple variable RHS. Same desugar path as literal init. */
    otpl = (51, (52,53), (54,55), 56);
    Outer otv = otpl;
    /* expected: otv=( 51 (52,53), (54,55), 56) */
    otv.println("otv");

    /* slid LHS — tuple-returning call RHS. */
    Outer ofn = make_outer_tuple();
    /* expected: ofn=( 61 (62,63), (64,65), 66) */
    ofn.println("ofn");

    /* slid LHS — assignment with missing elements: tail untouched. */
    Outer otA = (71, (72,73), (74,75), 76);
    otA = (80);
    /* expected: otA=( 80 (72,73), (74,75), 76) — only a_ updated */
    otA.println("otA");

    /* tuple LHS variants. */
    (int, int) pair_lit = (1, 2);
    __println("pair_lit=(" + pair_lit[0] + "," + pair_lit[1] + ")");

    (int, (int,int), int) mixed = (5, (6,7), 8);
    __println("mixed=(" + mixed[0] + ",(" + mixed[1][0] + "," + mixed[1][1] + ")," + mixed[2] + ")");

    /* tuple LHS — missing tail, decl form: tuple/array missing → 0. */
    (int, int, int) p3_short = (1, 2);
    /* expected: p3_short=(1,2,0) */
    __println("p3_short=(" + p3_short[0] + "," + p3_short[1] + "," + p3_short[2] + ")");

    /* tuple LHS — assignment with missing tail: untouched. */
    (int, int) pair_assign = (10, 20);
    pair_assign = (99);
    /* expected: pair_assign=(99,20) */
    __println("pair_assign=(" + pair_assign[0] + "," + pair_assign[1] + ")");

    /* tuple LHS — tuple variable RHS. */
    (int, int) pair_var_rhs = pair_assign;
    /* expected: pair_var_rhs=(99,20) */
    __println("pair_var_rhs=(" + pair_var_rhs[0] + "," + pair_var_rhs[1] + ")");

    /* array LHS — full init. */
    int arr_full[3] = (1, 2, 3);
    __println("arr_full=(" + arr_full[0] + "," + arr_full[1] + "," + arr_full[2] + ")");

    /* array LHS — missing tail, decl: 0. */
    int arr_short[3] = (1, 2);
    /* expected: arr_short=(1,2,0) */
    __println("arr_short=(" + arr_short[0] + "," + arr_short[1] + "," + arr_short[2] + ")");

    /* array of slid LHS, init from tuple-of-tuples. */
    Inner iarr[2] = ((10, 20), (30, 40));
    __print("iarr[0]=");
    iarr[0].print();
    __print(" iarr[1]=");
    iarr[1].print();
    __println("");
    /* expected: iarr[0]=(10,20) iarr[1]=(30,40) */

    /* array LHS — homogeneous tuple variable RHS. */
    ints_var = (100, 200, 300);
    int arr_var[3] = ints_var;
    /* expected: arr_var=(100,200,300) */
    __println("arr_var=(" + arr_var[0] + "," + arr_var[1] + "," + arr_var[2] + ")");

    /* single-value promotion: rhs `5` becomes (5,) before desugar. */
    Inner i_promo = 5;
    __print("i_promo=");
    i_promo.print();
    __println("");
    /* expected: i_promo=(5,2) — Inner.x_=5; y_ from default */

    (int, int) p_promo = 7;
    /* expected: p_promo=(7,0) */
    __println("p_promo=(" + p_promo[0] + "," + p_promo[1] + ")");

    /* per-element promotion in an array of slids. */
    Inner iarr_promo[2] = (5, 6);
    __print("iarr_promo[0]=");
    iarr_promo[0].print();
    __print(" iarr_promo[1]=");
    iarr_promo[1].print();
    __println("");
    /* expected: iarr_promo[0]=(5,2) iarr_promo[1]=(6,2) */

    //-EXPECT-ERROR: Too many tuple values for 'Outer'
    //Outer xtoo = (1, (2,3), (4,5), 6, 7);

    //-EXPECT-ERROR: Cannot implicitly convert 'char[]' to 'int'
    //Outer xstr = (1, "str", (4,5), 6);

    //-EXPECT-ERROR: Cannot implicitly convert 'char[]' to 'int'
    //int arr_bad[2] = (1, "str");

    //-EXPECT-ERROR: Too many values
    //(int, int) pair_too = (1, 2, 3);

    return 0;
}
