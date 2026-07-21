/*
test variable declarations + definite assignment + unused locals.

definite assignment: a local must be written before it is read. reading an
uninitialized local is a compile error ("Use of uninitialized variable 'x'.").
a decl-with-initializer and a plain assignment both count as the write; params
arrive initialized.

unused locals: a body-declared local that is never read is a compile error —
"Unused local variable 'x'." if it was never written, "Local variable 'x' set
but never used." if it was written. (A use-before-init error suppresses the
unused report on the same code, so the two never double up.)
*/

/*
claude says:

two checks, both in RESOLVE: definite assignment (a local must be written before
read; decl-init and plain assignment both count, params arrive initialized) and
the unused-local sweep (a body-declared local never read errors — "Unused local
variable" if never written, "set but never used" if written). A use-before-init
on the same code gates the sweep (resolve sets hasErrors), so the two never
double-report. Consts and params are exempt from the sweep.

covered here:
  - definite-assignment positives: typed init+read, declare-then-assign,
    reassignment, aug-assign (reads then writes), init from a call result.
  - "Use of uninitialized" negatives across every READ context: bare rhs, a
    larger expression, self-referential init, aug-assign / ++ target, call arg,
    return, print; plus per-function isolation (one function's init does not seed
    another's same-named local).
  - unused-local negatives: never-written vs written ("set but never used"), and
    the use-before-init-suppresses-unused composition (exactly one diagnostic).
  - param + const exemptions from the unused sweep.
  - FLOW-SENSITIVE definite assignment: the if/else join is a "must" intersection
    (intersectInit) and a while body may run zero times — so if/else-both-init
    reads cleanly (positive), while an if-with-no-else and a while-only write each
    leave the local uninitialized (negatives).
  - alternate WRITE-FORMS count as the initializing write: a move (`u <-- x`) and
    a destructure (`(a, b) = tuple`); a move also READS its source, so moving from
    an uninitialized local is a use-before-init (negative).
  - (assignment relation, class row — not definite assignment) a class VALUE
    converts only to the same class: assigning one to a primitive or an unrelated
    class errors at BOTH a decl and a bare assignment, via checkValueAssign.
  - CONSTRUCTION-STYLE scalar init `Type name(value)` for a NUMBER / POINTER target
    (== `Type name = value`): the size-1 construction tuple collapses to its element
    in checkValueAssign; a 0- or 2+-element tuple stays a "Cannot assign" mismatch.

not here: array/aggregate definite-assignment (an element store marking the whole
array initialized — pointer/array.sl). NOTE: a constructed-but-unread CLASS local
is NOT swept ("set but never used" doesn't fire) — exempt as an RAII guard; that
policy is untested-by-design and unspec'd.
*/

/*
params arrive initialized; reading them is fine. an unread param (unused) is
NOT flagged — only body-declared locals are swept, params are exempt.
*/
int32 add(int a, int b, int unused_param) {
    int s = a + b;
    return s;
}

/*
per-function isolation: this function's locals are named x and y, the same as
main's. Each function's initialized-set is independent, so these resolve and
read cleanly regardless of main. (The leak direction — one function's init
bleeding into another's same-named local — is the neg_isolation negative below.)
*/
int32 iso() {
    int x = 7;
    int y = x + 1;
    return x + y;
}

int32 main() {
    /* typed init, then read. */
    int x = 5;
    __println("x = " + x);

    /* declare-then-assign: y is written before it is read. */
    int y;
    y = x + 1;
    __println("y = " + y);

    /* read both initialized locals. */
    int z = x + y;
    __println("z = " + z);

    /* reassignment of an already-initialized local. */
    z = z + 100;
    __println("z = " + z);

    /* aug-assign reads then writes an initialized local. */
    z += 1;
    __println("z = " + z);

    /* an unread const is exempt from the unused-local sweep (consts only). */
    const int kUnreadConst = 99;

    /* a local initialized from a call result. */
    int q = add(x, y, 0);
    __println("q = " + q);

    /* a sibling function whose locals share main's names resolves cleanly. */
    int i = iso();
    __println("i = " + i);

    /* flow-sensitive: both branches initialize, so the post-merge read is ok. */
    int fa;
    if (x > 0) { fa = 10; } else { fa = 20; }
    __println("fa = " + fa);                    // 10

    /* a move counts as the initializing write of its target. */
    int mv;
    mv <-- x;
    __println("mv = " + mv);                    // 5

    /* a destructure counts as the initializing write for each target. */
    int da;
    int db;
    (da, db) = (30, 40);
    __println("da = " + da + " db = " + db);    // 30 40

    /* a const-EXPRESSION dim in a VAR-DECL's type (a tuple slot): folded + baked
       in constfold, so the slot array is sized and indexable. */
    const int kCols = 3;
    (int[kCols], int) vd = ((7, 8, 9), 10);
    __println("vd = " + vd[0][2] + " " + vd[1] + " (" + ##type(vd) + ")"); // 9 10 ((int[3], int))

    /* PER-ELEMENT IMPLICIT WIDENING on aggregate-value assignment. classify
       validates SHAPE match (dims/arity) at every composite level; codegen walks
       each leaf with widen::convert (the implicit widen — rejects narrowing /
       cross-family / sign-change). The walk composes through nested tuples and
       arrays. */

    /* int8[N] -> int[N]: element-by-element sign-extend. */
    int8 s8[3] = (1, 2, 3);
    int i32a[3] = s8;
    __println("i32a = " + i32a[0] + " " + i32a[1] + " " + i32a[2]);   // 1 2 3

    /* same shape, assign-form (not just init). */
    int i32b[3] = (0, 0, 0);
    i32b = s8;
    __println("i32b = " + i32b[0] + " " + i32b[1] + " " + i32b[2]);   // 1 2 3

    /* (int8,int8) -> (int,int): per-slot sign-extend. */
    (int8, int8) ts = (10, 20);
    (int, int) ti = ts;
    __println("ti = " + ti[0] + " " + ti[1]);                         // 10 20

    /* uint8[N] -> int[N]: zero-extend. */
    uint8 u8[3] = (200, 201, 202);
    int wu[3] = u8;
    __println("wu = " + wu[0] + " " + wu[1] + " " + wu[2]);           // 200 201 202

    /* nested: tuple-of-array — recurse into slot 0 (array), widen each elem. */
    (int8[3], int8) ns = ((1, 2, 3), 4);
    (int[3], int) ni = ns;
    __println("ni = " + ni[0][0] + " " + ni[0][2] + " " + ni[1]);     // 1 3 4

    /* multi-dim array: dims match, leaf widens. */
    int8 m8[2][3] = ((1, 2, 3), (4, 5, 6));
    int m32[2][3] = m8;
    __println("m32 = " + m32[0][0] + " " + m32[1][2]);                // 1 6

    /* tuple-of-tuple. */
    ((int8, int8), (int8, int8)) nts = ((1, 2), (3, 4));
    ((int, int), (int, int)) nti = nts;
    __println("nti = " + nti[0][0] + " " + nti[0][1]
              + " " + nti[1][0] + " " + nti[1][1]);                   // 1 2 3 4

    /* int8 -> int64: a wider widening that still rides the leaf grid. */
    int8 ws[3] = (-1, 0, 1);
    int64 wd[3] = ws;
    __println("wd = " + wd[0] + " " + wd[1] + " " + wd[2]);           // -1 0 1

    /* SUB-ARRAY STORE through kStoreStmt: `matrix[i] = sub_aggregate`. The
       lvalue is a partial index; the slice's elem type may differ from the
       source. Widens per leaf via the codegen kStoreStmt arm. */
    int matrix[3][2];
    int8 row8[2] = (7, 8);
    matrix[0] = row8;
    matrix[1] = (9, 10);
    matrix[2] = row8;
    __println("matrix = " + matrix[0][0] + " " + matrix[0][1] + " "
              + matrix[1][0] + " " + matrix[2][1]);                   // 7 8 9 8

    /* DEREF STORE through kStoreStmt: `ref^ = aggregate`. The dst is the
       pointee's storage; same widen dispatch. */
    int dst_arr[3] = (0, 0, 0);
    int[3]^ pdst = ^dst_arr;
    int8 dsrc[3] = (4, 5, 6);
    pdst^ = dsrc;
    __println("pdst^ = " + dst_arr[0] + " " + dst_arr[1] + " " + dst_arr[2]); // 4 5 6

    /* MOVE-INIT with aggregate widen: `dst <-- src` runs the same widen walk
       then null-leaves the source (no pointer leaves here, so a no-op tail). */
    int8 msrc[3] = (10, 20, 30);
    int mdst[3];
    mdst <-- msrc;
    __println("mdst = " + mdst[0] + " " + mdst[1] + " " + mdst[2]);   // 10 20 30

    /* RETURN value with aggregate widen: function returns int[3] but builds
       the value via an int8 local; the kReturnStmt arm walks per leaf. */
    int rret[3] = returns_widened();
    __println("rret = " + rret[0] + " " + rret[1] + " " + rret[2]);   // 11 12 13

    /* CALL SITE aggregate widen: pass an int8[3] arg to a function taking
       int[3]^. classify scores it cost 1 (shape match + per-leaf widen);
       codegen materializes a converted int[3] temp and passes its address. */
    int8 carg[3] = (21, 22, 23);
    int csum = sum_int3(carg);
    __println("csum = " + csum);                                       // 66

    /* TYPED-no-name destructure discard: a typed slot with NO name drops its tuple
       position — a documented discard, same as an empty `,` slot. It still counts
       toward arity, so the 3-slot pattern matches a 3-tuple (the middle is dropped). */
    (int td, int, int te) = (1, 2, 3);
    __println("td = " + td + " te = " + te);                           // 1 3

    /* COMPOSED nameless slot types: the wrapper chain spells a multi-modifier slot
       (ref-to-array, array-of-refs) inside a tuple TYPE — unspellable before the
       declarator unification (the single-suffix slot parser gave "Expected ')'"). */
    int sra_a[3] = (1, 2, 3);
    (int[3]^, int) sra = (^sra_a, 99);          // slot 0: reference TO an int[3]
    __println("sra = " + sra[0]^[2] + " " + sra[1]);                   // 3 99

    int sar_a = 5;
    int sar_b = 6;
    (int^[2], int) sar = ((^sar_a, ^sar_b), 77);   // slot 0: array OF 2 references
    __println("sar = " + sar[0][1]^ + " " + sar[1]);                   // 6 77

    /* redundant / nested type grouping collapses to the inner type at any depth —
       each `(T)` is a size-1 tuple (grouping; the comma is the tuple marker, there is
       no 1-tuple), so the parens evaporate unless one scopes a qualifier. Pins the
       collapse (a deferred item is whether to warn on this pointless grouping). */
    ((((int)))) grp = 5;
    int grp1 = grp + 1;
    __println("grp = " + grp1 + " (" + ##type(grp) + ")");             // 6 (int)
    (((int[3]))) garr = (7, 8, 9);
    __println("garr = " + garr[2] + " (" + ##type(garr) + ")");        // 9 (int[3])

    /* CONSTRUCTION-STYLE scalar init `Type name(value)` — for a NUMBER or POINTER
       target it equals `Type name = value`. The `(args)` form builds an explicit
       construction tuple; a size-1 tuple collapses to its element (the node-level
       1-tuple==scalar collapse — the `=`-grouping form gets it at parse). */
    int cn(42);
    __println("cn = " + cn);                                           // 42
    int64 cn64(7);
    __println("cn64 = " + cn64);                                       // 7
    float32 cf(1.5);
    __println("cf = " + cf);                                           // 1.5
    bool cb(true);
    __println("cb = " + cb);                                           // true
    char cc(65);
    __println("cc = " + cc);                                           // A
    int cpv = 9;
    int^ cpp(^cpv);                             // pointer target: element is a ref
    __println("cpp = " + cpp^);                                        // 9

    /* A CHAR ARRAY FROM A STRING LITERAL. A string literal is `const char[N]` — storage,
       N counting the terminating NUL — so this is an ordinary same-type array init, not
       a special initializer rule. The size must match EXACTLY: an array that is too
       short would drop the NUL (C allows that; it is how an unterminated buffer gets
       made) and one that is too long has no defined fill, so both are rejected — see
       the negatives below. */
    char greet[6] = "hello";              // 5 chars + NUL
    __println("greet = " + greet[0] + greet[1] + greet[2] + greet[3] + greet[4]);
    __println("greet nul = " + (greet[5] == '\0'));                    // true
    __println("greet ty = " + ##type(greet));                          // char[6]

    /* the array is ordinary storage, so it is MUTABLE — the literal it was built from
       is not, and this writes the copy. */
    greet[0] = 'H';
    __println("greet2 = " + greet[0] + greet[1]);                      // He

    /* the empty literal is not empty: it is the NUL alone. */
    char none[1] = "";
    __println("none = " + (none[0] == '\0') + " " + ##type(none));     // true char[1]

    /* the ELEMENT type need not be char — char is uint8, so a literal rides the ordinary
       per-element widen above, exactly like `uint8[3] -> int[3]`. Only the SIZE is fixed. */
    int codes[6] = "hello";
    __println("codes = " + codes[0] + " " + codes[4] + " " + codes[5]);  // 104 111 0

    {
        /* trivial statement uses x. */
        int x = 42; x;
    }

    return 0;
}

/* Returns int[3] but the body works with an int8 local. The kReturnStmt arm
   per-leaf widens to int[3] before `ret`. */
int[3] returns_widened() {
    int8 rs[3] = (11, 12, 13);
    return rs;
}

/* Takes int[3]^ by pointer. A caller passing an int8[3] triggers the
   classify-side shape-match cost-1 ranking + the codegen materialize-with-
   convert at the call site. */
int sum_int3(int[3]^ a) {
    return a^[0] + a^[1] + a^[2];
}

/*
negatives — definite-assignment violations (one //-block uncommented per run).
*/

/* read before any write. */
//-EXPECT-ERROR: Use of uninitialized variable 'u'
//int32 neg_read() {
//    int u;
//    int v = u;
//    return 0;
//}

/* a self-referential initializer reads the local being declared. */
//-EXPECT-ERROR: Use of uninitialized variable 'u'
//int32 neg_self() {
//    int u = u;
//    return 0;
//}

/* an uninitialized local read on the rhs of an assignment. */
//-EXPECT-ERROR: Use of uninitialized variable 'u'
//int32 neg_rhs() {
//    int u;
//    int w = 0;
//    w = u;
//    return 0;
//}

/* an uninitialized local read inside a larger expression (not a bare rhs). */
//-EXPECT-ERROR: Use of uninitialized variable 'u'
//int32 neg_expr() {
//    int u;
//    int v = u + 1;
//    __println("v = " + v);
//    return 0;
//}

/* aug-assign reads its target, which is uninitialized. */
//-EXPECT-ERROR: Use of uninitialized variable 'u'
//int32 neg_aug() {
//    int u;
//    u += 1;
//    return 0;
//}

/* increment reads its operand, which is uninitialized. */
//-EXPECT-ERROR: Use of uninitialized variable 'u'
//int32 neg_inc() {
//    int u;
//    u++;
//    return 0;
//}

/* an uninitialized local passed as a call argument. */
//-EXPECT-ERROR: Use of uninitialized variable 'u'
//int32 takes(int a) { return a; }
//int32 neg_call() {
//    int u;
//    return takes(u);
//}

/* an uninitialized local read in a non-assignment context (a return). */
//-EXPECT-ERROR: Use of uninitialized variable 'u'
//int32 neg_return() {
//    int u;
//    return u;
//}

/* an uninitialized local read in a non-assignment context (a print). */
//-EXPECT-ERROR: Use of uninitialized variable 'u'
//int32 neg_print() {
//    int u;
//    __println("u = " + u);
//    return 0;
//}

/*
per-function isolation: an earlier function initializes a local named u; a later
function with a same-named local must NOT inherit that as initialized. The read
of u in neg_isolation is uninitialized despite seeded() writing its own u.
*/
//-EXPECT-ERROR: Use of uninitialized variable 'u'
//int32 seeded() {
//    int u = 1;
//    return u;
//}
//int32 neg_isolation() {
//    int u;
//    return u;
//}

/*
flow-sensitive definite assignment: a write on only SOME paths does not initialize
(the if/else join is a "must" intersection; a while body may run zero times).
*/

/* an if with no else leaves the local uninitialized on the fall-through path. */
//-EXPECT-ERROR: Use of uninitialized variable 'u'
//int32 neg_if_partial(int c) {
//    int u;
//    if (c > 0) { u = 1; }
//    return u;
//}

/* a while body may run zero times, so its write does not initialize. */
//-EXPECT-ERROR: Use of uninitialized variable 'u'
//int32 neg_while_zero(int c) {
//    int u;
//    while (c > 0) { u = 1; c = 0; }
//    return u;
//}

/* a move reads its source, which is uninitialized. */
//-EXPECT-ERROR: Use of uninitialized variable 'u'
//int32 neg_move_src() {
//    int u;
//    int v <-- u;
//    return v;
//}

/*
unused locals — a body-declared local that is never read. The two cases differ
only by whether it was written: never-written → "Unused local variable",
written → "set but never used".
*/

/* a1: declared, never written, never read. */
//-EXPECT-ERROR: Unused local variable 'u'
//int32 neg_unused() {
//    int u;
//    return 0;
//}

/* a2: written (its value is computed) but never read. */
//-EXPECT-ERROR: Local variable 'u' set but never used
//int32 neg_set_unused() {
//    int u = 5;
//    return 0;
//}

/*
composition: a use-before-init and an unused local on the same variable. The
use-before-init fires first (resolve reports it, then the unused sweep is gated
on hasErrors), so exactly one diagnostic — not a doubled report.
*/
//-EXPECT-ERROR: Use of uninitialized variable 'u'
//int32 neg_compose() {
//    int u;
//    u = u + 1;
//    __println("u = " + u);
//    return 0;
//}

/*
a class VALUE has no conversion to an unrelated type: it is assignable only to
the same class. Assigning one to a primitive (or to a different class) is a type
error, not a silent struct-into-scalar store — checked at the decl AND a bare
assignment.
*/

/* a class value cannot initialize a primitive variable. */
//-EXPECT-ERROR: Cannot implicitly convert 'C' to 'int'
//C(int x_) { _(){} ~(){} }
//int32 neg_class_init_int() {
//    C v(1);
//    int y = v;
//    __println("y = " + y);
//    return 0;
//}

/* the same mismatch through a bare assignment, not just a decl. */
//-EXPECT-ERROR: Cannot implicitly convert 'C' to 'int'
//C(int x_) { _(){} ~(){} }
//int32 neg_class_assign_int() {
//    C v(1);
//    int y = 0;
//    y = v;
//    __println("y = " + y);
//    return 0;
//}

/* two unrelated classes don't convert either. */
//-EXPECT-ERROR: Cannot implicitly convert 'B' to 'A'
//A(int x_) { _(){} ~(){} }
//B(int x_) { _(){} ~(){} }
//int32 neg_class_to_class() {
//    A a(1);
//    B b(2);
//    a = b;
//    __println("done");
//    return 0;
//}

/*
PER-ELEMENT AGGREGATE WIDENING — negative cases. classify validates shape
(dims/arity); codegen's per-element widen::convert rejects narrowing / cross-
family / sign-change at the leaf, attributed to the rhs.
*/

/* narrowing array — int[N] -> int8[N] rejects per element. */
//-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
//int32 neg_agg_narrow_array() {
//    int big[3] = (1, 2, 3);
//    int8 small[3] = big;
//    return small[0];
//}

/* narrowing tuple — (int, int) -> (int8, int8) rejects per slot. */
//-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
//int32 neg_agg_narrow_tuple() {
//    (int, int) ts = (1, 2);
//    (int8, int8) tn = ts;
//    return tn[0];
//}

/* cross-family — float[N] into int[N] rejects implicitly. */
//-EXPECT-ERROR: Cannot implicitly
//int32 neg_agg_cross_family() {
//    float fa[3] = (1.0, 2.0, 3.0);
//    int ia[3] = fa;
//    return ia[0];
//}

/* shape mismatch — same family, different dim. */
//-EXPECT-ERROR: array shape differs
//int32 neg_agg_dim_mismatch() {
//    int8 src[4] = (1, 2, 3, 4);
//    int  dst[3] = src;
//    return dst[0];
//}

/* tuple arity mismatch. */
//-EXPECT-ERROR: slot count differs
//int32 neg_agg_arity_mismatch() {
//    (int8, int8, int8) src = (1, 2, 3);
//    (int, int) dst = src;
//    return dst[0];
//}

/* sign-change at a leaf (int8 -> uint8) rejects, even in a tuple slot. */
//-EXPECT-ERROR: Cannot implicitly convert
//int32 neg_agg_sign_change_slot() {
//    (int8, int8) src = (-1, -2);
//    (uint8, uint8) dst = src;
//    return dst[0];
//}

/* narrowing through a sub-array kStoreStmt (`matrix[i] = wider_src`). */
//-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
//int32 neg_agg_narrow_substore() {
//    int8 mat[3][2];
//    int wide[2] = (1, 2);
//    mat[0] = wide;
//    return mat[0][0];
//}

/* narrowing through a deref kStoreStmt (`ref^ = wider_src`). */
//-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
//int32 neg_agg_narrow_deref_store() {
//    int8 dst[3];
//    int s[3] = (1, 2, 3);
//    int8[3]^ p = ^dst;
//    p^ = s;
//    return dst[0];
//}

/* narrowing through a move (`dst <-- wider_src`). */
//-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
//int32 neg_agg_narrow_move() {
//    int wide[3] = (1, 2, 3);
//    int8 narrow[3];
//    narrow <-- wide;
//    return narrow[0];
//}

/* narrowing through a return (function returns narrower than the value). */
//-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
//int8[3] returns_narrowed() {
//    int rs[3] = (1, 2, 3);
//    return rs;
//}

/* narrowing through a call-site (caller passes wider than the param). */
//-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
//int call_narrow_helper(int8[3]^ a) { return a^[0]; }
//int32 neg_agg_narrow_call() {
//    int wide[3] = (1, 2, 3);
//    return call_narrow_helper(wide);
//}

/*
declarator unification — tuple-TYPE slots must be anonymous, and a top-level sized
array dim is written on the NAME, not inline on the type.
*/

/* a NAMED slot in a tuple TYPE is "too many names" — the name belongs on the
   variable, not inside the type. */
//-EXPECT-ERROR: A tuple-type slot cannot be named
//int32 neg_tuple_slot_named() {
//    (int x, int y) z = (1, 2);
//    return 0;
//}

/* the same rule holds at ANY nesting depth — a named inner slot is rejected. */
//-EXPECT-ERROR: A tuple-type slot cannot be named
//int32 neg_tuple_slot_named_deep() {
//    ((int a, int b), int) v = ((1, 2), 3);
//    return 0;
//}

/* a TOP-LEVEL sized array dim must go on the NAME (`int x[3]`), not inline on the
   type (`int[3] x`). A dim NESTED in a pointer (`int[3]^ x`) is fine, as is an
   alias whose underlying is an array. */
//-EXPECT-ERROR: An array size belongs on the declared name
//int32 neg_top_dim_on_type() {
//    int[3] x = (1, 2, 3);
//    return 0;
//}

/*
construction-style scalar init boundary: only a SIZE-1 construction tuple collapses
to its element. A 2+-element or empty `(args)` on a scalar target stays an
aggregate-vs-scalar mismatch (`Cannot assign '(...)' to '<scalar>'`), the same as
the `=` form (`int x = (1, 2)`).
*/

/* too many construction args for a scalar. */
//-EXPECT-ERROR: Cannot assign '(int, int)' to 'int'
//int32 neg_ctor_too_many() {
//    int x(1, 2);
//    x;
//    return 0;
//}

/* an empty construction on a scalar has no value to init from. */
//-EXPECT-ERROR: Cannot assign '()' to 'int'
//int32 neg_ctor_empty() {
//    int x();
//    x;
//    return 0;
//}

/* A CHAR ARRAY FROM A STRING LITERAL must match the literal's size EXACTLY. The literal
   is `const char[N]` with N counting the NUL, so these are ordinary array-shape
   mismatches — no rule of their own. */

/* too SHORT: C would accept this and silently drop the terminating NUL, leaving an
   unterminated buffer. Slids does not. */
//-EXPECT-ERROR: Cannot assign 'const char[6]' to 'char[5]'
//int32 neg_str_short() {
//    char s[5] = "hello";
//    s;
//    return 0;
//}

/* too LONG: there is no defined fill for the tail. */
//-EXPECT-ERROR: Cannot assign 'const char[6]' to 'char[10]'
//int32 neg_str_long() {
//    char s[10] = "hello";
//    s;
//    return 0;
//}

/* (no negative for a differing ELEMENT type: `int s[6] = "hello"` is legal and lives
   with the positives above. char is uint8, so it rides the ordinary per-element widen —
   the same rule as `uint8[3] -> int[3]` earlier in this file. Only the SIZE is fixed.) */
