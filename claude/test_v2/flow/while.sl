/*
test pre- and post- condition while loops.
including break, continue, and labels.

curly brackets around the code boy are required.

an empty condition is always true.

    while () {
    }

post condition while.

    while {
    } (cond);

labels always follow the closing curly bracket.
the default name of a while loop is 'while'.

    while (cond) {
    } :loop;

break and continue may use an optional number or name to break from
nested while loops.
when labels land.

    while (cond1) {
        while (cond2) {
            if (cond3) {
                break outer;
            }
            if (cond4) {
                continue inner;
            }
            if (cond5) {
                break 2;
            }
            if (cond5) {
                /* equivalent to naked break; */
                break 1;
            }
        } :inner;
    } :outer;
*/

/*
while is a pre-condition loop: the condition is tested before each iteration, so
the body may run zero times. per that, definite-assignment treats every loop as
possibly-zero — a local assigned only in the body is NOT assigned after the loop
(the post-loop init-set is the entry set), and the condition's reads must be
satisfied on entry. the condition truthy-coerces like an if condition.

break exits the nearest enclosing loop; continue jumps to its test. both are
abrupt (a statement after one in the same block is unreachable), and both are
errors outside any loop.

a local declared in a loop body is alloca'd ONCE (hoisted to the function entry
block), not per iteration — big_loop below would overflow the stack otherwise.
*/

/* basic counting loop; sum 1..n. n <= 0 runs zero times. */
int sum_to(int n) {
    int i = 1;
    int s = 0;
    while (i <= n) {
        s = s + i;
        i = i + 1;
    }
    return s;
}

/* break out of a bounded loop at the first i whose square exceeds n. */
int first_over(int n) {
    int i = 0;
    while (i < 100) {
        if (i * i > n) {
            break;
        }
        i = i + 1;
    }
    return i;
}

/* continue to skip even values; sum the odds in 1..n. */
int sum_odds(int n) {
    int i = 0;
    int s = 0;
    while (i < n) {
        i = i + 1;
        if (i % 2 == 0) {
            continue;
        }
        s = s + i;
    }
    return s;
}

/* PPID in the condition: n is post-decremented as the test is evaluated each
   iteration — including the final (false) test, so n ends one below the floor. */
int countdown(int n) {
    int count = 0;
    while (n-- > 0) {
        count = count + 1;
    }
    return count * 10 + n;
}

/* nested loops; the inner loop variable `c` is declared INSIDE the outer body
   (re-initialized each outer pass — its alloca is hoisted, not re-run). */
int grid(int rows, int cols) {
    int r = 0;
    int total = 0;
    while (r < rows) {
        int c = 0;
        while (c < cols) {
            total = total + 1;
            c = c + 1;
        }
        r = r + 1;
    }
    return total;
}

/* the loop never runs: the body init doesn't escape, x is unchanged. */
int never_runs() {
    int x = 5;
    while (x > 100) {
        x = x + 1;
    }
    return x;
}

/* non-bool condition: a numeric truthy-coerces (n != 0). */
int drain(int n) {
    int steps = 0;
    while (n) {
        n = n - 1;
        steps = steps + 1;
    }
    return steps;
}

/* an empty condition is always true (a slids convention); break is the only
   exit. count up to n. */
int count_to(int n) {
    int i = 0;
    while () {
        if (i >= n) {
            break;
        }
        i = i + 1;
    }
    return i;
}

/* a short-circuit && in the condition — re-evaluated each iteration. proves the
   logical lowers to phi nodes (no per-iteration alloca in the loop header). */
int bounded_sum(int n) {
    int i = 0;
    int s = 0;
    while (i < n && s < 100) {
        s = s + i;
        i = i + 1;
    }
    return s;
}

/* a body-local declared every iteration over a large loop — proves the alloca
   is hoisted to the entry block (a per-iteration alloca would overflow the
   stack across a million passes). */
int big_loop() {
    int i = 0;
    int last = 0;
    while (i < 1000000) {
        int x = i;
        last = x;
        i = i + 1;
    }
    return last;
}

/* post-condition: the body runs at least once, even when the condition is false
   on entry. */
int do_once(int n) {
    int count = 0;
    while {
        count = count + 1;
        n = n - 1;
    } (n > 0);
    return count;
}

/* the body runs at least once, so a value assigned unconditionally in the body
   IS definitely assigned after the loop (the key difference from a pre-condition
   while, where it would not be). */
int last_val(int n) {
    int v;
    while {
        v = n;
        n = n - 1;
    } (n > 0);
    return v;
}

/* break exits a do-while immediately. */
int do_break(int n) {
    int i = 0;
    while {
        i = i + 1;
        if (i >= n) {
            break;
        }
    } (i < 100);
    return i;
}

/* continue in a do-while jumps to the post-condition test. */
int do_continue(int n) {
    int i = 0;
    int s = 0;
    while {
        i = i + 1;
        if (i % 2 == 0) {
            continue;
        }
        s = s + i;
    } (i < n);
    return s;
}

/* PPID in the post-condition test: n is post-decremented as the test runs after
   each iteration. */
int do_countdown(int n) {
    int count = 0;
    while {
        count = count + 1;
    } (n-- > 0);
    return count * 10 + n;
}

/* break in an inner loop exits only the inner loop; the outer continues. */
int nested_break(int rows, int cols) {
    int r = 0;
    int count = 0;
    while (r < rows) {
        int c = 0;
        while (c < cols) {
            if (c == 2) {
                break;
            }
            count = count + 1;
            c = c + 1;
        }
        r = r + 1;
    }
    return count;
}

/* continue in an inner loop restarts only the inner loop. */
int nested_continue(int rows, int cols) {
    int r = 0;
    int total = 0;
    while (r < rows) {
        int c = 0;
        while (c < cols) {
            c = c + 1;
            if (c == 2) {
                continue;
            }
            total = total + 1;
        }
        r = r + 1;
    }
    return total;
}

/* break inside a bare block (not an if-arm) within a loop still targets the
   loop — the loop context threads through the block. */
int break_in_block(int n) {
    int i = 0;
    while (i < n) {
        {
            if (i == 3) {
                break;
            }
        }
        i = i + 1;
    }
    return i;
}

/* || in a loop condition lowers to a phi (no per-iteration alloca). loops while
   i is below either bound. */
int or_cond(int a, int b) {
    int i = 0;
    int count = 0;
    while (i < a || i < b) {
        count = count + 1;
        i = i + 1;
    }
    return count;
}

/* ^^ in a loop condition (straight-line, both operands evaluated). the second
   operand is false here, so the loop runs while i < n. */
int xor_cond(int n) {
    int i = 0;
    int count = 0;
    while ((i < n) ^^ (i >= 100)) {
        count = count + 1;
        i = i + 1;
    }
    return count;
}

/* do-while: the condition may read a local assigned in the body. with no
   continue, the test is reached only via the normal fall-through, where x is
   assigned — so this is sound and compiles. */
int do_cond_reads_body(int n) {
    int x;
    int iters = 0;
    while {
        x = n - iters;
        iters = iters + 1;
    } (x > 0);
    return iters;
}

/* a local declared inside a do-while body is hoisted (alloca'd once), like any
   loop body. */
int do_body_local(int n) {
    int i = 0;
    int sum = 0;
    while {
        int step = i;
        sum = sum + step;
        i = i + 1;
    } (i < n);
    return sum;
}

/* an empty post-condition is always true (the convention applies to the post
   form too); break is the only exit. */
int do_empty_cond(int n) {
    int i = 0;
    while {
        if (i >= n) {
            break;
        }
        i = i + 1;
    } ();
    return i;
}

/* a do-while body always runs once, so a constant-FALSE condition does NOT make
   it unreachable — this compiles and runs the body once. */
int do_const_false(int n) {
    int r = 0;
    while {
        r = n + 1;
    } (false);
    return r;
}

/* an explicit while(true) is NOT flagged (3B: no constant-true loop special
   case); the body is reachable, break is the only exit. */
int while_true(int n) {
    int i = 0;
    while (true) {
        if (i >= n) {
            break;
        }
        i = i + 1;
    }
    return i;
}

int32 main() {
    __println("sum_to(5) = " + sum_to(5));      // 15
    __println("sum_to(0) = " + sum_to(0));      // 0
    __println("first_over(10) = " + first_over(10));    // 4
    __println("first_over(0) = " + first_over(0));      // 1
    __println("sum_odds(5) = " + sum_odds(5));  // 9
    __println("sum_odds(6) = " + sum_odds(6));  // 9
    __println("countdown(3) = " + countdown(3));    // 29
    __println("grid(3, 4) = " + grid(3, 4));    // 12
    __println("never_runs() = " + never_runs());    // 5
    __println("drain(4) = " + drain(4));        // 4
    __println("count_to(5) = " + count_to(5));  // 5
    __println("bounded_sum(5) = " + bounded_sum(5));    // 10
    __println("big_loop() = " + big_loop());    // 999999
    __println("do_once(0) = " + do_once(0));    // 1
    __println("do_once(3) = " + do_once(3));    // 3
    __println("last_val(3) = " + last_val(3));  // 1
    __println("last_val(0) = " + last_val(0));  // 0
    __println("do_break(3) = " + do_break(3));  // 3
    __println("do_break(0) = " + do_break(0));  // 1
    __println("do_continue(5) = " + do_continue(5));    // 9
    __println("do_countdown(3) = " + do_countdown(3));  // 39
    __println("do_const_false(5) = " + do_const_false(5));  // 6
    __println("while_true(4) = " + while_true(4));  // 4
    __println("nested_break(3, 5) = " + nested_break(3, 5));    // 6
    __println("nested_continue(2, 3) = " + nested_continue(2, 3));  // 4
    __println("break_in_block(10) = " + break_in_block(10));    // 3
    __println("break_in_block(2) = " + break_in_block(2));      // 2
    __println("or_cond(3, 5) = " + or_cond(3, 5));      // 5
    __println("or_cond(2, 0) = " + or_cond(2, 0));      // 2
    __println("xor_cond(4) = " + xor_cond(4));  // 4
    __println("do_cond_reads_body(3) = " + do_cond_reads_body(3));  // 4
    __println("do_body_local(5) = " + do_body_local(5));    // 10
    __println("do_empty_cond(0) = " + do_empty_cond(0));    // 0
    __println("do_empty_cond(4) = " + do_empty_cond(4));    // 4
    return 0;
}

/*
negatives — one //-block uncommented per run.
*/

/* break outside any loop. */
//-EXPECT-ERROR: A 'break' statement must be inside a loop.
//int neg_break_outside(int n) {
//    if (n > 0) {
//        break;
//    }
//    return 0;
//}

/* continue outside any loop. */
//-EXPECT-ERROR: A 'continue' statement must be inside a loop.
//int neg_continue_outside(int n) {
//    if (n > 0) {
//        continue;
//    }
//    return 0;
//}

/* a statement after a break in the same block is unreachable. */
//-EXPECT-ERROR: Unreachable statement.
//int neg_unreachable_after_break(int n) {
//    while (n > 0) {
//        break;
//        n = n - 1;
//    }
//    return 0;
//}

/* a loop may run zero times, so a body-only assignment doesn't initialize r for
   after the loop. */
//-EXPECT-ERROR: Use of uninitialized variable 'r'
//int neg_loop_init_escape(int n) {
//    int r;
//    while (n > 0) {
//        r = 1;
//        n = n - 1;
//    }
//    return r;
//}

/* do-while: a break before the assignment cuts the after-set — r is assigned on
   the normal fall-through but not on the break path, so the ∩ drops it. */
//-EXPECT-ERROR: Use of uninitialized variable 'r'
//int neg_do_break_before_assign(int n) {
//    int r;
//    while {
//        if (n > 0) {
//            break;
//        }
//        r = 5;
//    } (n > 0);
//    return r;
//}

/* do-while: a continue reaches the post-condition test before the assignment,
   so the condition's read of x is not satisfied on every path to the test. */
//-EXPECT-ERROR: Use of uninitialized variable 'x'
//int neg_do_continue_cond(int n) {
//    int x;
//    while {
//        if (n > 0) {
//            continue;
//        }
//        x = 5;
//    } (x > 0);
//    return x;
//}

/* a statement after a continue in the same block is unreachable. */
//-EXPECT-ERROR: Unreachable statement.
//int neg_unreachable_after_continue(int n) {
//    while (n > 0) {
//        continue;
//        n = n - 1;
//    }
//    return 0;
//}

/* a loop is never a terminator (3B: possibly-zero), so a non-void function whose
   last statement is a while does not satisfy return-correctness. */
//-EXPECT-ERROR: must end with a return statement
//int neg_while_no_return(int n) {
//    while (n > 0) {
//        n = n - 1;
//    }
//}

/* likewise for a post-condition while. */
//-EXPECT-ERROR: must end with a return statement
//int neg_do_no_return(int n) {
//    while {
//        n = n - 1;
//    } (n > 0);
//}

/* a constant-false condition makes the body unreachable. (a constant-TRUE loop
   is NOT flagged — 3B has no constant-true loop special case.) */
//-EXPECT-ERROR: Unreachable statement.
//int neg_while_false() {
//    while (false) {
//        __println("dead");
//    }
//    return 0;
//}
