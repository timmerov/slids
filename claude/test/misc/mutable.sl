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

the const qualifier is a promise that you will not modify the value
or anything it points to iteratively and recursively.

    const int^^^^^ a;
    (const int^^^^)^ b;
    a is an immutable pointer to constant data.
    b is a mutable pointer to constant data.
    a^ and b^ are exactly the same type. specifically:
    const (const (const (const int)^)^)^)^
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

phase 6 status (2026-07-26): the const-LVALUE WRITE WALL is LANDED — a
const value cannot be the target of a write (=, augmented, ++/--, a move's
TARGET); DELETE and a move's SOURCE are exempt by canon (the lifecycle
rule). assign/lvalue.sl owns the wall's canon. PARAM ENFORCEMENT is LANDED
too (same day): the munge's const is REAL — a body writes through a
reference / iterator param only when it is `mutable` (fill2/bump_ref the
positives; the neg_write_* family the negatives). The ONE param exemption
is the RECEIVER `_$recv`/`self` of a PLAIN method — field writes through
it stay legal (const on a method is the author's option, never required).
the SIZED-ARRAY munge gap is
CLOSED (same day): `int a[3]` munges ELEMENT-WISE to `(const int)[3]`
behind the by-pointer rewrite (multi-dim + alias-spelled arrays included;
`mutable` opts out), and the wall's index step sees through the
by-pointer auto-deref. THE CALLER SIDE is landed (same day): a const
value cannot flow into a `mutable` parameter — const-pointee values,
addressed const lvalues (addr-of MINTS a const pointee now), const
arrays, string literals (the pool is read-only; the context decay keeps
its const), and const classes into `mutable T^` all reject; `<mutable>`
casts through (the positive above). THE FLOW RULE is landed (same day):
a value flows into a slot that PRESERVES or ADDS const, never one that
DROPS it — every position, every depth (the positional lockstep walk;
copies stay const-blind per the value-category rule). STRING LITERALS
are STRICT: `char[] s = "hi"` rejects (the lying middle); the blessed
spellings are `const char[]` (deep alias), `(const char)[]` (shallow
alias), and `char s[3] = "hi"` (the sized-array COPY — owned, writable).
CONST IS RECURSIVE (the canon block above): every `const X` spelling —
leading OR buried (`(const int^^^)^`) — materializes the DEEP form of X
at type resolution, so `a^` and `b^` in the canon example are one type
(pinned in main). THE MUNGE IS TRANSITIVE the same way: a non-mutable
pointee deep-consts, so a `(char[], ...)^` param freezes what its slot
pointers reach — a literal-built `#` tuple flows into a dump param with
plain `char[]` slots (the author's natural spelling; the contract adds
the recursive const — see take_pair's ##type).
CONST METHODS are LANDED (same day; class/method.sl owns the canon):
`Ret const name(args)` munges the receiver `(const Class)^` deep, so the
body faces the wall and the flow rule through self; transitivity through
SELF only (a const method cannot call a non-const method on self);
external receivers stay UNGATED — const is never required; hooks and
free functions reject focused.
still deferred:
  - SWAP on const operands (its questions outrank it).

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

// THE MUNGE IS ENFORCED (param enforcement, 2026-07-26): a body writes
// through a reference / iterator param only when it is `mutable` — the
// caller sees the writes; the non-mutable twins are the negatives below.
void fill2(mutable int[] dst, int v) {
    dst[0] = v;
    dst[1] = v + 1;
}
void bump_ref(mutable int^ p) {
    p^ += 1;
}

// a SIZED-ARRAY param munges ELEMENT-WISE (the gap closed 2026-07-26:
// `int a[3]` -> `(const int)[3]` behind the by-pointer rewrite, multi-dim
// included); `mutable` opts out like any reference.
void show_arr(int a[3], mutable int m[3], int g[2][2]) {
    __println(##type(a));       // (const int)[3]^
    __println(##type(m));       // int[3]^
    __println(##type(g));       // (const int)[2][2]^
}
void fill3(mutable int a[3], int v) {
    a[0] = v;
    a[1] = v + 1;
    a[2] = v + 2;
}

// a DEEP-const return type (`const int^` — the leading const is deep by
// canon, materialized at type resolution so the flow rule reads it right).
const int^ deepret(int^ p) { return p; }

// CALLER-SIDE param const (2026-07-26): a const value cannot flow into a
// parameter that declared it will WRITE — an un-const pointee only arises
// from `mutable` — and `<mutable>` casts through (the negatives below pin
// each rejected source: a const-pointee value, an addressed const lvalue,
// a const array, a string literal, a const class into `mutable T^`).
void wchr(mutable char[] s) {
    s[0] = 'x';
}

// a reference parameter to a tuple munges its whole pointee to const.
void take_pair((int, int)^ p) {
    __println(##type(p));       // (const (const int, const int))^ — the munge is the RECURSIVE promise
}

// explicit const on a parameter: deep (outer, not re-munged) vs shallow.
void const_forms(const int^ deep, (const int)^ shallow) {
    __println(##type(deep));     // const (const int)^ — the leading const is
                                 // DEEP, materialized at type resolution
                                 // (2026-07-26): pointer AND pointee const
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
    // the mutable-dst arg is OWNED storage (the sized-array COPY form —
    // `char[] m = "abc"` would alias the read-only pool, a const drop
    // under the strict flow rule).
    char m[4] = "abc";
    munge(m, "xyz", 3);

    (int, int) pr = (1, 2);
    take_pair(^pr);

    // the ENFORCED munge, positive side: `mutable` params write and the
    // caller sees it.
    int fa[2];
    fill2(^fa[0], 8);
    __println(fa[0] + " " + fa[1]);   // 8 9
    int bv = 5;
    bump_ref(^bv);
    __println(bv);                    // 6

    // the array-param munge spellings, and a mutable array filled in place.
    int sa[3] = (1, 2, 3);
    int sm[3] = (4, 5, 6);
    int sg[2][2];
    sg[0][0] = 1;
    sg[0][1] = 2;
    sg[1][0] = 3;
    sg[1][1] = 4;
    show_arr(sa, sm, sg);
    fill3(sm, 7);
    __println(sm[0] + " " + sm[1] + " " + sm[2]);   // 7 8 9

    // a mutable char iterator writes; the LITERAL twin is a negative (the
    // pool is read-only).
    char cbuf[3] = "ab";
    wchr(^cbuf[0]);
    __println(cbuf);                  // xb

    // the caller side: addr-of a const lvalue MINTS a const pointee, and
    // the `<mutable>` cast is the sanctioned override at a call.
    const int cco[2] = (1, 2);
    __println(##type(^cco[1]));       // (const int)^
    bump_ref(<mutable> ^cco[0]);
    __println(cco[0]);                // 2 (the cast overrode the wall)

    // THE FLOW RULE (2026-07-26): adding const flows implicitly, at any
    // depth; a value copy is const-blind (a const array copies into a
    // mutable one — values, not aliases); `<mutable>` drops it explicitly.
    int fv = 3;
    int^ fm = ^fv;
    (const int)^ fc = fm;             // add const: implicit
    __println(fc^);                   // 3
    int^ fback = <mutable> fc;        // drop via the cast: sanctioned
    fback^ = 4;
    __println(fv);                    // 4
    int fcopy[2];
    fcopy = cco;                      // const ARRAY -> mutable array: a COPY
    fcopy[0] = 9;
    __println(fcopy[0] + " " + cco[0]);   // 9 2 (cco untouched)

    // the user's blessed const-string spellings: the deep-const alias to
    // the pool, the shallow alias, and the sized-array COPY.
    const char[] ds = "hi";
    __println(ds[0]);                 // h
    (const char)[] ss = "yo";
    __println(ss[1]);                 // o
    char ows[3] = "ab";
    ows[0] = 'z';
    __println(ows);                   // zb

    // a DEEP-const return type parses and means what it says (the leading
    // const materializes deep at type resolution).
    const int^ dr = deepret(^fv);
    __println(dr^);                   // 4

    // CONST IS RECURSIVE (the canon block above): `const int^^^ a3` and
    // `(const int^^)^ b3` agree that their derefs are ONE type — a buried
    // const materializes exactly as deep as a leading one, so the two
    // spellings cross-assign in both directions.
    int v1o = 7;
    int^ v1p = ^v1o;
    int^^ v2p = ^v1p;
    const int^^^ a3 = ^v2p;
    (const int^^)^ b3 = ^v2p;
    __println(##type(a3));            // const (const (const (const int)^)^)^
    const int^^^ x3 = b3;
    (const int^^)^ y3 = x3;
    __println(y3^^^);                 // 7

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
    show_pair(^ct);              // (const (const int, const int))^ — deep through the munge

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

    // a const tuple with a CLASS slot — the tuple-type-with-a-class-slot
    // resolveDeclType gap is fixed, so the composed const type resolves and the
    // const routing builds it (slot 0 is a default construction of Box).
    const (Box, int) cbt = (Box(1), 2);
    __println(##type(cbt));               // (const Box, const int)
    __println("" + cbt[0].v_ + " " + cbt[1]);  // 1 2

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

    // WRITE through a const variable: REJECTED since the const-lvalue wall
    // landed (the negative below; assign/lvalue.sl owns the write canon).
    const int cw[2] = (1, 2);
    __println(cw[0]);                     // 1

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

// the const-lvalue WALL (landed; assign/lvalue.sl owns the full canon): a
// write through a const variable rejects.
//-EXPECT-ERROR: Cannot write to a const value
//int neg_const_write() {
//    const int cwn[2] = (1, 2);
//    cwn[0] = 9;
//    return cwn[0];
//}

// PARAM ENFORCEMENT: the munge's const is real — a body may not write
// through a NON-mutable reference param...
//-EXPECT-ERROR: Cannot write to a const value
//void neg_write_ref(int^ p) { p^ = 1; }

// ...nor a non-mutable iterator's element...
//-EXPECT-ERROR: Cannot write to a const value
//void neg_write_iter(int[] it) { it[0] = 1; }

// ...nor a munged class param's field...
//-EXPECT-ERROR: Cannot write to a const value
//Wbox(int w_ = 0) { }
//void neg_write_class(Wbox^ b) { b^.w_ = 1; }

// ...and an AUTHOR-spelled const pointee is the same wall.
//-EXPECT-ERROR: Cannot write to a const value
//void neg_write_shallow((const int)^ p) { p^ = 1; }

// a SIZED-ARRAY param's element (the closed munge gap)...
//-EXPECT-ERROR: Cannot write to a const value
//void neg_write_arr(int a[3]) { a[0] = 1; }

// ...a MULTI-DIM element...
//-EXPECT-ERROR: Cannot write to a const value
//void neg_write_grid(int g[2][2]) { g[1][1] = 1; }

// ...and an ALIAS-spelled array param is the same munge.
//-EXPECT-ERROR: Cannot write to a const value
//void neg_write_alias_arr(A3 a) { a[2] = 1; }

// CALLER-SIDE: an addressed const lvalue into a `mutable` reference...
//-EXPECT-ERROR: Cannot pass a const value
//int neg_pass_ref() {
//    const int ca[2] = (1, 2);
//    bump_ref(^ca[0]);
//    return ca[0];
//}

// ...a const-elem iterator into a `mutable` iterator...
//-EXPECT-ERROR: Cannot pass a const value
//int neg_pass_iter() {
//    const int ca[2] = (1, 2);
//    fill2(^ca[0], 9);
//    return ca[0];
//}

// ...a const array into a `mutable` array param...
//-EXPECT-ERROR: Cannot pass a const value
//int neg_pass_arr() {
//    const int ca[3] = (1, 2, 3);
//    fill3(ca, 9);
//    return ca[0];
//}

// ...a const-pointee POINTER VALUE into a `mutable` reference...
//-EXPECT-ERROR: Cannot pass a const value
//int neg_pass_ptr() {
//    int z = 1;
//    (const int)^ cq = ^z;
//    bump_ref(cq);
//    return z;
//}

// ...and a STRING LITERAL into a `mutable char[]` — the pool is read-only.
//-EXPECT-ERROR: Cannot pass a const value
//void neg_pass_lit() {
//    wchr("hi");
//}

// THE FLOW RULE's negatives: dropping const rejects at every position and
// depth. A decl-init drop...
//-EXPECT-ERROR: Cannot drop 'const'
//int neg_flow_decl() {
//    int z = 1;
//    (const int)^ c = ^z;
//    int^ m = c;
//    return m^;
//}

// ...a plain-assign drop...
//-EXPECT-ERROR: Cannot drop 'const'
//int neg_flow_assign() {
//    int z = 1;
//    (const int)^ c = ^z;
//    int^ m = ^z;
//    m = c;
//    return m^;
//}

// ...a MOVE's copy half...
//-EXPECT-ERROR: Cannot drop 'const'
//int neg_flow_move() {
//    int z = 1;
//    (const int)^ c = ^z;
//    int^ m = ^z;
//    m <-- c;
//    return m^;
//}

// ...a RETURN drop...
//-EXPECT-ERROR: Cannot drop 'const'
//int^ neg_flow_ret((const int)^ c) { return c; }

// ...the TUPLE-SLOT launder (a tuple COPY carries its pointer slots)...
//-EXPECT-ERROR: Cannot drop 'const'
//int neg_flow_tuple() {
//    int z = 1;
//    ((const int)^, int) ct = (^z, 2);
//    (int^, int) mt = ct;
//    return mt[0]^;
//}

// ...the POSITIONAL launder (const-bearing both sides, slot 0 swapped)...
//-EXPECT-ERROR: Cannot drop 'const'
//int neg_flow_pos() {
//    int z = 1;
//    ((const int)^, (int)^)^ a = nullptr;
//    ((int)^, (const int)^)^ b = a;
//    b;
//    return z;
//}

// ...the DEPTH drop (`^^` — the inner pointee's const)...
//-EXPECT-ERROR: Cannot drop 'const'
//int neg_flow_deep() {
//    int z = 1;
//    (const int)^ c = ^z;
//    (const int)^^ cc = ^c;
//    int^^ mm = cc;
//    return mm^^;
//}

// ...and the STRICT literal ruling: `char[] s = "hi"` is the lying middle —
// a mutable alias to the read-only pool (spell `(const char)[]`, deep
// `const char[]`, or the sized-array COPY `char s[3]`).
//-EXPECT-ERROR: Cannot drop 'const'
//int neg_flow_lit() {
//    char[] s = "hi";
//    return s[0];
//}

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

