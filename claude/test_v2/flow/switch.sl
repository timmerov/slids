/*
test switch statements.

switch keyword, value tuple, code block of case statements.
case statement is a label-list, code block, optional continue and semicolon.
label-list is const-expression or default keyword, colon, repeated.

    switch (value) {
        const-expr: { }
        const-expr:
        const-expr: { }
        const-expr: { } continue;
        default: { }
    }

value clause may not be empty.
the code body may be empty.
case const-expressions must be unique.
execution ends at the case code block unless there's a trailing continue.
then the code falls through to the next case statement.
default is optional.
default is singular.
trailing continue is optional.

switch statememts embedded in for/while loops:

    while (cond) {
        switch (value) {
        0: {
            /*
            continue inside the case code block continues
            the enclosing loop.
            all continue forms: naked, numbered, named.
            */
            continue;
            }
        1: 2: 3: {
            /*
            continue after the case code block falls through
            to the next case statement.
            always bare.
            */
            } continue;
        default: {
            }
        }
    }

case must be able to handle qualified names.

    enum Dir ( N, S, E, W );
    Dir d = N;
    switch (d) {
    Dir:N: {
        }
    }

formatting is up to the author.
some suggestions:

    switch (value) {
        0: { ... }
        1: 2: { ... }
        default: { ... }
    }

    switch (value) {
        0: {
            ...
        }
        1:
        2: {
            ...
        }
        default: {
            ...
        }
    }

    switch (value) {
        default: {
            ...
        } 0: {
            ...
        } 1: 2: {
            ...
        }
    }


notes:
should we be able to define things at the top of the switch body?
the things could be used within the case statements.
aspirational.
for example:

    switch (value) {
        int x = 42;
        const float kPi = 3.14;
        Class (int a) { }

        0: {
            Class cls(x);
        }
    }
*/

/*
claude says:

GRAMMAR — no `case` keyword. A clause is a LABEL-LIST + a mandatory `{ }` body
block + an optional trailing `continue;`:
    label (: label)* : { body } [continue;]
- kSwitchStmt children[0]=scrutinee, [1..]=kCaseClause. A clause's children are
  [label0 .. labelK, body]: each label is a const-expr (null = a `default`
  label, mixable into a list), children.back() = the body kBlockStmt. The clause
  node's text == "continue" marks a trailing fall-through.
- label-list (`1: 2: 3: { }`) = "any of these values share this body" — distinct
  from fall-through. QUALIFIED enum-member labels (`Dir:N:`) work via the
  terminator-aware qualified-name parse (scan the maximal `:`-ident chain; the
  trailing `:` is the terminator). The body is always `{`-led, so a single
  qualified label needs no rewind. (Limitation: two ADJACENT qualified/ident
  labels in one list — `Dir:N: Dir:S:` — greedily merge; the tests don't stack
  them. Bare/numeric label-lists are unambiguous.)

CONTROL — no implicit fall-through and no `break`:
- a clause exits the switch at its body's `}`. The only fall-through is a
  trailing `} continue;`, which carries control into the NEXT clause (a trailing
  continue on the LAST clause falls off the bottom = exits; it is allowed and
  inert).
- `break`/`continue` inside a body bind to the ENCLOSING LOOP — switch is fully
  transparent (no switch frame is pushed; it is not a break target). A naked
  break/continue with no enclosing loop is an error. The trailing `continue;` is
  clause metadata, not a kContinueStmt (no loop binding).

ANALYSIS:
- scrutinee must be integer-class (int/uint/char/bool/enum); float rejected.
  Labels must be unique constants of the scrutinee type; default singular, may
  sit anywhere.
- DA: each clause body ENTERS from S (every clause is a direct dispatch target).
  A clause that completes Normally is an exit path unless it carries a trailing
  continue into a non-last clause. after = ∩ of the exit paths (+ S for the
  no-match path when default-less/empty).
- return-terminator iff it has a default, no clause has an (escaping) break, and
  EVERY clause's exit reaches a return — a non-returning clause is acceptable
  only with a trailing continue into a later clause (the chain must end in a
  return).
*/

/* a named enum whose members are referenced by qualified name in case labels. */
enum Dir ( N, S, E, W );

/* an anonymous enum → bare int consts, also usable as case labels. */
enum ( kRed, kGreen, kBlue );

/* a char-underlying enum — a char-class scrutinee with qualified labels. */
enum char Letter ( a = 'a', b = 'b', c = 'c' );

/* a named constant usable as a case label (constfold substitutes it). */
const int kThreshold = 42;

/* basic match + default. */
int basic(int v) {
    switch (v) {
        0: { return 10; }
        1: { return 20; }
        default: { return 99; }
    }
}

/* fall-through: a clause with a trailing continue falls into the next. */
int fallthrough(int v) {
    int r = 0;
    switch (v) {
        0: {
            r = r + 1;
        } continue;
        1: {
            r = r + 10;
        } continue;
        2: {
            r = r + 100;
        }
        default: {
            r = r + 1000;
        }
    }
    return r;
}

/* stacked labels: a label-list shares one clause body. */
int stacked(int v) {
    int r = 0;
    switch (v) {
        3: 4: { r = 34; }
        default: { r = 0; }
    }
    return r;
}

/* a label-list whose shared body returns makes the switch exhaustive: every
   value returns, so the switch is a return-terminator and the function needs no
   trailing return. */
int stacked_terminator(int v) {
    switch (v) {
        3: 4: { return 1; }
        default: { return 0; }
    }
}

/* a default-less switch: an unmatched value is a no-op. */
int no_default(int v) {
    int r = 7;
    switch (v) {
        0: { r = 0; }
        1: { r = 1; }
    }
    return r;
}

/* default need not be last; here it falls through into a later clause. */
int default_middle(int v) {
    int r = 0;
    switch (v) {
        0: {
            r = 1;
        }
        default: {
            r = 9;
        } continue;
        1: {
            r = r + 100;
        }
    }
    return r;
}

/* a clause ends at its block — no fallthrough — then control returns to the loop. */
int switch_in_while(int n) {
    int count = 0;
    int i = 0;
    while (i < n) {
        switch (i) {
            0: {
                count = count + 1;
            }
            default: {
                count = count + 10;
            }
        }
        ++i;
    }
    return count;
}

/* a continue inside a clause body continues the enclosing loop. */
int switch_continue(int n) {
    int count = 0;
    int i = 0;
    while (i < n) {
        ++i;
        switch (i) {
            2: {
                continue;
            }
            default: {
                count = count + 1;
            }
        }
    }
    return count;
}

/* nested switches. */
int nested_switch(int a, int b) {
    int r = 0;
    switch (a) {
        0: {
            switch (b) {
                0: {
                    r = 1;
                }
                default: {
                    r = 2;
                }
            }
        }
        default: {
            r = 9;
        }
    }
    return r;
}

/* a char scrutinee; every case + default returns, so the switch is exhaustive
   and the function needs no trailing return (return-correctness terminator). */
int char_scrutinee(char c) {
    switch (c) {
        'a': { return 1; }
        'b': { return 2; }
        default: { return 0; }
    }
}

/* a bool scrutinee. */
int bool_scrutinee(bool b) {
    switch (b) {
        true: { return 1; }
        default: { return 0; }
    }
}

/* QUALIFIED enum-member labels — `Dir:N:` (the v1 `:`-collision case). */
int dir_switch(Dir d) {
    switch (d) {
        Dir:N: { return 1; }
        Dir:S: { return 2; }
        Dir:E: { return 3; }
        default: { return 0; }
    }
}

/* anonymous-enum bare members as case labels. */
int anon_enum_switch(int v) {
    switch (v) {
        kRed: { return 1; }
        kGreen: { return 2; }
        default: { return 0; }
    }
}

/* definite assignment: r is assigned on every clause AND default, so it is
   definitely assigned after the switch. */
int da_exhaustive(int v) {
    int r;
    switch (v) {
        default: {
            r = 3;
        } 0: {
            r = 1;
        } 1: {
            r = 2;
        }
    }
    return r;
}

/* an empty switch body. */
int empty_body(int v) {
    int r = 5;
    switch (v) {
    }
    return r;
}

/* an arbitrary expression scrutinee + a folded constant case label. */
int expr_scrutinee(int a, int b) {
    switch (a + b) {
        1 + 2: { return 1; }
        default: { return 0; }
    }
}

/* (A) qualified enum-member labels followed by an IDENTIFIER-led body (assign),
   which exercises the terminator-aware label parse's rewind. */
int dir_assign(Dir d) {
    int r = 0;
    switch (d) {
        Dir:N: { r = 1; }
        Dir:S: { r = 2; }
        default: { r = 9; }
    }
    return r;
}

/* PPID in the scrutinee: i++ is read as the value, then bumps once. */
int ppid_scrutinee() {
    int i = 5;
    int r = 0;
    switch (i++) {
        5: { r = 1; }
        default: { r = 2; }
    }
    return r * 100 + i;
}

/* negative-valued case labels. */
int neg_labels(int v) {
    int r = 0;
    switch (v) {
        -1: { r = 1; }
        -2: { r = 2; }
        default: { r = 9; }
    }
    return r;
}

/* a uint scrutinee. */
int uint_scrutinee(uint v) {
    switch (v) {
        0: { return 1; }
        1: { return 2; }
        default: { return 9; }
    }
}

/* an int64 scrutinee. */
int i64_scrutinee(int64 v) {
    switch (v) {
        100: { return 1; }
        default: { return 0; }
    }
}

/* a char-underlying enum scrutinee with qualified labels. */
int letter_switch(Letter x) {
    switch (x) {
        Letter:a: { return 1; }
        Letter:b: { return 2; }
        default: { return 0; }
    }
}

/* a named constant as a case label. */
int const_label(int v) {
    switch (v) {
        kThreshold: { return 1; }
        default: { return 0; }
    }
}

/* a case label at the scrutinee type's boundary — 255 fits a char (0..255). */
int char_max(char c) {
    switch (c) {
        255: { return 1; }
        0: { return 2; }
        default: { return 9; }
    }
}

/* a trailing continue on the LAST clause falls off the bottom = exits the switch.
   it is allowed and behaves identically to omitting it. */
int last_continue(int v) {
    int r = 0;
    switch (v) {
        default: { r = 9; }
        0: { r = 1; } continue;
    }
    return r;
}

/* a trailing continue after a RETURNING last clause is statically unreachable but
   inert: not a compile error. Every clause returns, so the switch is still a
   return-terminator and the function needs no trailing return. */
int last_continue_return(int v) {
    switch (v) {
        0: { return 1; }
        default: { return 9; } continue;
    }
}

/* a non-returning clause with a trailing continue falls into a returning clause,
   so every path returns — the switch is a return-terminator via the fall-through
   chain (no trailing return needed). */
int continue_terminator(int v) {
    int r = 0;
    switch (v) {
        0: { r = 1; } continue;
        default: { return r + 10; }
    }
}

/* a class instance declared in a clause body: the body is an ordinary block
   scope, so its dtor fires at the body's `}` when (and only when) that clause is
   taken — no cross-clause lifetime, no constructed-flag. */
Tracer(int id_) {
    _() { __println("Tracer ctor " + id_); }
    ~() { __println("Tracer dtor " + id_); }
}
void clause_scope(int v) {
    switch (v) {
        0: {
            Tracer t(0);
            __println("clause 0 body");
        }
        default: {
            __println("default body");
        }
    }
    __println("after switch");
}

/* a label-list shared body with a trailing continue: 1 or 2 run the shared body,
   then fall through into the next clause. */
int list_continue(int v) {
    int r = 0;
    switch (v) {
        1: 2: { r = r + 1; } continue;
        3: { r = r + 10; }
        default: { r = r + 100; }
    }
    return r;
}

/* a default label mixed into a label-list: value 0 OR any unmatched value share
   the one body. */
int default_in_list(int v) {
    int r = 0;
    switch (v) {
        0: default: { r = 1; }
        1: { r = 2; }
    }
    return r;
}

/* fall-through from a value clause INTO the default clause (default need not be
   last). */
int into_default(int v) {
    int r = 0;
    switch (v) {
        0: { r = 1; } continue;
        default: { r = r + 50; }
        1: { r = 2; }
    }
    return r;
}

/* an empty clause body with a trailing continue still falls into the next clause
   (without the continue it would exit with r = 0). */
int empty_continue(int v) {
    int r = 0;
    switch (v) {
        0: { } continue;
        default: { r = 7; }
    }
    return r;
}

int32 main() {
    __println("basic(1) = " + basic(1));                    // 20
    __println("basic(5) = " + basic(5));                    // 99
    __println("fallthrough(0) = " + fallthrough(0));        // 111
    __println("fallthrough(1) = " + fallthrough(1));        // 110
    __println("fallthrough(2) = " + fallthrough(2));        // 100
    __println("fallthrough(9) = " + fallthrough(9));        // 1000
    __println("stacked(3) = " + stacked(3));                // 34
    __println("stacked(4) = " + stacked(4));                // 34
    __println("stacked(5) = " + stacked(5));                // 0
    __println("stacked_terminator(3) = " + stacked_terminator(3));  // 1
    __println("stacked_terminator(0) = " + stacked_terminator(0));  // 0
    __println("no_default(0) = " + no_default(0));          // 0
    __println("no_default(9) = " + no_default(9));          // 7
    __println("default_middle(0) = " + default_middle(0));  // 1
    __println("default_middle(1) = " + default_middle(1));  // 100
    __println("default_middle(5) = " + default_middle(5));  // 109
    __println("switch_in_while(3) = " + switch_in_while(3));    // 21
    __println("switch_continue(4) = " + switch_continue(4));    // 3
    __println("nested_switch(0, 0) = " + nested_switch(0, 0));  // 1
    __println("nested_switch(0, 5) = " + nested_switch(0, 5));  // 2
    __println("nested_switch(5, 0) = " + nested_switch(5, 0));  // 9
    __println("char_scrutinee('a') = " + char_scrutinee('a'));  // 1
    __println("char_scrutinee('z') = " + char_scrutinee('z'));  // 0
    __println("bool_scrutinee(true) = " + bool_scrutinee(true));    // 1
    __println("bool_scrutinee(false) = " + bool_scrutinee(false));  // 0
    __println("dir_switch(Dir:S) = " + dir_switch(Dir:S));          // 2
    __println("dir_switch(Dir:W) = " + dir_switch(Dir:W));          // 0
    __println("anon_enum_switch(kGreen) = " + anon_enum_switch(kGreen));  // 2
    __println("da_exhaustive(0) = " + da_exhaustive(0));    // 1
    __println("da_exhaustive(9) = " + da_exhaustive(9));    // 3
    __println("empty_body(7) = " + empty_body(7));          // 5
    __println("expr_scrutinee(1, 2) = " + expr_scrutinee(1, 2));    // 1
    __println("expr_scrutinee(5, 5) = " + expr_scrutinee(5, 5));    // 0
    __println("dir_assign(Dir:N) = " + dir_assign(Dir:N));          // 1
    __println("dir_assign(Dir:E) = " + dir_assign(Dir:E));          // 9
    __println("ppid_scrutinee() = " + ppid_scrutinee());            // 106
    __println("neg_labels(-1) = " + neg_labels(-1));                // 1
    __println("neg_labels(-2) = " + neg_labels(-2));                // 2
    __println("neg_labels(0) = " + neg_labels(0));                  // 9
    __println("uint_scrutinee(1) = " + uint_scrutinee(1));          // 2
    __println("i64_scrutinee(100) = " + i64_scrutinee(100));        // 1
    __println("i64_scrutinee(5) = " + i64_scrutinee(5));            // 0
    __println("letter_switch(Letter:b) = " + letter_switch(Letter:b));  // 2
    __println("letter_switch(Letter:c) = " + letter_switch(Letter:c));  // 0
    __println("const_label(42) = " + const_label(42));              // 1
    __println("const_label(0) = " + const_label(0));                // 0
    char hi = 255;
    __println("char_max(255) = " + char_max(hi));                   // 1
    __println("last_continue(0) = " + last_continue(0));            // 1
    __println("last_continue(5) = " + last_continue(5));            // 9
    __println("last_continue_return(0) = " + last_continue_return(0));   // 1
    __println("last_continue_return(5) = " + last_continue_return(5));   // 9
    __println("continue_terminator(0) = " + continue_terminator(0));     // 11
    __println("continue_terminator(5) = " + continue_terminator(5));     // 10
    clause_scope(0);    // Tracer ctor 0 / clause 0 body / Tracer dtor 0 / after switch
    clause_scope(9);    // default body / after switch
    __println("list_continue(1) = " + list_continue(1));            // 11
    __println("list_continue(2) = " + list_continue(2));            // 11
    __println("list_continue(3) = " + list_continue(3));            // 10
    __println("list_continue(9) = " + list_continue(9));            // 100
    __println("default_in_list(0) = " + default_in_list(0));        // 1
    __println("default_in_list(1) = " + default_in_list(1));        // 2
    __println("default_in_list(9) = " + default_in_list(9));        // 1
    __println("into_default(0) = " + into_default(0));              // 51
    __println("into_default(1) = " + into_default(1));              // 2
    __println("into_default(9) = " + into_default(9));              // 50
    __println("empty_continue(0) = " + empty_continue(0));          // 7
    __println("empty_continue(9) = " + empty_continue(9));          // 7
    return 0;
}

/*
negatives — one //-block uncommented per run.
*/

/* the value clause may not be empty. */
//-EXPECT-ERROR: A switch value is required.
//int neg_empty_value(int v) {
//    switch () {
//        0: { }
//    }
//    return 0;
//}

/* case const-expressions must be unique. */
//-EXPECT-ERROR: Duplicate case label '0'.
//int neg_dup_case(int v) {
//    switch (v) {
//        0: { }
//        0: { }
//    }
//    return 0;
//}

/* default is singular. */
//-EXPECT-ERROR: A switch may have only one default clause.
//int neg_dup_default(int v) {
//    switch (v) {
//        default: { }
//        default: { }
//    }
//    return 0;
//}

/* the scrutinee must be integer-class. */
//-EXPECT-ERROR: A switch value must be integer-class; got 'float'.
//int neg_float_scrutinee() {
//    switch (1.5) {
//        0: { }
//    }
//    return 0;
//}

/* a case label must be a constant. */
//-EXPECT-ERROR: A case label must be a constant.
//int neg_nonconst_label(int v, int n) {
//    switch (v) {
//        n: { }
//    }
//    return 0;
//}

/* a case label must be an integer constant (no float). */
//-EXPECT-ERROR: A case label must be an integer constant.
//int neg_float_label(int v) {
//    switch (v) {
//        1.5: { }
//    }
//    return 0;
//}

/* a naked continue inside a switch with no enclosing loop is an error. */
//-EXPECT-ERROR: A 'continue' statement must be inside a loop.
//int neg_continue_no_loop(int v) {
//    switch (v) {
//        default: { continue; }
//    }
//    return 0;
//}

/* code after an exhaustive (all-return) switch is unreachable. */
//-EXPECT-ERROR: Unreachable statement.
//int neg_unreachable_after(int v) {
//    switch (v) {
//        0: { return 1; }
//        default: { return 2; }
//    }
//    return 9;
//}

/* a default-less switch is possibly-skipped, so a body-only assignment does not
   definitely initialize r. */
//-EXPECT-ERROR: Use of uninitialized variable 'r'.
//int neg_da_default_less(int v) {
//    int r;
//    switch (v) {
//        0: { r = 1; }
//    }
//    return r;
//}

/* (F) a label-list shares a returning body, so the switch IS a return-terminator
   — code after it is unreachable. */
//-EXPECT-ERROR: Unreachable statement.
//int neg_unreachable_after_stacked(int v) {
//    switch (v) {
//        3: 4: { return 1; }
//        default: { return 0; }
//    }
//    return 9;
//}

/* (D) duplicate case labels across kinds — 'a' and 97 are the same value. */
//-EXPECT-ERROR: Duplicate case label '97'.
//int neg_xkind_dup(int v) {
//    switch (v) {
//        'a': { }
//        97: { }
//    }
//    return 0;
//}

/* (E) duplicate case labels after folding — 1 + 2 and 3 are the same value. */
//-EXPECT-ERROR: Duplicate case label '3'.
//int neg_folded_dup(int v) {
//    switch (v) {
//        1 + 2: { }
//        3: { }
//    }
//    return 0;
//}

/* a clause that ends without assigning r leaves r not-definitely-assigned at
   the merge, even though the other clauses + default assign it. */
//-EXPECT-ERROR: Use of uninitialized variable 'r'.
//int neg_partial_da(int v) {
//    int r;
//    switch (v) {
//        0: { r = 1; }
//        1: { }
//        default: { r = 3; }
//    }
//    return r;
//}

/* a case label outside the scrutinee type's range can never match — rejected
   rather than emitted as a truncated constant. */
//-EXPECT-ERROR: Case label '300' is out of range for switch type 'char'.
//int neg_label_range(char c) {
//    switch (c) {
//        300: { return 1; }
//        default: { return 0; }
//    }
//}

/* a clause body must be a `{ }` block — a bare statement body is rejected. */
//-EXPECT-ERROR: Expected '{' to open the clause body.
//int neg_missing_brace(int v) {
//    switch (v) {
//        0: return 1;
//        default: { return 0; }
//    }
//}

/* a non-returning clause with a trailing continue into a returning clause makes
   the switch exhaustive via the fall-through chain — code after it is
   unreachable. */
//-EXPECT-ERROR: Unreachable statement.
//int neg_unreachable_continue_chain(int v) {
//    switch (v) {
//        0: { } continue;
//        default: { return 0; }
//    }
//    return 9;
//}
