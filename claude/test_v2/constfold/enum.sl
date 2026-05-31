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
