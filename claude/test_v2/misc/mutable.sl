/*
test const and mutable usage.

at the current time, syntax only.
const correctness is not enforced.

basic const types.

    (const int)^ ref;
    (const int)[] iter;
    const int arr[3];
    (const int, int) tuple;
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

deferred to phase 6 (when const becomes enforceable):
  - enforcement (the assignment relation: mutable->const ok, const->mutable
    rejected, no write through a const reference). until then a const->mutable
    assignment WITHOUT a `<mutable>` cast is also silently accepted.
  - const methods (a const receiver).

caveats:
  - at a statement/decl start a leading `const` is the named-CONSTANT marker (it
    needs an initializer), so `const int arr[3];` / `const Class cls;` as locals
    are named constants, not const-qualified variables. a const-qualified
    variable type is written in a type position, e.g. `(const int)^ r`.
  - a parenthesized const type with a SIZED-array suffix as a statement-level
    local (`(const int)[3] a;`, `(const int, int)[2] a;`) does not parse — the
    parenthesized-type-decl lookahead admits only `^` / `[]`, not `[N]`. it is
    fine as a tuple SLOT (`((const int)[2], int)`) or a parameter. pre-existing
    declarator gap, not const-specific.
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
