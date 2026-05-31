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
void bar() {
    /* compile error: ambiguous */
    //-EXPECT-ERROR: 'kOops' is ambiguous
    //int err = kOops;

    int good = Bonk1:kOops;
    int fine = Bonk2:kOops;
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

/* an explicit value overflows the underlying type. */
//-EXPECT-ERROR: does not fit declared type 'int8'
//enum int8 Big ( kFine = 1, kBad = 200 );

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
