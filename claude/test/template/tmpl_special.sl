/*
test function template specialization.

defined by example.
suppose we want to return the number of elements in a container
of an unspecified type.
the ugliness we are trying to avoid:

    intptr countof_array<T>(T arg) {
        return sizeof(arg)/sizeof(arg[0]);
    }

    intptr countof_tuple<T>(T arg) {
        slids:fatal_error("countof tuple type not yet implemented.");
    }

    intptr countof_primitive<T>(T arg) {
        return 1;
    }

    intptr countof_class<T>(T arg) {
        return arg.size();
    }

define sets of types.

    alias set-name = < type-list > ;

the type-list is a list of valid types (including arrays, explicit tuples,
and template types) and pattern matching sequences separated by |.
pattern matching sequences and their meanings:

    reference: optional-type ^
    iterator: optional-type []
    fixed size array: optional-type [N]
    tuple: ( optional-size-N )
    excluded: !

type sets are built left to right.
types (or sets) are added with |.
types (or sets) are removed with |!.
removing a type that is not in the set is weird, but valid - this decision
may be revisited in the future.
there is only one set of < angle brackets > in the alias statement (plus
those necessary for template types).
a leading ! matches everything except the members of the set.

example useful type sets:

    alias SignedIntegers = <int|int8|int16|int32|int64>;
    alias UnsignedIntegers = <uint|uint8|uint16|uint32|uint64>;
    alias Integers = <SignedIntegers|UnsignedIntegers>;
    alias Floats = <float|float32|float64>;
    alias Numbers = <Integers|Floats>;
    alias Pointers = <^|[]>;
    alias Primitives = <bool|char|intptr|Numbers|Pointers>;
    alias Arrays = <[N]>;
    alias Tuples = <()>;
    alias Classes = !<Arrays|Tuples|Primitives>;

advanced features of type sets:

    alias BigIntegers = <Integers|!int8|!uint8>;
    alias SomeTuples = <(int,int)|(float,float)>;
    alias SomeClasses = <ClassA|ClassB|ClassC>;
    alias SomePointers = <int^|float[]|ClassA^^|ClassB[]>;

specialized templates:
note: some of these specialization examples intentionally conflict
with others.

    intptr countof<T=A[N]>(A arg[N]) {
        return N;
    }
    intptr countof<T=[N]>(T arg) {
        return N;
    }
    intptr countof<T=[N][M]>(T arg) {
        return N * M;
    }

    intptr countof<T=()>(T arg) {
        slids:fatal_error("countof tuple type not yet implemented.");
    }
    intptr countof<T=(N)>(T arg) {
        return N;
    }

    intptr countof<T=Primitives>(T arg) {
        return 1;
    }

    intptr countof<T=Classes>(T arg) {
        return arg.size();
    }

interesting side effect with string literals:

    five = sizeof("hello");
    six = countof("hello");

inline type sets:

    bool is_zero<T=<int|float>>(T arg) {
        return (arg == (T=0));
    }

an unused optional-type is a compile error.

    /* compile error: A2 is not used. */
    intptr countof<T=A2[N][M]>(T arg) {
        return N * M;
    }

    /* valid syntax: T is neither optional nor used. */
    intptr countof<T=A[N]>(A arg[N]) {
        return N;
    }

    /* valid syntax: N is required to indicate an array. */
    intptr countof<T=[N]>(T arg) {
        return sizeof(arg) / sizeof(arg[0]);
    }


example usage:

    Class(int a) {
        intptr size() {
            return a;
        }
    }
    int iarr[2];
    float farr[3];
    Class carr[4];
    n = countof(iarr);
    n = countof(farr);
    n = countof(carr);
    n = countof(carr[0]);

ambiguous specialized templates is a compile error.
example:

    void error<T=Integers>(T arg) { }
    void error<T=int>(T arg) { }

mixing specialized and non-specialized templates.
the specialized templates may not be ambiguous with each other.
the specialized template is matched before the non-specialized template.
example:

    T mixed<T=int>(T arg) { return 0; }
    T mixed<T>(T arg) { return arg; }

currently, explicit class types must match exactly.
a derived type does not match a specialization for its base.
this decision may be revisited in the future.
*/

/*
claude says:

sets: `alias Name = <terms>;` registers a TYPE SET entry (not a type — using
one in a type position errors "is a type set, not a type"). terms resolve
eagerly at the alias-validate pass; membership walks them LEFT TO RIGHT
(`|` adds, `|!` removes — an absent member's removal is a no-op — and a
leading `!` complements over everything), recursing through referenced sets
by entry id. concrete members compare canonically: aliases shed, the OUTER
const peeled, buried const kept. patterns are categories: `^` any reference,
`[]` any iterator, `[N]` / `[N][M]` DEPTH-EXACT fixed arrays (a 2-D array
never matches `[N]`), `()` / `(N)` any unnamed tuple (slids has no 1-tuples,
so a lone identifier in parens is always an arity, never an element type).

specialization: `name<T=constraint>` arms share a name and an arity —
registration permits the overlap when EITHER sibling is constrained (only
two unconstrained siblings still clash: at most one catch-all per range).
selection is PER ARM at the call: each constrained arm deduces T against
its OWN patterns (failures muted — arms may differ in shape), membership
filters the successful binds, and exactly one arm must survive: none falls
back to the family's unconstrained CATCH-ALL (bound against its own
patterns, full pattern language — canon: specialized matches first), more
than one is "Ambiguous specialization" — a catch-all NEVER rescues an
ambiguity, and an exact-type arm gets NO priority over a set containing it
(the canon error<T=Integers>/error<T=int> example). with no catch-all, no
match is "No specialization"; if nothing even deduces, the pattern-mismatch
(or conflicting-bindings) diagnostic surfaces instead. a constraint may
also be a full TYPE SPELLING — `T=int`, `T=int^`, `T=(int,int)`, `T=int[]`,
`T=Box<int>` — a one-type set (full specialization); a bare identifier
still resolves set-first. arity selection and plain-beats-template are
unchanged upstream; a different-arity unconstrained sibling coexists as
before.

inline typesets: `T=<int|float>` (and `T=!<...>`) is an ANONYMOUS set —
the full term grammar applies (concrete types, named sets, patterns), the
terms resolve at the same point a `T=Name` lookup does, and nothing enters
the symbol table. patterns inside are WILDCARDS: an inline set binds
nothing — binders need the direct `T=A[N]` / `T=(N)` forms. the `...>>`
tail splits like a nested template type-arg's. note a float LITERAL binds
`float` (the typeless-decl type — measured; the reference's "3.14 is
float64" line is stale), so `is_zero(3.5)` lands in `<int|float>` while a
declared float64 needs its own arm.

binders bind at instantiation from the matched T: the element type as a
transparent alias, each dimension — and a tuple's arity — as an intptr
constant the body and dims read. a param spelled through the binders
(`(A arg[N])`) IS the constrained T: its pattern is the bare T, and the
instance substitutes the bound type directly. an unused ELEMENT-TYPE binder
errors at registration (a misspelled type must not become a silent
wildcard); size binders are required by the pattern's spelling and may go
unused. specialization is a free-function feature — a method template's
`T=` is rejected.

the decay change: a bare-T param meeting an ARRAY argument now binds T to
the ARRAY type — arrays ride the class/tuple rung of the convention of
convenience ((const T)^; the call passes the whole-array reference). the
old decayed-iterator binding is gone; an iterator binding is still
reachable with an explicit type-list (tmpl_function.sl's string-literal
call now spells `zork<char[]>`).

sizeof note: the canon comment expects `five = sizeof("hello")`, but sizeof
includes the NUL (canon test/misc/sizeof.sl: "Hello, World!" is 14), so
sizeof and countof both yield 6 here. flagged as a canon conflict — nothing
changed on either side.
*/

import string;

/* the canon sets, plus Arrays2 so the complement excludes 2-D arrays too. */
alias SignedIntegers = <int|int8|int16|int32|int64>;
alias UnsignedIntegers = <uint|uint8|uint16|uint32|uint64>;
alias Integers = <SignedIntegers|UnsignedIntegers>;
alias Floats = <float|float32|float64>;
alias Numbers = <Integers|Floats>;
alias Pointers = <^|[]>;
alias Primitives = <bool|char|intptr|Numbers|Pointers>;
alias Arrays = <[N]>;
alias Arrays2 = <[N][M]>;
alias Tuples = <()>;
alias Classes = !<Arrays|Arrays2|Tuples|Primitives>;

/* the countof family — one arm per disjoint set. the 1-D arm spells its
   param through the binders; the others take T. */
intptr countof<T=A[N]>(A arg[N]) {
    return N;
}
intptr countof<T=[N][M]>(T arg) {
    return N * M;
}
intptr countof<T=(N)>(T arg) {
    return N;
}
intptr countof<T=Primitives>(T arg) {
    return 1;
}
intptr countof<T=Classes>(T arg) {
    return arg.size();
}
/* an unconstrained sibling with a DISJOINT arity coexists as before. */
intptr countof<T>(T a, T b) {
    return 2;
}

/* left-to-right removal: BigIntegers lacks int8/uint8; NotBig is the rest. */
alias BigIntegers = <Integers|!int8|!uint8>;
alias NotBig = !<BigIntegers>;
intptr big<T=BigIntegers>(T v) { return 1; }
intptr big<T=NotBig>(T v) { return 2; }

/* removing an ABSENT member is a no-op, and order matters: the removal
   precedes the add, so int8 IS in Weird. */
alias Weird = <!int8|Integers>;
alias NotWeird = !<Weird>;
intptr weird<T=Weird>(T v) { return 1; }
intptr weird<T=NotWeird>(T v) { return 2; }

/* an exact tuple type as a member; a category minus a concrete member. */
alias PairII = <(int,int)>;
alias OtherTuples = <()|!(int,int)>;
intptr twos<T=PairII>(T v) { return 1; }
intptr twos<T=OtherTuples>(T v) { return 2; }

/* typed pointers as members; the pointer categories minus one. */
alias IntRef = <int^>;
alias OtherPtrs = <^|[]|!int^>;
intptr ptrs<T=IntRef>(T v) { return 1; }
intptr ptrs<T=OtherPtrs>(T v) { return 2; }

/* a single CLASS name is a one-type set: full specialization. */
Duck(int webs = 2) { }
Goat(int hooves = 4) { }
intptr legs<T=Duck>(T v) { return 2; }
intptr legs<T=Goat>(T v) { return 4; }

/* a template-type member in a set (the one-angle-bracket carve-out). */
Box<T>(T v_) { }
alias Boxes = <Box<int>>;
alias NotBoxesI = !<Boxes>;
intptr boxed<T=Boxes>(T v) { return 1; }
intptr boxed<T=NotBoxesI>(T v) { return 2; }

/* a mutable array param writes through to the caller (the whole-array
   reference the convention passes). */
void fill9<T=A[N]>(mutable A arg[N]) {
    arg[0] = 9;
}

/* constrained arms at TWO arities: the argument count filters the arm set
   before membership does. */
intptr pick2<T=Primitives>(T a, T b) { return 1; }
intptr pick2<T=Classes>(T a, T b) { return 2; }
intptr pick2<T=Primitives>(T v) { return 3; }

/* INLINE typesets (canon lines 102-106): `T=<terms>` is an anonymous set —
   the full term grammar, nothing registered. Patterns inside are wildcards
   (an inline set binds nothing). `!<...>` complements. */
bool is_zero<T=<int|float>>(T arg) {
    return (arg == (T=0));
}
intptr inl<T=<int|char>>(T v) { return 1; }
intptr inl<T=<float|bool>>(T v) { return 2; }
intptr noz<T=<int|float>>(T v) { return 1; }
intptr noz<T=!<int|float>>(T v) { return 2; }
intptr mix<T=<Floats|[Q]>>(T v) { return 1; }
intptr mix<T=!<Floats|[Q]>>(T v) { return 2; }

/* MIXED specialized + non-specialized (canon lines 147-153): the specialized
   arm matches first; the unconstrained CATCH-ALL takes what no arm admits.
   each constrained arm deduces against its OWN patterns, so the arms — and
   the catch-all — may differ in shape. */
T mixed<T=int>(T arg) { return 0; }
T mixed<T>(T arg) { return arg; }

intptr shp<T=int>((T,T)^ x) { return 1; }
intptr shp<T=float>(T x) { return 2; }
intptr shp<T>(T x) { return 3; }

/* concrete TYPE SPELLINGS as constraints — each a one-type set. */
intptr tsp<T=int^>(T v) { return 1; }
intptr tsp<T=(int,int)>(T v) { return 2; }
intptr tsp<T>(T v) { return 3; }
intptr tbx<T=Box<int>>(T v) { return 1; }
intptr tbx<T>(T v) { return 2; }
intptr tit<T=int[]>(T v) { return 1; }
intptr tit<T>(T v) { return 2; }

/* the catch-all keeps its own pattern language. */
intptr caf<T=char>(T v) { return 1; }
intptr caf<T>(T^ p) { return 2; }

/* the canon example class; size() is const so the convention's (const T)^
   receiver can call it. */
Thing(int a) {
    intptr const size() {
        return a;
    }
}

int32 main() {
    int iarr[2];
    iarr[0] = 0;
    iarr[1] = 0;
    float farr[3];
    farr[0] = 0.0;
    Thing carr[4];
    int grid[2][3];
    grid[0][0] = 0;

    /* the canon usage shapes. */
    println(String + "iarr    = " + countof(iarr));
    println(String + "farr    = " + countof(farr));
    println(String + "carr    = " + countof(carr));
    println(String + "carr0   = " + countof(carr[0]));
    println(String + "grid    = " + countof(grid));
    println(String + "pair    = " + countof((1, 2)));
    println(String + "triple  = " + countof((1, 2, 3)));

    /* every Primitives corner. */
    int x = 7;
    char c = 'a';
    intptr z = 3;
    int^ p = ^x;
    int[] it = ^iarr[0];
    println(String + "int     = " + countof(x));
    println(String + "float   = " + countof(3.5));
    println(String + "bool    = " + countof(true));
    println(String + "char    = " + countof(c));
    println(String + "intptr  = " + countof(z));
    println(String + "ref     = " + countof(p));
    println(String + "iter    = " + countof(it));

    /* the canon string-literal side effect: char[6] counts the NUL. */
    println(String + "str     = " + countof("hello"));

    /* an explicit type-list selects by membership too; an explicit iterator
       type decays the literal into the Primitives arm. */
    println(String + "expl    = " + countof<int[2]>(iarr));
    println(String + "explit  = " + countof<char[]>("hello"));

    /* aliases shed; the outer const peels. */
    alias Integer = int;
    Integer ai = 3;
    const int ci = 9;
    println(String + "alias   = " + countof(ai));
    println(String + "const   = " + countof(ci));

    Thing t(5);
    println(String + "thing   = " + countof(t));
    println(String + "two     = " + countof(4, 5));

    int8 i8 = 1;
    uint8 u8 = 1;
    uint u = 1;
    println(String + "big1    = " + big(x));
    println(String + "big2    = " + big(i8));
    println(String + "big3    = " + big(u8));
    println(String + "big4    = " + big(u));

    println(String + "weird1  = " + weird(i8));
    println(String + "weird2  = " + weird(3.5));

    println(String + "twos1   = " + twos((1, 2)));
    println(String + "twos2   = " + twos((1.5, 2.5)));
    println(String + "twos3   = " + twos((1, 2, 3)));

    float f = 0.0;
    println(String + "ptrs1   = " + ptrs(p));
    println(String + "ptrs2   = " + ptrs(^f));
    println(String + "ptrs3   = " + ptrs(it));

    Duck d;
    Goat g;
    println(String + "duck    = " + legs(d));
    println(String + "goat    = " + legs(g));

    Box<int> bi(4);
    Box<float> bf(1.5);
    println(String + "boxed1  = " + boxed(bi));
    println(String + "boxed2  = " + boxed(bf));

    fill9(iarr);
    println(String + "fill9   = " + iarr[0]);

    println(String + "pick2a  = " + pick2(1, 2));
    println(String + "pick2b  = " + pick2(d, d));
    println(String + "pick2c  = " + pick2(7));

    /* a body-scope set + constrained templates, like any nested function. */
    alias Small = <int8|uint8>;
    alias NotSmall = !<Small>;
    intptr tiny<T=Small>(T v) { return 1; }
    intptr tiny<T=NotSmall>(T v) { return 2; }
    println(String + "tiny1   = " + tiny(i8));
    println(String + "tiny2   = " + tiny(3));

    /* inline typesets: the canon example (a float LITERAL binds `float`, the
       typeless-decl type), arm selection, the complement, a named set and a
       wildcard pattern inside an inline set, and a body-scope inline arm. */
    int zero = 0;
    float fz = 0.0;
    float f25 = 2.5;
    float64 d35 = 3.5;
    println(String + "iz1     = " + is_zero(zero));
    println(String + "iz2     = " + is_zero(x));
    println(String + "iz3     = " + is_zero(fz));
    println(String + "iz4     = " + is_zero(f25));
    println(String + "inl1    = " + inl(x));
    println(String + "inl2    = " + inl(c));
    println(String + "inl3    = " + inl(f25));
    println(String + "inl4    = " + inl(true));
    println(String + "noz1    = " + noz(x));
    println(String + "noz2    = " + noz(true));
    println(String + "noz3    = " + noz(3.5));
    println(String + "noz4    = " + noz(d35));
    println(String + "mix1    = " + mix(f25));
    println(String + "mix2    = " + mix(iarr));
    println(String + "mix3    = " + mix(d35));
    println(String + "mix4    = " + mix(x));
    intptr insm<T=<int8|uint8>>(T v) { return 1; }
    intptr insm<T=!<int8|uint8>>(T v) { return 2; }
    println(String + "insm1   = " + insm(i8));
    println(String + "insm2   = " + insm(x));

    /* mixed specialized + catch-all (canon): specialized matches first,
       inferred and explicit. */
    println(String + "mx1     = " + mixed(5));
    println(String + "mx2     = " + mixed(f25));
    println(String + "mx3     = " + mixed(true));
    println(String + "mx4     = " + mixed<float>(f25));
    println(String + "mx5     = " + mixed<int>(7));

    /* per-arm deduction across DIFFERENT shapes, catch-all included. */
    (int, int) tt = (1, 2);
    println(String + "shp1    = " + shp(^tt));
    println(String + "shp2    = " + shp(f25));
    println(String + "shp3    = " + shp(true));
    println(String + "shp4    = " + shp<int>(^tt));

    /* type-spelling constraints: pointer, tuple, template instance,
       iterator; the catch-all takes the rest. */
    println(String + "tsp1    = " + tsp(p));
    println(String + "tsp2    = " + tsp((1, 2)));
    println(String + "tsp3    = " + tsp(c));
    println(String + "tbx1    = " + tbx(bi));
    println(String + "tbx2    = " + tbx(bf));
    println(String + "tit1    = " + tit(it));
    println(String + "tit2    = " + tit(x));

    /* the catch-all's own composite pattern. */
    println(String + "caf1    = " + caf(c));
    println(String + "caf2    = " + caf(^x));

    /* a body-scope mixed pair with a primitive-keyword constraint. */
    intptr bmx<T=int>(T v) { return 1; }
    intptr bmx<T>(T v) { return 2; }
    println(String + "bmx1    = " + bmx(x));
    println(String + "bmx2    = " + bmx(f25));

    return 0;
}

/* a T matching two arms is a call-site ambiguity. */
//-EXPECT-ERROR: Ambiguous specialization
//intptr clash<T=[N]>(T v) { return N; }
//intptr clash<T=Arrays>(T v) { return 1; }
//void poke_clash() {
//    int a2[2];
//    a2[0] = 0;
//    a2[1] = 0;
//    intptr n = clash(a2);
//    n = n;
//}

/* a T matching no arm. */
//-EXPECT-ERROR: No specialization
//intptr prim_only<T=Primitives>(T v) { return 1; }
//void poke_nomatch() {
//    Thing t2(1);
//    intptr n = prim_only(t2);
//    n = n;
//}

/* canon: an unused optional-type is a compile error. */
//-EXPECT-ERROR: is not used
//intptr unused_a2<T=A2[N][M]>(T arg) {
//    return N * M;
//}

/* a type set is a constraint, not a type. */
//-EXPECT-ERROR: is a type set, not a type
//void poke_settype() {
//    Primitives bad = 0;
//}

/* TWO unconstrained same-arity siblings still clash — at most one catch-all
   per overlapping range. */
//-EXPECT-ERROR: may not share its name
//intptr uncon<T>(T solo) { return 0; }
//intptr uncon<T>(T dup) { return 1; }

/* the canon ambiguity example: int matches both Integers and int — an exact
   type gets NO priority over a set containing it. */
//-EXPECT-ERROR: Ambiguous specialization
//void error<T=Integers>(T arg) { }
//void error<T=int>(T arg) { }
//void poke_error() {
//    error(5);
//}

/* a catch-all never rescues an ambiguity. */
//-EXPECT-ERROR: Ambiguous specialization
//intptr resq<T=SignedIntegers>(T v) { return 1; }
//intptr resq<T=int>(T v) { return 2; }
//intptr resq<T>(T v) { return 3; }
//void poke_resq() {
//    intptr n = resq(5);
//    n = n;
//}

/* with no catch-all, a shape no arm deduces reports the pattern mismatch. */
//-EXPECT-ERROR: does not match the template pattern
//intptr strict<T=int>((T,T)^ x) { return 1; }
//void poke_strict() {
//    intptr n = strict(true);
//    n = n;
//}

/* a conflicting bare-T binding surfaces through the family path. */
//-EXPECT-ERROR: Conflicting bindings
//void poke_conflict() {
//    intptr n = pick2(1, 2.5);
//    n = n;
//}

/* the constraint owns its template list. */
//-EXPECT-ERROR: single-parameter template list
//intptr multi<T=Primitives, U>(T v, U w) { return 1; }

/* specialization is a free-function feature. */
//-EXPECT-ERROR: not supported on a method template
//intptr Thing:m<T=Primitives>(T v) { return 1; }

/* a binder cannot reuse the parameter name. */
//-EXPECT-ERROR: reuses the template parameter
//intptr selfbind<T=T[N]>(T v) { return N; }

/* binders are distinct. */
//-EXPECT-ERROR: Duplicate binder name
//intptr dupbind<T=A[N][N]>(A arg) { return N; }

/* an inline set overlapping a named one is the same call-site ambiguity. */
//-EXPECT-ERROR: Ambiguous specialization
//intptr iamb<T=<int|bool>>(T v) { return 1; }
//intptr iamb<T=Integers>(T v) { return 2; }
//void poke_iamb() {
//    intptr n = iamb(5);
//    n = n;
//}

/* no arm admits a float64 through the canon inline pair. */
//-EXPECT-ERROR: No specialization
//intptr izonly<T=<int|float>>(T v) { return 1; }
//void poke_izonly() {
//    float64 d = 1.5;
//    intptr n = izonly(d);
//    n = n;
//}

/* a type set binds no type parameters. */
//-EXPECT-ERROR: cannot have a template list
//alias BadSet<Q> = <int|bool>;
