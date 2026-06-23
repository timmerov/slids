/*
test overload class operators.

phase 1: default move/copy operators.
unblocks other features.
move/copy by slot iteratively and recursively.
requires lhs and rhs to be the same type.
*/

/*
claude says:

tbd
*/

DefaultMove(
    char c_,
    int^ p_,
    int[] q_
) {
    _() { __println("ctor " + c_); }
    ~() { __println("dtor " + c_); }
}

void print(DefaultMove^ dm) {
    __print(dm^.c_);
    if (dm^.p_ == nullptr) {
        __print(" nullptr");
    } else {
        __print(" " + dm^.p_^);
    }
    if (dm^.q_ == nullptr) {
        __println(" nullptr");
    } else {
        __println(" " + dm^.q_^);
    }
}

/* a class whose field is itself a class — move/copy must DESCEND into inner_
   (recursively applying DefaultMove's move/copy). */
Outer(
    DefaultMove inner_
) {
}

/* a class with TWO class fields — move/copy must descend into BOTH. */
Pair(
    DefaultMove a_,
    DefaultMove b_
) {
}

/* a pointer-free class — move is a pure copy (nothing to null, source intact). */
Plain(
    char c_,
    int n_
) {
    _() { __println("Plain ctor " + c_); }
    ~() { __println("Plain dtor " + c_); }
}

/* a class with an ARRAY of class — move/copy walks the elements (iterative AND
   recursive: class -> array -> element class -> pointer leaf). */
Holder(
    DefaultMove items_[2]
) {
}

int32 main() {

    int a = 42;
    int b = 7;
    int d[2] = (98, 99);

    /* ---- leaf class: the (move/copy) x (init/assign) matrix ---- */

    /* move-INIT: char copied, source pointer + iterator nulled. */
    DefaultMove mi('a', ^a, ^d[0]);
    DefaultMove mi2 <-- mi;
    print(^mi);                   // a nullptr nullptr
    print(^mi2);                  // a 42 98

    /* copy-INIT: pointer + iterator shared, source unchanged. */
    DefaultMove ci('b', ^b, ^d[1]);
    DefaultMove ci2 = ci;
    print(^ci);                   // b 7 99
    print(^ci2);                  // b 7 99

    /* copy-ASSIGN onto an existing object. */
    ci2.c_ = 'c';
    mi2 = ci2;                    // mi2 -> {c,^b,^d[1]}
    print(^mi2);                  // c 7 99
    print(^ci2);                  // c 7 99

    /* move-ASSIGN onto an existing object: source pointer + iterator nulled. */
    DefaultMove ma('d', ^a, ^d[0]);
    mi2 <-- ma;                   // mi2 -> {d,^a,^d[0]}; ma -> {d,null,null}
    print(^mi2);                  // d 42 98
    print(^ma);                   // d nullptr nullptr

    /* ---- self-copy is a no-op (self-MOVE is a compile error — see negatives) ---- */
    DefaultMove same('s', ^a, ^d[0]);
    same = same;                  // self-copy: a no-op
    print(^same);                 // s 42 98

    /* ---- recursive: a class-typed field is moved by descending into it ---- */
    Outer o(('a', ^a, ^d[0]));    // o.inner_ = {a, ^a, ^d[0]}
    Outer o2 <-- o;               // recursive move -> inner pointer + iterator nulled in o
    print(^o.inner_);             // a nullptr nullptr
    print(^o2.inner_);            // a 42 98

    /* ---- iterative: an array of class is moved element-wise ---- */
    DefaultMove arr[2] = (('a', ^a, ^d[0]), ('b', ^b, ^d[1]));
    DefaultMove brr[2] <-- arr;   // each arr[i]'s pointer + iterator nulled
    print(^arr[0]);               // a nullptr nullptr
    print(^arr[1]);               // b nullptr nullptr
    print(^brr[0]);               // a 42 98
    print(^brr[1]);               // b 7 99

    /* ---- a class with TWO class fields: move recurses into BOTH ---- */
    Pair pr(('a', ^a, ^d[0]), ('b', ^b, ^d[1]));
    Pair pr2 <-- pr;
    print(^pr.a_);                // a nullptr nullptr
    print(^pr.b_);                // b nullptr nullptr
    print(^pr2.a_);               // a 42 98
    print(^pr2.b_);               // b 7 99

    /* ---- a pointer-free class: move is a pure copy, source untouched ---- */
    Plain pl('p', 9);
    Plain pl2 <-- pl;
    __println("pl.n_ = " + pl.n_);    // 9 (nothing nulled)

    /* ---- a class with an array-of-class field: move walks the elements ---- */
    Holder h((('a', ^a, ^d[0]), ('b', ^b, ^d[1])));
    Holder h2 <-- h;
    print(^h.items_[0]);          // a nullptr nullptr
    print(^h2.items_[0]);         // a 42 98

    /* ---- move into a FIELD target (not a bare variable) ---- */
    DefaultMove srcf('z', ^a, ^d[0]);
    pr2.a_ <-- srcf;              // overwrite pr2.a_; null srcf
    print(^pr2.a_);               // z 42 98
    print(^srcf);                 // z nullptr nullptr

    return 0;
}

/*
negatives — one //-block uncommented per run.
*/

/* a self-MOVE is rejected: a whole-value move nulls the source's pointer leaves,
   so moving a value onto itself would wipe it after the no-op store. */
//-EXPECT-ERROR: Cannot move a value onto itself.
//int neg_self_move() {
//    int a = 42;
//    DefaultMove dm('a', ^a);
//    dm <-- dm;
//    return 0;
//}

/* default move/copy require the SAME type on both sides — a different class is
   not whole-value moved/copied (it falls to the field spread, which mismatches). */
//-EXPECT-ERROR: Cannot implicitly convert 'Pair' to 'char'.
//int neg_cross_type_move() {
//    int a = 42;
//    Pair pr(('a', ^a), ('b', ^a));
//    DefaultMove dm <-- pr;
//    return 0;
//}
