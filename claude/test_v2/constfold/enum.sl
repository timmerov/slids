/*
test enums.

enums may be defined in any scope where constants may be defined.

the default type for enum is int.
values outside the range of int require an explicit type.

the name is optional.
if no name is specified, the enum constants are declared in the enclosing scope.

the fully qualified name must be used.

todo: land aliases.
the enum qualifier may be removed with alias.

enum conceptually/mechanically resolves to a type alias and a namespace.

    enum int Demo ( kMix, kTape );
    -->
    alias int Demo;
    Demo /*namespace*/ {
        const Demo /*aliased type*/ kMix = 0;
        const Demo /*aliased type*/ kTape = 1;
    };

    enum ( kBare, kNaked );
    -->
    const int kBare = 0;
    const int kNaked = 1;

future todo:
re-open enums.
names cannot collide.
tbd: the constant list restarts at 0 ?
the first constant must have an assigned value ?
*/

enum (
    kUnnamed1,
    kUnnamed2,
    kUnnamed3
);

enum /*int*/ Direction (
    kNorth,
    kSouth,
    kEast,
    kWest
);

/*
a member's explicit init is a qualified reference to another enum's member;
the following implicit member continues from it (auto-increment clones the
qualified anchor, so the clone must carry the qualifier).
*/
enum Mapped (
    kBase = Direction:kWest,
    kNext
);

/*
a member's explicit init references a file-scope const. the const is visible
even though the enum registers early — inits resolve in a later pass. forward
references work too (the const may be declared below the enum).
*/
const int kSix = 6;
enum FromConst (
    kVal = kSix,    // 6
    kNextVal        // 7, auto-increment from the const anchor
);
enum FwdConst ( kFwd = kEight );
const int kEight = 8;

/* qualifier gap: a member init references a sibling member bare. */
enum Sib ( s0, s1 = s0 );

/* the same, inside an expression — the canonical C idiom. */
enum Arith ( aA = 5, aB = aA + 1 );

/*
a bare-sibling anchor with a trailing implicit member: c2 continues from c1's
clone (the third anchor kind, alongside Mapped's qualified ref and FromConst's
const ref).
*/
enum Chain ( c0, c1 = c0, c2 );

/* a plain literal anchor with a trailing implicit member. */
enum Lit ( l0 = 5, l1 );

/* a wide underlying holds a value beyond int32's range. */
enum int64 Wide ( wBig = 5000000000 );

/* float member inits referencing a file-scope const, including a forward ref. */
const float fPi = 3.5;
enum float FConst ( fA = fPi, fB );
enum float FFwd ( fF = fLater );
const float fLater = 9.5;

/* reverse direction: a file-scope const init reads an enum member. */
const int kFromMember = Direction:kEast;

/* option A: a forward sibling ref resolves (all members register first). */
enum Fwd2 ( q0 = q1, q1 = 1 );

/* a valid enum bool: members in range {0,1}. */
enum bool Flag ( off, on );

/*
a signed underlying holds a negative explicit value; the implicit member
auto-increments from the negative anchor.
*/
enum int8 Temp ( cold = -5, mild );

/* two explicit inits interspersed — the running counter resets twice. */
enum Multi ( m0 = 1, m1, m2 = 10, m3 );

/* a char underlying with char-literal member values. */
enum char Key ( a = 'A', b = 'Z' );

/* an unsigned underlying. */
enum uint8 U ( u0 = 200, u1 );

/* an aliased underlying type resolves through the alias chain to int8. */
alias Byte = int8;
enum Byte Small ( sa = 1, sb );

/* an anonymous enum with an explicit init — bare consts continue from it. */
enum ( az = 100, bz );

/*
an enum may be a namespace member. From outside, `Geo:Dir` is a qualified type
and `Geo:Dir:member` a qualified value. Inside Geo, Dir is bare: a sibling const
may read a member (Dir:east) and a const may be Dir-typed. An anonymous nested
enum's members (red/green/blue) become members of Geo itself.
*/
Geo {
    enum Dir ( north, south, east, west );   // 0,1,2,3
    const int ax = Dir:east;                 // 2 — in-namespace member ref
    const Dir corner = Dir:west;             // 3 — enum used as a type in Geo
    enum ( red, green, blue );               // 0,1,2 — members of Geo
}

/* order-independence: a const member's type names an enum declared below it. */
Ord {
    const Mode m = Mode:hi;                  // 1
    enum Mode ( lo, hi );
}

/* a qualified type used as a namespace member's type (cross-namespace). */
Ref {
    const Geo:Dir d = Geo:Dir:east;          // 2
}

/* deeper nesting: a 3-segment qualified type + 4-segment qualified value. */
Outer {
    Inner {
        enum Deep ( d0, d1 );
    }
}

void foo() {
    enum Language (
        kSlids,
        kCPP,
        kRust
    );

    Language lang = Language:kCPP;
    __println("lang = " + lang);

    /*
    reach goal: drop the qualifier in context.
    the type of the assigned variable is Language.
    the statement could get an implied alias Language.
    which ends at the semicolon.
    */
    /*
    Language best = kRust;
    best = kSlids;
    */
}

enum Bonk1 ( kOops );
enum Bonk2 ( kOops );
alias Bonk1;
alias Bonk2;
int bar() {
    /* compile error: ambiguous */
    //-EXPECT-ERROR: 'kOops' is ambiguous
    //int err = kOops;

    int good = Bonk1:kOops;
    int fine = Bonk2:kOops;
    return good + fine;
}

/*
anonymous enum in a function: members land as bare consts in the body frame,
default type int (usable in int arithmetic).
*/
int32 anon_local() {
    enum ( kLocalA, kLocalB, kLocalC );
    __println("kLocalA = " + kLocalA);
    __println("kLocalC = " + kLocalC);
    int sum = kLocalA + kLocalB + kLocalC;   // default int: plain int arithmetic
    __println("local sum = " + sum);
    return 0;
}

int32 main() {

    /* file-scope anonymous enum: all three members, auto-incremented from 0. */
    __println("kUnnamed1 = " + kUnnamed1);
    __println("kUnnamed2 = " + kUnnamed2);
    __println("kUnnamed3 = " + kUnnamed3);

    int u = anon_local();
    __println("u = " + u);

    enum float Consts (
        kPi = 3.14,
        kE = 2.718
    );

    __println("kNorth = " + Direction:kNorth);
    __println("kWest = " + Direction:kWest);
    __println("kPi = " + Consts:kPi);
    __println("kE = " + Consts:kE);

    __println("kBase = " + Mapped:kBase);
    __println("kNext = " + Mapped:kNext);

    __println("kVal = "     + FromConst:kVal);
    __println("kNextVal = " + FromConst:kNextVal);
    __println("kFwd = "     + FwdConst:kFwd);

    /* block-scope sibling ref. */
    enum BlockSib ( b0, b1 = b0 );
    __println("b0 = " + BlockSib:b0);
    __println("b1 = " + BlockSib:b1);

    __println("s0 = "   + Sib:s0);
    __println("s1 = "   + Sib:s1);
    __println("aA = "   + Arith:aA);
    __println("aB = "   + Arith:aB);
    __println("c0 = "   + Chain:c0);
    __println("c1 = "   + Chain:c1);
    __println("c2 = "   + Chain:c2);
    __println("l0 = "   + Lit:l0);
    __println("l1 = "   + Lit:l1);
    __println("wBig = " + Wide:wBig);
    __println("fA = "   + FConst:fA);
    __println("fB = "   + FConst:fB);
    __println("fF = "   + FFwd:fF);
    __println("kFromMember = " + kFromMember);
    __println("q0 = "   + Fwd2:q0);
    __println("q1 = "   + Fwd2:q1);
    __println("off = "  + Flag:off);
    __println("on = "   + Flag:on);
    __println("cold = " + Temp:cold);
    __println("mild = " + Temp:mild);
    __println("m0 = "   + Multi:m0);
    __println("m1 = "   + Multi:m1);
    __println("m2 = "   + Multi:m2);
    __println("m3 = "   + Multi:m3);

    /* enums as namespace members + qualified type names. */
    __println("Geo:Dir:north = " + Geo:Dir:north);
    __println("Geo:Dir:west = "  + Geo:Dir:west);
    __println("Geo:ax = "        + Geo:ax);
    __println("Geo:corner = "    + Geo:corner);
    __println("Geo:red = "       + Geo:red);
    __println("Geo:blue = "      + Geo:blue);
    __println("Ord:m = "         + Ord:m);
    Geo:Dir gx = Geo:Dir:south;   // a qualified type name in a declaration
    __println("gx = " + gx);

    __println("Key:a = "    + Key:a);
    __println("Key:b = "    + Key:b);
    __println("U:u0 = "     + U:u0);
    __println("U:u1 = "     + U:u1);
    __println("Small:sa = " + Small:sa);
    __println("Small:sb = " + Small:sb);
    __println("az = "       + az);
    __println("bz = "       + bz);
    __println("Ref:d = "    + Ref:d);
    Outer:Inner:Deep dv = Outer:Inner:Deep:d1;   // 3-segment qualified type
    __println("dv = "       + dv);
    __println("deep0 = "    + Outer:Inner:Deep:d0);

    alias Direction;
    __println("kNorth = " + kNorth);

    //-EXPECT-ERROR: 'kPi' needs a namespace qualifier
    //__println("kPi = " + kPi);

    return 0;
}

/*
file-scope negative cases — whole enum decls, so they sit outside main
(one //-block uncommented per run).
*/

/* an auto-incremented member overflows the underlying type. */
//-EXPECT-ERROR: does not fit declared type 'bool'
//enum bool TooMany ( kA, kB, kC );

/* an explicit member value falls below the bool range {0,1}. */
//-EXPECT-ERROR: does not fit declared type 'bool'
//enum bool Neg ( a = -1 );

/* an explicit value overflows the underlying type. */
//-EXPECT-ERROR: does not fit declared type 'int8'
//enum int8 Big ( kFine = 1, kBad = 200 );

/* an explicit float value exceeds the float32 underlying. */
//-EXPECT-ERROR: does not fit declared type 'float'
//enum float FBig ( a = 1e40 );

/* two members of one enum collide. */
//-EXPECT-ERROR: Duplicate declaration of 'kX'
//enum Dup ( kX, kX );

/* the underlying type is not a known type. */
//-EXPECT-ERROR: Unknown type 'Nope'
//enum Nope Bad ( kA );

/* a member initialized to a non-constant (a runtime variable). */
//-EXPECT-ERROR: is not a constant expression
//int32 nonconst_var() {
//    int x = 7;
//    enum E ( kA = x );
//    return 0;
//}

/* a member initialized to a non-constant (a function call). */
//-EXPECT-ERROR: is not a constant expression
//int32 nonconst_call() {
//    enum E ( kA = helper() );
//    return 0;
//}
//int helper() { return 1; }

/*
a member initialized to a string — a constant, but the wrong type for the
int underlying. distinguished from the non-constant cases above.
*/
//-EXPECT-ERROR: has a string initializer, which does not match declared type 'int'
//enum Stringy ( kA = "x" );

/* a const and an enum member that reference each other — a cycle. */
//-EXPECT-ERROR: is not a constant expression
//const int cyc_x = Cyc:ca;
//enum Cyc ( ca = cyc_x );

/* two members of one enum reference each other — a cycle. */
//-EXPECT-ERROR: is not a constant expression
//enum Cyc2 ( a = b, b = a );

/* a qualified type whose head is not a namespace. */
//-EXPECT-ERROR: is not a namespace
//int32 qNs() { Nope:Dir x = Geo:Dir:north; return 0; }

/* a qualified type with a non-existent intermediate namespace segment. */
//-EXPECT-ERROR: is not a namespace member of 'Geo'
//int32 qMember() { Geo:Missing:Dir x = Geo:Dir:north; return 0; }

/* a qualified type whose leaf names a value, not a type. */
//-EXPECT-ERROR: is not a type in 'Geo'
//int32 qType() { Geo:ax x = Geo:Dir:north; return 0; }

/* a namespaced enum member referenced bare needs a qualifier. */
//-EXPECT-ERROR: needs a namespace qualifier
//int32 qBare() { int z = north; return 0; }
