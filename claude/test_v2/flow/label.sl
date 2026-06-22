/*
test labels on for/while loops.
include numbered and named breaks and continues.
includes breaks and continues in switch statements.

the label goes after the closing curly bracket.
followed by semi-colon if necessary.

    while (cond) {
        while {
            if (cond) {
                /* break from the inner loop. */
                break;
            switch (value) {
            0: {
                /* naked break — exits the inner loop (switch is transparent). */
                break;
            }
            1: {
                /* break from the inner loop. */
                break 1;
            }
            2: {
                /* break from the inner loop. */
                break inner;
            }
            3: {
                /* continue the inner loop. */
                continue;
            }
            4: {
                /* continue the inner loop. */
                continue 1;
            }
            5: {
                /* continue the inner loop. */
                continue inner;
            }
            6: {
                /* break from the outer loop. */
                break 2;
            }
            7: {
                /* break from the outer loop. */
                break outer;
            }
            8: {
                /* continue the outer loop. */
                continue 2;
            }
            9: {
                /* continue the outer loop. */
                continue outer;
            }
            }
        } :inner (cond);
    } :outer;

the default name of a for loop is for.
the default name of a while loop is while.

*/

/*
claude says:

SESSION 6 — labels on loops (switch labels deferred / "distant future"):
- a loop (while / do-while / for) carries an optional `:name` right after its
  body `}` (for do-while, that is before the trailing `(cond)`); default names
  are the keyword `for` / `while`. Innermost match wins; shadowing is allowed.
- break/continue take an optional argument: a NUMBER (Nth ENCLOSING LOOP outward)
  or a NAME (a loop's label, incl. the `for`/`while` keyword default). switch is
  transparent to both break and continue (it is not a frame / break target), so
  naked break = naked continue = the nearest enclosing loop. Inside a switch a
  naked `break;` and `break 1;` are equivalent (both exit the nearest loop).
- NO break/continue of any flavor (naked / numbered / named) is allowed directly
  in a long-for update clause. No labels on switch this round.
*/

enum Dir ( N, S, E, W );

/* a label on a DO-WHILE sits between the body `}` and the `(cond)`. */
int dowhile_label(int n) {
    int hits = 0;
    int i = 0;
    while {
        ++i;
        if (i == 3) {
            break loop;
        }
        hits = hits + 1;
    } :loop (i < n);
    return hits;
}

/* a label on a RANGED-for, reached by a named break from a nested loop. */
int ranged_label(int n) {
    int hits = 0;
    for (i : 0..n) {
        int j = 0;
        while (j < n) {
            ++j;
            if (j == 2) {
                break rows;
            }
            hits = hits + 1;
        }
    } :rows;
    return hits;
}

/* a label on an ENUM-for, reached by a named break. */
int enum_label() {
    int hits = 0;
    for (d : Dir) {
        for (e : Dir) {
            if (hits == 1) {
                break outer;
            }
            hits = hits + 1;
        }
    } :outer;
    return hits;
}

/* an explicit label on a LONG-for, reached by a named break. */
int longfor_label(int n) {
    int hits = 0;
    for (i = 0) (i < n) { ++i; } {
        int j = 0;
        while (j < n) {
            ++j;
            if (j == 2) {
                break rows;
            }
            hits = hits + 1;
        }
    } :rows;
    return hits;
}

/* `break 2;` from inside a switch exits the OUTER of two enclosing loops (switch
   is transparent to break — it is not a frame). */
int sw_break2(int n) {
    int hits = 0;
    int i = 0;
    while (i < n) {
        ++i;
        int j = 0;
        while (j < n) {
            ++j;
            switch (j) {
                1: { break 2; }
                default: { hits = hits + 1; }
            }
        }
    }
    return hits;
}

/* `break 3;` exits the third enclosing loop (the outermost of three). */
int break3(int n) {
    int hits = 0;
    int a = 0;
    while (a < n) {
        ++a;
        int b = 0;
        while (b < n) {
            ++b;
            int c = 0;
            while (c < n) {
                ++c;
                if (a == 2) {
                    break 3;
                }
                hits = hits + 1;
            }
        }
    }
    return hits;
}

/* a named break that skips TWO nested switches to exit the enclosing loop. */
int nested_sw_break(int n) {
    int hits = 0;
    int i = 0;
    while (i < n) {
        ++i;
        switch (i) {
            default: {
                switch (i) {
                    2: { break w; }
                    default: { hits = hits + 1; }
                }
            }
        }
    } :w;
    return hits;
}

/* a numbered continue from inside a switch advances the enclosing loop. */
int cont_from_sw(int n) {
    int hits = 0;
    int i = 0;
    while (i < n) {
        ++i;
        switch (i) {
            2: { continue 1; }
            default: { hits = hits + 1; }
        }
        hits = hits + 100;
    }
    return hits;
}

/* a named continue by the default `while` keyword. */
int cont_while(int n) {
    int hits = 0;
    int i = 0;
    while (i < n) {
        ++i;
        if (i == 2) {
            continue while;
        }
        hits = hits + 1;
    }
    return hits;
}

/* a labeled break into a do-while where the var is assigned on EVERY exit path
   (unconditional body assign + the break), so it is definitely assigned after. */
int dowhile_da_ok(int n) {
    int r;
    int i = 0;
    while {
        r = 7;
        ++i;
        if (i > 2) {
            break w;
        }
    } :w (i < n);
    return r;
}

/* a declared-but-unreferenced label compiles cleanly (labels are not swept). */
int unused_label(int n) {
    int i = 0;
    while (i < n) {
        ++i;
    } :loop;
    return i;
}

/* named break from an inner loop exits the OUTER loop by its label. */
int named_break(int n) {
    int hits = 0;
    int i = 0;
    while (i < n) {
        ++i;
        int j = 0;
        while (j < n) {
            ++j;
            if (i == 2) {
                break outer;
            }
            hits = hits + 1;
        }
    } :outer;
    return hits;
}

/* numbered break: `break 2;` exits the second enclosing loop (the outer). */
int numbered_break(int n) {
    int hits = 0;
    int i = 0;
    while (i < n) {
        ++i;
        int j = 0;
        while (j < n) {
            ++j;
            if (i == 2) {
                break 2;
            }
            hits = hits + 1;
        }
    }
    return hits;
}

/* break by the default while name. */
int break_default(int n) {
    int hits = 0;
    int i = 0;
    while (i < n) {
        ++i;
        if (i == 3) {
            break while;
        }
        hits = hits + 1;
    }
    return hits;
}

/* break by the default for name. */
int for_default(int n) {
    int hits = 0;
    for (i = 0) (i < n) { ++i; } {
        if (i == 2) {
            break for;
        }
        hits = hits + 1;
    }
    return hits;
}

/* numbered continue: `continue 2;` re-tests the outer loop, skipping the rest of
   both bodies (so the outer post-inner code is bypassed). */
int continue_numbered(int n) {
    int hits = 0;
    int i = 0;
    while (i < n) {
        ++i;
        int j = 0;
        while (j < n) {
            ++j;
            if (j == 2) {
                continue 2;
            }
            hits = hits + 1;
        }
        hits = hits + 1000;
    }
    return hits;
}

/* a break inside a switch, numbered to exit the enclosing loop. switch is
   transparent to break, so a naked break here does the same thing. */
int sw_break_loop(int n) {
    int hits = 0;
    int i = 0;
    while (i < n) {
        ++i;
        switch (i) {
            2: { break 1; }
            default: { hits = hits + 1; }
        }
    }
    return hits;
}

/* a named continue inside a switch advances the enclosing loop. */
int sw_continue_loop(int n) {
    int hits = 0;
    int i = 0;
    while (i < n) {
        ++i;
        switch (i) {
            2: { continue loop; }
            default: { hits = hits + 1; }
        }
        hits = hits + 100;
    } :loop;
    return hits;
}

/* shadowing labels: both loops are :loop, and break loop hits the INNERMOST. */
int shadow_label(int n) {
    int hits = 0;
    int i = 0;
    while (i < n) {
        ++i;
        int j = 0;
        while (j < n) {
            ++j;
            if (j == 1) {
                break loop;
            }
            hits = hits + 100;
        } :loop;
        hits = hits + 1;
    } :loop;
    return hits;
}

int32 main() {
    __println("named_break(3) = " + named_break(3));            // 3
    __println("numbered_break(3) = " + numbered_break(3));      // 3
    __println("break_default(5) = " + break_default(5));        // 2
    __println("for_default(5) = " + for_default(5));            // 2
    __println("continue_numbered(3) = " + continue_numbered(3));    // 3
    __println("sw_break_loop(4) = " + sw_break_loop(4));        // 1
    __println("sw_continue_loop(4) = " + sw_continue_loop(4));  // 303
    __println("shadow_label(2) = " + shadow_label(2));          // 2
    __println("dowhile_label(5) = " + dowhile_label(5));        // 2
    __println("ranged_label(3) = " + ranged_label(3));          // 1
    __println("enum_label() = " + enum_label());                // 1
    __println("longfor_label(3) = " + longfor_label(3));        // 1
    __println("sw_break2(3) = " + sw_break2(3));                // 0
    __println("break3(2) = " + break3(2));                      // 4
    __println("nested_sw_break(4) = " + nested_sw_break(4));    // 1
    __println("cont_from_sw(4) = " + cont_from_sw(4));          // 303
    __println("cont_while(4) = " + cont_while(4));              // 3
    __println("dowhile_da_ok(5) = " + dowhile_da_ok(5));        // 7
    __println("unused_label(3) = " + unused_label(3));          // 3
    return 0;
}

/*
negatives — one //-block uncommented per run.
*/

/* a break count must be at least 1. */
//-EXPECT-ERROR: Break count must be at least 1.
//int neg_break_zero(int n) {
//    int i = 0;
//    while (i < n) {
//        ++i;
//        break 0;
//    }
//    return 0;
//}

/* a break count beyond the enclosing loop nesting. */
//-EXPECT-ERROR: Break count exceeds the enclosing loop nesting.
//int neg_break_too_deep(int n) {
//    int i = 0;
//    while (i < n) {
//        ++i;
//        break 5;
//    }
//    return 0;
//}

/* a named break with no matching enclosing loop. */
//-EXPECT-ERROR: No enclosing loop labeled 'nope'.
//int neg_break_unknown(int n) {
//    int i = 0;
//    while (i < n) {
//        ++i;
//        break nope;
//    }
//    return 0;
//}

/* `break while;` with no enclosing while (only a for). */
//-EXPECT-ERROR: No enclosing loop labeled 'while'.
//int neg_break_no_while(int n) {
//    for (i = 0) (i < n) { ++i; } {
//        break while;
//    }
//    return 0;
//}

/* a numbered break is not allowed directly in a for-loop update clause. */
//-EXPECT-ERROR: A 'break' statement is not allowed in a for-loop update clause.
//int neg_numbered_in_update(int n) {
//    for (i = 0) (i < n) { break 1; } {
//    }
//    return 0;
//}

/* a named continue is not allowed directly in a for-loop update clause. */
//-EXPECT-ERROR: A 'continue' statement is not allowed in a for-loop update clause.
//int neg_named_in_update(int n) {
//    for (i = 0) (i < n) { continue for; } {
//    }
//    return 0;
//}

/* a label on a switch is not allowed (labels go on loops only this round). */
//-EXPECT-ERROR: Expected statement.
//int neg_label_on_switch(int n) {
//    switch (n) {
//        default: { }
//    } :sw;
//    return 0;
//}
