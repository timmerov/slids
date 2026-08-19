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

ambiguous specialization is currently a compile error.
this decision may be revisited in the future to allow a catch-all
template with no specialization to co-exist with specialized templates.
in that future, specialized templates would match before the
non-specialized template.

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
registration permits the overlap only when BOTH siblings are constrained.
the call deduces T (the bare-T binding), then membership selects the arm:
no match is "No specialization", more than one is "Ambiguous specialization"
(a CALL-site check; there is no declaration-time disjointness pass). an
unconstrained same-arity sibling still clashes (no catch-all yet). a
`T=Name` constraint may also name a concrete type — a one-type set is a
full specialization. arity selection and plain-beats-template are unchanged
upstream; a different-arity unconstrained sibling coexists as before.

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

/* an unconstrained SAME-arity sibling still clashes (no catch-all yet). */
//-EXPECT-ERROR: may not share its name
//intptr countof<T>(T solo) { return 0; }

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

/* a type set binds no type parameters. */
//-EXPECT-ERROR: cannot have a template list
//alias BadSet<Q> = <int|bool>;
