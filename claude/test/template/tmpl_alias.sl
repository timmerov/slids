/*
test alias templates.

definition:

    alias alias-name < template-list > = type ;

template aliases may be declared anywhere an alias may be declared.

usage:

    alias-name < type-list > variable-name ;

type-list is required.
*/

/*
claude says:

an alias template's TARGET resolves once into a PATTERN (each type parameter a
marker leaf); a USE resolves its arguments in the use's own scope and
substitutes them into the pattern — a pure structural walk. the expansion wraps
in a transparent alias labeled with the use AS WRITTEN, so ##type reports
`Ref<int>` (or `Ref<Integer>` through an alias argument) while every structural
consumer sees the underlying type. because a use inside a function template's
signature resolves its arguments where that template's own T is in scope, the
two features compose with no extra machinery (dump below).

declarable anywhere an alias is (file, namespace, class body, block); usable at
every type position (parseType owns the `Name<args>` form). nested uses
(`Ref<Ref<int>>`) landed with the `>>` closer split — a use's ARGUMENTS resolve
before the alias name joins the cycle set (composition, not a cycle; Nest<T>).
*/

import string;

alias Plain = int;

/* a two-parameter alias template; ##type shows the use as written. */
alias Pair2<A, B> = (A, B);

/* an alias OF a template-alias use (concrete args). */
alias IP = Ref<int>;

/* a namespace-scope alias template: used qualified from outside, and BARE from
   inside its own namespace. */
Deep {
    alias RT<T> = T^;
    int through(int v) { RT<int> t = ^v; return t^; }
}

/* a class-body alias template: used qualified from outside, and bare by the
   class's own method. */
Box(int b_ = 0) {
    alias BR<T> = T^;
    int look(BR<int> p) { return p^ + b_; }
}

/* a use in a template's SIGNATURE and BODY; also (T, T) shape via the alias.
   (The body's local spells the munged param's const — the flow rule.) */
T second<T>( Pair2<T, T>^ p ) {
    (const Pair2<T, T>)^ q = p;
    return q^[1];
}

/* the degenerate target: the parameter is ignored. */
alias K<T> = int;

/* an alias argument keeps the use's as-written label. */
alias Integer2 = int;

/* a file-scope GLOBAL typed by a use (constant initializer). */
Ref<int64> gref = nullptr;

/* template-alias uses in a signature: param and return type. */
int deref_ip(IP q) { return q^; }
Ref<int> pick(mutable Ref<int> r) { return r; }

/* a class field typed by a template-alias use. */
Holder(Ref<int> r_ = nullptr, int v_ = 0) { }

alias Ref<T> = T^;

alias DumpTuple<T> = (
    char[],  // ##file
    char[],  // ##line
    char[],  // ##type
    char[],  // ##name
    T^       // value
);

void dump64(DumpTuple<int64>^ tuple) {
    println(String + tuple^[0] + ":" + tuple^[1] + ": "
        + tuple^[2] + " " + tuple^[3] + " = " + tuple^[4]^);
}

void dump<T>(DumpTuple<T>^ tuple) {
    println(String + tuple^[0] + ":" + tuple^[1] + ": "
        + tuple^[2] + " " + tuple^[3] + " = " + tuple^[4]^);
}

int32 main() {

    int64 x = 64;
    dump64(#x);

    uint32 y = 99;
    dump(#y);

    Ref<int> p;
    println(String + "Ref<int> p type: " + ##type(p));

    int a = 10;
    int b = 20;

    /* the label is the use as written; structure is the underlying. */
    IP ip = ^a;
    println(String + "ip type: " + ##type(ip) + " deref = " + deref_ip(ip));
    Ref<int> rp = pick(^b);
    println(String + "rp = " + rp^);

    /* a two-parameter use; a tuple expansion is an ordinary tuple. */
    Pair2<int, float> pf = (3, 2.5);
    println(String + "pf = " + pf[0] + " " + pf[1] + " (" + ##type(pf) + ")");

    /* a template-alias use as a tuple SLOT type. */
    (Ref<int>, int) rt = (^a, 5);
    println(String + "rt = " + rt[0]^ + " " + rt[1]);

    /* a class field typed by a use. */
    Holder h(^a, 7);
    println(String + "h = " + h.r_^ + " " + h.v_);

    /* qualified uses: namespace-scope and class-body alias templates. */
    Deep:RT<int> dq = ^b;
    println(String + "dq = " + dq^);
    Box:BR<int> bq = ^a;
    println(String + "bq = " + bq^);

    /* a block-scope alias template. */
    alias LR<T> = T^;
    LR<int> lp = ^b;
    println(String + "lp = " + lp^);

    /* an explicit type-list on the composing template. */
    dump<uint32>(#y);

    /* the as-written label survives an ALIAS argument (same underlying). */
    Ref<Integer2> ari = ^a;
    println(String + "ari type: " + ##type(ari) + " deref = " + ari^);

    /* a CAST of a use (same-pointee reinterpret). */
    int^ q1 = ^a;
    Ref<int> cst = <Ref<int> >(q1);
    println(String + "cst = " + cst^);

    /* sizeof of uses. */
    println(String + "sz = " + sizeof(Ref<int>) + " " + sizeof(Pair2<int, float>));

    /* an array of uses. */
    Ref<int> ra[2];
    ra[0] = ^a;
    ra[1] = ^b;
    println(String + "ra = " + ra[0]^ + " " + ra[1]^);

    /* a use as the new operand. */
    Ref<int>^ np = new Ref<int>;
    np^ = ^a;
    println(String + "np = " + np^^);
    delete np;

    /* the file-scope global typed by a use. */
    gref = ^x;
    println(String + "gref = " + gref^);

    /* the degenerate target ignores its argument; the label keeps it. */
    K<float> kf = 9;
    println(String + "kf = " + kf + " (" + ##type(kf) + ")");

    /* composite arguments. */
    Pair2<int^, (const char)[]> pc = (^a, "hi");
    println(String + "pc = " + pc[0]^ + " " + pc[1]);

    /* a use in a template's signature and body; T binds through the expansion. */
    int sec = second((7, 8));
    println(String + "sec = " + sec);

    /* bare use inside the declaring namespace; the class's own method. */
    println(String + "thru = " + Deep:through(41));
    Box bx(5);
    println(String + "look = " + bx.look(^a));

    /* wrong number of template arguments. */
    //-EXPECT-ERROR: Wrong number of template arguments
    //Ref<int, float> bad1 = ^a; println(String + "bad1 = " + bad1^);

    /* type arguments on a plain alias. */
    //-EXPECT-ERROR: is not a template alias
    //Plain<int> bad2 = 0; println(String + "bad2 = " + bad2);

    /* a template alias needs its arguments. */
    //-EXPECT-ERROR: needs a type-argument list
    //Ref bare = ^a; println(String + "bare = " + bare^);

    /* an unknown type as an argument. */
    //-EXPECT-ERROR: Unknown type
    //Ref<Bogus> bad3 = ^a; println(String + "bad3 = " + bad3^);

    /* a QUALIFIED bare use needs its arguments too. */
    //-EXPECT-ERROR: needs a type-argument list
    //Deep:RT qb = ^a; println(String + "qb = " + qb^);

    /* an empty argument list on an ALIAS template: `<>` names a CLASS
       template's listless flavor — an alias use still needs its arguments. */
    //-EXPECT-ERROR: Wrong number of template arguments
    //Ref<> re = ^a; println(String + "re = " + re^);

    /* arguments on a NAMESPACE name. */
    //-EXPECT-ERROR: is not a template alias
    //Deep<int> dn = ^a; println(String + "dn = " + dn^);

    /* a qualified use with the wrong arity. */
    //-EXPECT-ERROR: Wrong number of template arguments
    //Deep:RT<int, int> qa = ^a; println(String + "qa = " + qa^);

    /* a duplicate block-scope alias template. */
    //-EXPECT-ERROR: Duplicate declaration
    //alias LR<U> = U[];

    /* a use inside a function-template type-list PARSES now (the nested-type
       landing, tmpl_nested.sl); this case then stops in dump's body, where the
       String chain meets the reference-typed T value. */
    //-EXPECT-ERROR: Arithmetic is not allowed on a reference
    //dump<Ref<uint32> >(#y);

    /* a self-NESTED use is composition, not a cycle: Nest<int> = int^^. */
    int nz = 9;
    Ref<int> nr = ^nz;
    Nest<int> nn = ^nr;
    int nv = nn^^; println(String + "nv = " + nv);

    return 0;
}

/* a duplicate declaration (aliases don't overload). */
//-EXPECT-ERROR: Duplicate declaration
//alias Ref<U> = U[];

/* a self-referential target is a cycle. */
//-EXPECT-ERROR: part of a cycle
//alias Cyc<T> = Cyc<T>^;
//Cyc<int> cy = nullptr;

/* a NESTED use in an alias target (the '>>' closer split + the composition-
   not-cycle rule; used positively in main). */
alias Nest<T> = Ref<Ref<T>>;

/* a template alias may not take a class's name. */
//-EXPECT-ERROR: Duplicate declaration
//alias Box<T> = T[];

/* nor a function template's name. */
//-EXPECT-ERROR: Duplicate declaration
//alias dump<T> = T^;

/* a mutually-recursive pair is a cycle (caught at the validate pass, no use
   needed). */
//-EXPECT-ERROR: part of a cycle
//alias MA<T> = MB<T>^;
//alias MB<T> = MA<T>^;
