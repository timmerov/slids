/*
test switch statements.

includes case, default, break.

    switch (value) {
    case const-expr:
    default:
        break;
    }

value clause may not be empty.
the code body may be empty.
case const-expressions must be unique.
case with no break falls through.
default is optional.
default is singular.
trailing break is optional.

switch statememts embedded in for/while loops:

    while (cond) {
        switch (value) {
        case 0:
            /* naked break, breaks from the switch. */
            break;
        case 1:
            /* numbered break, breaks from the loop. */
            break 1;
        case 2:
            /* named break, breaks from the loop. */
            break while;
        default:
            /* naked, numbered, named continue, continues the loop. */
            continue;
        }
    }

case must be able to handle qualified names.

    enum Dir ( N, S, E, W );
    Dir d = N;
    switch (d) {
    case Dir:N:
        break;
    }

notes:
consider nested switch statements.
should switch code blocks have labels so we can break by named switch?
maybe.
low utility.
deferred to distant future.
*/

/*
claude says:

SESSION 5 core (labels = numbered/named break+continue deferred to Phase 6):
- kSwitchStmt children[0]=scrutinee, [1..]=kCaseClause; a clause is
  children[0]=label const-expr (null = default), [1]=body block.
- scrutinee must be integer-class (int/uint/char/bool/enum); float rejected.
- fall-through: a clause body that completes normally falls into the next; a
  break/return/continue ends the run. naked break exits the SWITCH; naked
  continue skips the switch to the enclosing loop.
- DA: each clause body enters from S; after = ∩ of break-points + bottom-fall;
  a default-less switch contributes S (after = S). A switch is a return-
  correctness terminator iff it has a default, no clause has a break escaping
  past it, and the LAST clause's body ends in a return — fall-through means a
  stacked empty (or non-returning) clause reaches that final return.
- case labels must be unique constants of the scrutinee type; default singular,
  may sit anywhere. QUALIFIED enum-member labels (`Dir:N`) work via a
  terminator-aware qualified-name parse (scan the maximal `:`-ident chain; the
  trailing `:` is the terminator, else rewind one segment) — solves the v1 `:`
  collision.
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
        case 0: return 10;
        case 1: return 20;
        default: return 99;
    }
}

/* fall-through: a clause with no break falls into the next. */
int fallthrough(int v) {
    int r = 0;
    switch (v) {
        case 0: r = r + 1;
        case 1: r = r + 10;
        case 2: r = r + 100; break;
        default: r = r + 1000;
    }
    return r;
}

/* stacked labels: an empty clause body falls into the next. */
int stacked(int v) {
    int r = 0;
    switch (v) {
        case 3:
        case 4:
            r = 34; break;
        default:
            r = 0;
    }
    return r;
}

/* a stacked empty-body clause that falls through into a returning clause makes
   the switch exhaustive: every value returns (case 3 falls into case 4's
   return), so the switch is a return-terminator and the function needs no
   trailing return. */
int stacked_terminator(int v) {
    switch (v) {
        case 3:
        case 4: return 1;
        default: return 0;
    }
}

/* a default-less switch: an unmatched value is a no-op. */
int no_default(int v) {
    int r = 7;
    switch (v) {
        case 0: r = 0; break;
        case 1: r = 1; break;
    }
    return r;
}

/* default need not be last; here it falls through into a later case. */
int default_middle(int v) {
    int r = 0;
    switch (v) {
        case 0: r = 1; break;
        default: r = 9;
        case 1: r = r + 100; break;
    }
    return r;
}

/* naked break exits the SWITCH, not the enclosing loop. */
int switch_in_while(int n) {
    int count = 0;
    int i = 0;
    while (i < n) {
        switch (i) {
            case 0: count = count + 1; break;
            default: count = count + 10;
        }
        ++i;
    }
    return count;
}

/* naked continue inside a switch continues the enclosing loop. */
int switch_continue(int n) {
    int count = 0;
    int i = 0;
    while (i < n) {
        ++i;
        switch (i) {
            case 2: continue;
            default: count = count + 1;
        }
    }
    return count;
}

/* nested switches. */
int nested_switch(int a, int b) {
    int r = 0;
    switch (a) {
        case 0:
            switch (b) {
                case 0: r = 1; break;
                default: r = 2;
            }
            break;
        default: r = 9;
    }
    return r;
}

/* a char scrutinee; every case + default returns, so the switch is exhaustive
   and the function needs no trailing return (return-correctness terminator). */
int char_scrutinee(char c) {
    switch (c) {
        case 'a': return 1;
        case 'b': return 2;
        default: return 0;
    }
}

/* a bool scrutinee. */
int bool_scrutinee(bool b) {
    switch (b) {
        case true: return 1;
        default: return 0;
    }
}

/* QUALIFIED enum-member case labels — `case Dir:N:` (the v1 `:`-collision case). */
int dir_switch(Dir d) {
    switch (d) {
        case Dir:N: return 1;
        case Dir:S: return 2;
        case Dir:E: return 3;
        default: return 0;
    }
}

/* anonymous-enum bare members as case labels. */
int anon_enum_switch(int v) {
    switch (v) {
        case kRed: return 1;
        case kGreen: return 2;
        default: return 0;
    }
}

/* definite assignment: r is assigned on every case AND default, so it is
   definitely assigned after the switch. */
int da_exhaustive(int v) {
    int r;
    switch (v) {
        case 0: r = 1; break;
        case 1: r = 2; break;
        default: r = 3;
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
        case 1 + 2: return 1;
        default: return 0;
    }
}

/* (A) qualified enum-member labels followed by an IDENTIFIER-led body (assign),
   which exercises the terminator-aware label parse's rewind. */
int dir_assign(Dir d) {
    int r = 0;
    switch (d) {
        case Dir:N: r = 1; break;
        case Dir:S: r = 2; break;
        default: r = 9;
    }
    return r;
}

/* (B) a break nested inside an if: it escapes the switch on one path, while the
   other path falls through into the next clause. */
int break_in_if(int v, int stop) {
    int r = 0;
    switch (v) {
        case 0:
            if (stop > 0) {
                break;
            }
            r = 5;
        default:
            r = r + 1;
    }
    return r;
}

/* PPID in the scrutinee: i++ is read as the value, then bumps once. */
int ppid_scrutinee() {
    int i = 5;
    int r = 0;
    switch (i++) {
        case 5: r = 1; break;
        default: r = 2;
    }
    return r * 100 + i;
}

/* negative-valued case labels. */
int neg_labels(int v) {
    int r = 0;
    switch (v) {
        case -1: r = 1; break;
        case -2: r = 2; break;
        default: r = 9;
    }
    return r;
}

/* a uint scrutinee. */
int uint_scrutinee(uint v) {
    switch (v) {
        case 0: return 1;
        case 1: return 2;
        default: return 9;
    }
}

/* an int64 scrutinee. */
int i64_scrutinee(int64 v) {
    switch (v) {
        case 100: return 1;
        default: return 0;
    }
}

/* a char-underlying enum scrutinee with qualified labels. */
int letter_switch(Letter x) {
    switch (x) {
        case Letter:a: return 1;
        case Letter:b: return 2;
        default: return 0;
    }
}

/* a named constant as a case label. */
int const_label(int v) {
    switch (v) {
        case kThreshold: return 1;
        default: return 0;
    }
}

/* a case label at the scrutinee type's boundary — 255 fits a char (0..255). */
int char_max(char c) {
    switch (c) {
        case 255: return 1;
        case 0: return 2;
        default: return 9;
    }
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
    __println("break_in_if(0, 1) = " + break_in_if(0, 1));          // 0
    __println("break_in_if(0, 0) = " + break_in_if(0, 0));          // 6
    __println("break_in_if(9, 0) = " + break_in_if(9, 0));          // 1
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
    return 0;
}

/*
negatives — one //-block uncommented per run.
*/

/* the value clause may not be empty. */
//-EXPECT-ERROR: A switch value is required.
//int neg_empty_value(int v) {
//    switch () {
//        case 0: break;
//    }
//    return 0;
//}

/* case const-expressions must be unique. */
//-EXPECT-ERROR: Duplicate case label '0'.
//int neg_dup_case(int v) {
//    switch (v) {
//        case 0: break;
//        case 0: break;
//    }
//    return 0;
//}

/* default is singular. */
//-EXPECT-ERROR: A switch may have only one default clause.
//int neg_dup_default(int v) {
//    switch (v) {
//        default: break;
//        default: break;
//    }
//    return 0;
//}

/* the scrutinee must be integer-class. */
//-EXPECT-ERROR: A switch value must be integer-class; got 'float32'.
//int neg_float_scrutinee() {
//    switch (1.5) {
//        case 0: break;
//    }
//    return 0;
//}

/* a case label must be a constant. */
//-EXPECT-ERROR: A case label must be a constant.
//int neg_nonconst_label(int v, int n) {
//    switch (v) {
//        case n: break;
//    }
//    return 0;
//}

/* a case label must be an integer constant (no float). */
//-EXPECT-ERROR: A case label must be an integer constant.
//int neg_float_label(int v) {
//    switch (v) {
//        case 1.5: break;
//    }
//    return 0;
//}

/* a naked continue inside a switch with no enclosing loop is an error. */
//-EXPECT-ERROR: A 'continue' statement must be inside a loop.
//int neg_continue_no_loop(int v) {
//    switch (v) {
//        default: continue;
//    }
//    return 0;
//}

/* code after an exhaustive (all-return) switch is unreachable. */
//-EXPECT-ERROR: Unreachable statement.
//int neg_unreachable_after(int v) {
//    switch (v) {
//        case 0: return 1;
//        default: return 2;
//    }
//    return 9;
//}

/* a default-less switch is possibly-skipped, so a body-only assignment does not
   definitely initialize r. */
//-EXPECT-ERROR: Use of uninitialized variable 'r'.
//int neg_da_default_less(int v) {
//    int r;
//    switch (v) {
//        case 0: r = 1; break;
//    }
//    return r;
//}

/* (C) a break nested in an if ESCAPES the switch, so a function ending in such a
   switch can fall through without returning — a trailing return is required. */
//-EXPECT-ERROR: must end with a return statement
//int neg_break_escape(int v) {
//    switch (v) {
//        case 0:
//            if (v > 0) { break; }
//            return 1;
//        default: return 2;
//    }
//}

/* (F) a stacked empty-body clause falls through into a returning clause, so the
   switch IS a return-terminator — code after it is unreachable. */
//-EXPECT-ERROR: Unreachable statement.
//int neg_unreachable_after_stacked(int v) {
//    switch (v) {
//        case 3:
//        case 4: return 1;
//        default: return 0;
//    }
//    return 9;
//}

/* (D) duplicate case labels across kinds — 'a' and 97 are the same value. */
//-EXPECT-ERROR: Duplicate case label '97'.
//int neg_xkind_dup(int v) {
//    switch (v) {
//        case 'a': break;
//        case 97: break;
//    }
//    return 0;
//}

/* (E) duplicate case labels after folding — 1 + 2 and 3 are the same value. */
//-EXPECT-ERROR: Duplicate case label '3'.
//int neg_folded_dup(int v) {
//    switch (v) {
//        case 1 + 2: break;
//        case 3: break;
//    }
//    return 0;
//}

/* a clause that breaks without assigning r leaves r not-definitely-assigned at
   the merge, even though the other clauses + default assign it. */
//-EXPECT-ERROR: Use of uninitialized variable 'r'.
//int neg_partial_da(int v) {
//    int r;
//    switch (v) {
//        case 0: r = 1; break;
//        case 1: break;
//        default: r = 3;
//    }
//    return r;
//}

/* a case label outside the scrutinee type's range can never match — rejected
   rather than emitted as a truncated constant. */
//-EXPECT-ERROR: Case label '300' is out of range for switch type 'char'.
//int neg_label_range(char c) {
//    switch (c) {
//        case 300: return 1;
//        default: return 0;
//    }
//}
