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

    return 0;
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
