/*
test suppressing the unreachable code diagnostic.

    if (0) { void; }
    for () (false) { void; } { void; }
    for (var : any-flavor) { void; }
    while (false) { void; }
    switch (const-value) { not-const-value { void; } }
    if (true) { } else if (cond) { void; }

the void; statement is a no-op.
it may be used anywhere in a runtime code block.
including code blocks that are reachable.
the only side effect it has is to artificially mark unreachable code as reachable.
it has no effect on uninitialized variables, unused variables, or any other error.
use of void; as a statement outside of a runtime code block is a compile error.

    /* compile errors */
    Class( field-list ) { void; }
    switch (value) { void; }
    import { void; }
    Namespace { void; }
    global (var-list) { void; }
    Template<type-list>( field-list ) { void; }

notes:
if-else chains need to be suppressible.
so a void; statement in the code block of a chained if suppresses the diagnostic
for the else clause.
there's no other place to put the void;.

the update blocks of short-for loops should never be tagged as unreachable.
they are synthesized by the compiler and are outside the reach of the author.

this is a weird case that's useful for debugging.
so it's allowed.

    void function() {
        /* code */

        /* temporary exit for debugging. */
        return;
        /* suppress the unreachable code diagnostic. */
        void;

        /* more code temporarily unreachable. */
    }

nicety:
the unreachable code diagnostic may suggest using void; in the code block to
suppress the error.
*/

/*
claude says:

the suppression is per code block: a top-level void; marks the block that
directly contains it, so a nested dead branch needs its own marker.

an if-else chain gets ONE report; a void; in any block along the chain
suppresses it.

a suppressed dead tail after a return is resolved and type-checked like live
code (so unused/uninitialized/etc. still fire), then dropped before codegen —
nothing may be emitted after a terminator. function definitions in the dead
tail are never dropped; a live call above the return may target one.

switch dead-arm analysis (constant scrutinee): entry is the matching clause,
else the default; a trailing continue carries reachability into the next
clause; everything else is dead. no analysis if any label is invalid.

a constant-false long-for condition kills the update block and the body block
independently — each takes its own void;. a constant-true condition is not
flagged (same stance as while). a post-condition while runs its body once, so
it is never flagged.

the ranged/class for flavors reject an empty constant range outright ("Invalid
range.") — a different error; void; does not touch it.

the diagnostic carries the suggestion note.
*/

import string;

/* the debugging early-exit: the dead tail is compiled and checked, then
   dropped. */
int32 debug_exit() {
    println(String + "debug_exit: before the return");
    return 7;
    void;
    println(String + "debug_exit: dead tail");
    return 8;
}

int32 const_if(int32 x) {
    if (0) { void; println(String + "const_if: dead then"); }
    if (1) { x = x + 1; } else { void; println(String + "const_if: dead else"); }
    /* nested: each block carries its own marker. */
    if (0) { void; if (0) { void; x = 99; } }
    /* anywhere in the block: the marker may follow the dead code. */
    if (0) { x = 50; void; }
    /* an empty dead block has nothing to flag — no marker needed. */
    if (0) { }
    return x;
}

/* the marker may precede the abrupt statement — anywhere in the block. */
int32 marker_first() {
    void;
    return 5;
    println(String + "marker_first: dead");
}

/* a function definition in the dead tail stays compiled — the live call above
   the return targets it. */
int32 def_in_dead_tail() {
    return helper();
    void;
    int32 helper() { return 21; }
}

int32 chain(int32 x) {
    /* the chain's report is suppressed by the void; in the chained if's block. */
    if (1) { x = x + 10; } else if (x > 0) { void; x = x + 100; }
    return x;
}

int32 loops() {
    int32 n = 0;
    while (false) { void; n = 1; }
    for () (false) { void; n = 2; } { void; n = 3; }
    /* post-condition while: the body runs once — reachable, no marker needed. */
    while { n = n + 4; } (false);
    /* void; is a no-op in reachable code, in every for flavor. */
    for (i : 0..3) { void; n = n + i; }
    int32 arr[2];
    arr[0] = 5; arr[1] = 6;
    for (a : arr) { void; n = n + a; }
    for (t : (7, 8)) { void; n = n + t; }
    return n;
}

int32 const_switch() {
    int32 r = 0;
    const kSel = 2;
    switch (kSel) {
        1: { void; r = 1; }
        2: { r = 2; }
        default: { void; r = 9; }
    }
    switch (kSel) {
        2: { r = r + 10; } continue;
        /* reachable through the trailing continue — no marker needed. */
        3: { r = r + 100; }
        /* 3 has no trailing continue: dead again. */
        4: { void; r = r + 1000; }
    }
    return r;
}

int32 no_match() {
    const kNone = 5;
    int32 r = 0;
    /* nothing matches, no default: every clause is dead. */
    switch (kNone) {
        1: { void; r = 1; }
        2: { void; r = 2; }
    }
    /* nothing matches: the default is the entry. */
    switch (kNone) {
        1: { void; r = r + 1; }
        default: { r = 42; }
    }
    return r;
}

int32 main() {
    void;
    println(String + "debug_exit() = " + debug_exit());        // 7
    println(String + "const_if(1) = " + const_if(1));          // 2
    println(String + "chain(1) = " + chain(1));                // 11
    println(String + "marker_first() = " + marker_first());    // 5
    println(String + "def_in_dead_tail() = " + def_in_dead_tail());  // 21
    println(String + "loops() = " + loops());                  // 33
    println(String + "const_switch() = " + const_switch());    // 112
    println(String + "no_match() = " + no_match());            // 42
    return 0;
}

/* ------------------------------------------------------------------ */
/* negatives                                                           */

// the diagnostic still fires without the marker.
//-EXPECT-ERROR: Unreachable statement.
//int32 neg_const_if() {
//    if (0) { println(String + "dead"); }
//    return 0;
//}

//-EXPECT-ERROR: Unreachable statement.
//int32 neg_dead_tail() {
//    return 0;
//    println(String + "dead");
//}

// the suppression is per block: the outer void; does not cover the inner if.
//-EXPECT-ERROR: Unreachable statement.
//int32 neg_nested() {
//    if (0) { void; if (0) { println(String + "inner"); } }
//    return 0;
//}

//-EXPECT-ERROR: Unreachable statement.
//int32 neg_while() {
//    while (false) { println(String + "dead"); }
//    return 0;
//}

//-EXPECT-ERROR: Unreachable statement.
//int32 neg_forlong_body() {
//    for () (false) { void; } { println(String + "dead"); }
//    return 0;
//}

// the long-for update block is author code: it flags on its own.
//-EXPECT-ERROR: Unreachable statement.
//int32 neg_forlong_update() {
//    for () (false) { println(String + "dead update"); } { void; }
//    return 0;
//}

//-EXPECT-ERROR: Unreachable statement.
//int32 neg_chain() {
//    int32 x = 1;
//    if (1) { x = 2; } else if (x > 0) { x = 3; }
//    return x;
//}

//-EXPECT-ERROR: Unreachable statement.
//int32 neg_switch_arm() {
//    const kSel = 2;
//    int32 r = 0;
//    switch (kSel) {
//        1: { r = 1; }
//        2: { r = 2; }
//    }
//    return r;
//}

// void; marks reachability only — every other diagnostic still fires.
//-EXPECT-ERROR: Unused local variable
//int32 neg_unused() {
//    if (0) { void; int32 t; }
//    return 0;
//}

//-EXPECT-ERROR: uninitialized
//int32 neg_uninit() {
//    int32 x;
//    return 0;
//    void;
//    return x;
//}

// void; outside a runtime code block is a compile error: class body.
//-EXPECT-ERROR: A class body holds
//NegClass( int32 a_ ) { void; }

// switch block (between clauses, not inside a case body).
//-EXPECT-ERROR: A type is not an expression
//int32 neg_switch_block(int32 v) {
//    switch (v) { void; }
//    return 0;
//}

// import block.
//-EXPECT-ERROR: Expected
//import { void; }

// namespace body.
//-EXPECT-ERROR: Expected
//NegSpace { void; }

// global block.
//-EXPECT-ERROR: A global group body holds
//global ( int32 x_ = 0 ) { void; }

// template class body.
//-EXPECT-ERROR: A class body holds
//NegTmpl<T>( int32 a_ ) { void; }
