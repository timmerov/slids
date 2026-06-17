/*
test const and mutable usage.

at the current time, syntax only.
const correctness is not enforced.

basic const types.

    const int^ ref;
    const int[] iter;
    (const int)^ ref;
    (const int)[] iter;
    const int arr[3];
    const (int, int) tuple;
    (const int, int) tuple;
    const (int,int)^ tuplep;
    (const (int, int))^ tuplep;
    const Class cls;
    const Clsss^ clsp;

parameter munging.

    void strcpy( mutable char[] dst, char[] src );
    ##type(dst) --> char[]
    ##type(src) --> (const char)[]

casting.

    int^ mut;
    const int^ constp;
    constp = mut;
    constp = <const> mut;
    mut = <mutable> constp;
*/

/*
claude says:

implemented (syntax only — const correctness is NOT enforced yet):
  - const / mutable are real facets on the type, not strings. const binds
    loosest, so `const T^` is deep (the pointer AND its data) and `(const T)^`
    is shallow (a mutable pointer to const data). both spellings round-trip.
  - parameter munge (the default contract): a reference / iterator pointee
    becomes const unless `mutable` opts out — `T^` -> `(const T)^`,
    `T[]` -> `(const T)[]`. a primitive passes by value (untouched). `##type`
    shows the munged type. see the cases below.
  - `mutable` is a pointer/iterator PARAMETER qualifier only; it is rejected on
    a non-pointer parameter, on a class field, and in any non-parameter
    position (negative cases below).
  - const composes into COMPLEX types: a tuple slot at any position, a nested
    tuple, an array element, an iterator element. the reference munge wraps the
    WHOLE pointee, composing with an inner const slot the author wrote.
  - const works on a CLASS reference (a `Class^` param munges to `(const Class)^`;
    explicit `const Class^` / `(const Class)^` are accepted), and an alias may
    name a const type (`alias CI = const int;`) — the alias name still shows in
    `##type`.
  - the implicit mutable->const cast holds for references and iterators (a `T^`
    / `T[]` value flows into a const-pointee target). it is unchecked for now,
    so the reverse direction is also silently accepted.
  - the casting block above works: `<const>` / `<mutable>` add / remove const on
    a pointer (keeping its type), and the full-type forms `<const T^>` / `<T^>`
    do the same. value-preserving (const is erased in IR). `<const>` /
    `<mutable>` reject a non-pointer operand.
  - a leading `const` on a NON-SCALAR LOCAL (array / tuple / pointer / iterator /
    class) is a not-mutable VARIABLE — allocated + initialized, its type
    DEEP-const-wrapped (every mutable position: array elem, tuple slot, pointer +
    pointee) — so `##type` shows the const and it survives index / slot / deref. a
    leading `const` on a foldable SCALAR stays a substituted named constant
    instead. at a NON-RUNTIME scope (file / namespace / class body) a non-scalar
    const is a GLOBAL — not yet built (phase 8) — and is reported as such.

deferred to phase 6 (when const becomes enforceable):
  - enforcement (the assignment relation: mutable->const ok, const->mutable
    rejected, no write through a const reference). until then a const->mutable
    assignment WITHOUT a `<mutable>` cast is also silently accepted.
  - const methods (a const receiver).

caveats:
  - a leading `const` at a statement/decl start needs an initializer. on a
    foldable SCALAR it is the named-CONSTANT marker (a substituted constant); on a
    non-scalar type it is a not-mutable VARIABLE (see above). a SCALAR
    const-qualified variable type is still written in a type position,
    `(const int)^ r` (a leading `const int x` would be the named constant).
*/

// parameter munge: `mutable char[]` opts out (-> char[]); plain `char[]`
// defaults to a const element (-> (const char)[]); the primitive is by value.
void munge(mutable char[] dst, char[] src, int count) {
    __println(##type(dst));     // char[]
    __println(##type(src));     // (const char)[]
    __println(##type(count));   // int
}

// a reference parameter to a tuple munges its whole pointee to const.
void take_pair((int, int)^ p) {
    __println(##type(p));       // (const (int, int))^
}

// explicit const on a parameter: deep (outer, not re-munged) vs shallow.
void const_forms(const int^ deep, (const int)^ shallow) {
    __println(##type(deep));     // const int^
    __println(##type(shallow));  // (const int)^
}

// a reference parameter to a complex value munges the WHOLE pointee to const,
// composing with an inner const slot the author already wrote.
void show_pair((const int, int)^ p) {
    __println(##type(p));        // (const (const int, int))^
}

// const on a CLASS reference: a class ref param munges its pointee to const,
// and the explicit deep / shallow forms are accepted too.
Box(int v_) { }
void show_box(Box^ b)                 { __println(##type(b)); }  // (const Box)^
void show_box_deep(const Box^ b)      { __println(##type(b)); }  // const Box^
void show_box_shallow((const Box)^ b) { __println(##type(b)); }  // (const Box)^

// an alias whose underlying is a const type; the alias name still shows in ##type.
alias CI = const int;

// an alias to a NON-SCALAR (an array) — used by a const-variable test below.
alias A3 = int[3];

// a const tuple value passed BY REFERENCE into a function (auto-ref of a const
// lvalue at the call site).
void show_ctup((const int, const int)^ p) { __println(p^[0]); }   // 4

// a hook class (explicit ctor + dtor): a const variable of it runs the ctor once
// at the decl and the dtor once at scope exit — through the const-variable path.
Res(int id_) {
    _() { __println("Res.ctor"); }
    ~() { __println("Res.dtor"); }
}

int32 main() {
    char[] m = "abc";
    munge(m, "xyz", 3);

    (int, int) pr = (1, 2);
    take_pair(^pr);

    int n = 7;
    const_forms(^n, ^n);

    // a const-qualified reference local; read through it.
    (const int)^ r = ^n;
    __println(##type(r));        // (const int)^
    __println(r^);               // 7

    // a tuple with a const slot; both slots read normally.
    (const int, int) ct = (4, 5);
    __println(##type(ct));       // (const int, int)
    int sum = ct[0] + ct[1];
    __println(sum);              // 9

    // const in COMPLEX types: a complex-value ref param munge, then const at a
    // non-first slot, a nested tuple slot, and an array element (all via type).
    show_pair(^ct);              // (const (const int, int))^

    (const int, const char[]) hetero;
    __println(##type(hetero));   // (const int, const char[])
    ((const int, int), int) nested;
    __println(##type(nested));   // ((const int, int), int)
    ((const int)[2], int) constarr;
    __println(##type(constarr)); // ((const int)[2], int)

    // implicit mutable -> const cast (reference and iterator; not enforced).
    int^ mref = ^n;
    (const int)^ cref = mref;
    __println(cref^);            // 7
    int arr[3] = (10, 20, 30);
    (const int)[] citer = ^arr[0];
    __println(citer[1]);         // 20

    // qualifier casts: <const> adds const, <mutable> removes it (on a pointer of
    // the same type); the full-type forms <const T^> / <T^> do the same. value-
    // preserving — const is erased in IR — so the pointer reads identically.
    int^ qm = ^n;
    __println(##type(<const> qm));        // const int^
    __println(##type(<const int^> qm));   // const int^
    (const int)^ qc = ^n;
    __println(##type(<mutable> qc));      // int^
    int^ qback = <mutable> qc;
    __println(qback^);                    // 7

    // const on a class reference (munge + explicit deep / shallow).
    Box bx(1);
    show_box(^bx);                        // (const Box)^
    show_box_deep(^bx);                   // const Box^
    show_box_shallow(^bx);                // (const Box)^

    // const carried through an alias.
    CI cv = 7;
    __println(##type(cv));                // CI
    __println(cv);                        // 7
    CI^ ap = ^n;
    __println(##type(ap));                // CI^

    // const VARIABLES: a leading `const` on a NON-SCALAR type (array / tuple /
    // pointer / iterator / class) is a not-mutable VARIABLE — allocated +
    // initialized, with a DEEP-const type — NOT a substituted constant. const is
    // unenforced (phase 6), so each behaves as an ordinary local. (a leading
    // `const` on a foldable SCALAR stays a substituted constant — the array dims
    // and `CI cv` above.)
    const int carr[3] = (1, 2, 3);
    __println(##type(carr));              // (const int)[3]
    int csum = carr[0] + carr[2];
    __println(csum);                      // 4

    const (int, int) ctup = (4, 5);
    __println(##type(ctup));              // (const int, const int)
    __println(ctup[1]);                   // 5

    const int^ cdeep = ^n;
    __println(##type(cdeep));             // const (const int)^
    __println(cdeep^);                    // 7

    const (int, int)^ ctp = ^ctup;
    __println(##type(ctp));               // const (const int, const int)^
    __println(##type(ctp^));              // (const int, const int)
    __println(ctp^[0]);                   // 4

    const int[] cit = ^carr[0];
    __println(##type(cit));               // const (const int)[]
    __println(cit[2]);                    // 3

    const Box cbx(9);
    __println(##type(cbx));               // const Box
    __println(cbx.v_);                    // 9

    // a const variable of a LOCAL class (defined in this body) — the local-class
    // registration runs before the const pre-pass, so the const decl's type resolves.
    Pt(int px_, int py_) { }
    const Pt cp(2, 3);
    __println(##type(cp));                // const Pt
    __println(cp.py_);                    // 3

    // const aggregate in a NESTED block — the resolveStmtList path (distinct from
    // the function-body pre-pass; both register local classes before consts).
    if (true) {
        const int nb[2] = (8, 9);
        __println(nb[1]);                 // 9
    }

    // DEEP const recurses through COMPOSED types: a nested tuple and a
    // multi-dimensional array const-qualify every leaf.
    const ((int, int), int) cnest = ((1, 2), 3);
    __println(##type(cnest));             // ((const int, const int), const int)
    const int cm[2][3] = ((1, 2, 3), (4, 5, 6));
    __println(##type(cm));                // (const int)[2][3]
    __println(cm[1][2]);                  // 6

    // an alias to a NON-SCALAR, declared const: deepConst recurses through the
    // alias; ##type shows the alias NAME (the const rides underneath it).
    const A3 ca = (1, 2, 3);
    __println(##type(ca));                // A3
    __println(ca[2]);                     // 3

    // a const variable passed BY REF to a function (auto-ref of a const lvalue).
    show_ctup(^ctup);                     // 4

    // WRITE through a const variable: accepted today (const is UNENFORCED — phase 6
    // flips this to an error, which will trip this case).
    const int cw[2] = (1, 2);
    cw[0] = 9;
    __println(cw[0]);                     // 9

    // move (`<--`) and swap (`<-->`) involving a const variable.
    (int, int) mv = (3, 4);
    const (int, int) cmv <-- mv;
    __println(cmv[0]);                    // 3
    const (int, int) csw = (1, 2);
    (int, int) sw = (5, 6);
    csw <--> sw;
    __println(csw[0]);                    // 5
    __println(sw[0]);                     // 1

    // a const variable of a HOOK class (ctor / dtor) — in its own block so the
    // dtor fires deterministically at block exit. ctor at the decl, dtor once.
    {
        const Res cr(5);
        __println(##type(cr));            // const Res
        __println(cr.id_);                // 5
    }                                     // Res.dtor here

    return 0;
}

/*
negative cases. each block below is disabled; the negative-test runner enables
one at a time and asserts the marked error substring.
*/

//-EXPECT-ERROR: applies only to a pointer
//void neg_mut_value(mutable int x) { }

//-EXPECT-ERROR: may only appear on a function parameter
//mutable int neg_mut_nonparam;

//-EXPECT-ERROR: may only appear on a function parameter
//Box(mutable int x_) { }

// a non-scalar const at NON-RUNTIME scope (file / namespace / class body) is a
// not-mutable GLOBAL — allocated, not substituted — which needs globals (phase 8).
//-EXPECT-ERROR: requires global storage
//const int neg_file_arr[3] = (1, 2, 3);

//-EXPECT-ERROR: requires global storage
//Glob { const (int, int) neg_ns_tup = (1, 2); }

//-EXPECT-ERROR: requires global storage
//Held(int z_) { const int neg_member_arr[2] = (1, 2); }

// a TYPELESS const aggregate should infer as a const VARIABLE (like the typed
// form), but the form is unknown at resolve so it stays on the substitution path
// and mis-reports "not a constant expression". deferred until typeless-const
// inference distinguishes a foldable scalar from an aggregate.
//-EXPECT-ERROR-DEFERRED: typeless const aggregate mis-routed to the substitution path
//int main() { const a = (1, 2, 3); __println(a[0]); return 0; }

// a const tuple with a CLASS slot is blocked by the TUPLE TYPE WITH A CLASS SLOT
// bug (resolveDeclType: "Unknown type '(Box, int)'") — the const routing is fine;
// the composed type can't be resolved. deferred until that bug is fixed.
//-EXPECT-ERROR-DEFERRED: blocked by tuple-type-with-a-class-slot resolveDeclType gap
//Bxx(int v_) { } int main() { const (Bxx, int) t = (Bxx(1), 2); return 0; }
