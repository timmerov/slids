/*
test if / else chains.

an if/else splits control flow; definite-assignment INTERSECTS at the merge — a
local is initialized after the if only if EVERY path reaching the merge
initialized it. a branch that returns never reaches the merge, so it does not
constrain the join (it contributes the universal set, dropped from the ∩).

an else-less if never adds an initialization: the fall-through path keeps the
entry set, so entry ∩ then = entry. the condition truthy-coerces like `!`/`&&`
(a numeric is compared against zero, a bool passes through). a statement after a
branch that always returns is unreachable. an if/else where both arms return is
itself a terminating statement and may stand as a function's trailing
return-correctness witness.

an arm is a block: it opens a nested scope (arm-locals die at the arm), and a
read of an outer local on only one path still counts as a use.
*/

/* both arms initialize r -> r is definitely assigned after the merge. */
int both_arms(int n) {
    int r;
    if (n > 0) {
        r = 10;
    } else {
        r = 20;
    }
    return r;
}

/* the then-arm returns (abrupt, never reaches the merge); only the else-arm
   contributes to the join, so r is assigned after the if. */
int one_arm_returns(int n) {
    int r;
    if (n > 0) {
        return 99;
    } else {
        r = 7;
    }
    return r;
}

/* guard clause: an else-less if whose then-arm returns. the abrupt then leaves
   only the no-else fall-through path, which keeps the entry set S unchanged — so
   r (assigned before the if) is still assigned after it. */
int guard_clause(int n) {
    int r = 100;
    if (n < 0) {
        return -1;
    }
    return r;
}

/* else-if chain: each leaf assigns b, so b is assigned on every path. */
int band(int n) {
    int b;
    if (n < 0) {
        b = 0;
    } else if (n < 10) {
        b = 1;
    } else if (n < 100) {
        b = 2;
    } else {
        b = 3;
    }
    return b;
}

/* else-if chain with an abrupt arm mid-chain: the first arm returns; the rest of
   the chain assigns r on every remaining path, so r is assigned after. */
int chain_return(int n) {
    int r;
    if (n < 0) {
        return -1;
    } else if (n < 10) {
        r = 1;
    } else {
        r = 2;
    }
    return r;
}

/* an else-less if conditionally rewrites an already-initialized local. the
   no-else path keeps r assigned, so the join leaves r assigned. */
int clamp_low(int n) {
    int r = n;
    if (r < 0) {
        r = 0;
    }
    return r;
}

/* nested if/else inside an arm: the inner if assigns r on both its paths, so
   the outer then-arm assigns r, matching the outer else-arm. */
int nested(int a, int b) {
    int r;
    if (a > 0) {
        if (b > 0) {
            r = 1;
        } else {
            r = 2;
        }
    } else {
        r = 3;
    }
    return r;
}

/* an arm is a block: each arm declares its own `t` (distinct entries that die at
   the arm), and `t` is not visible after the if. */
int arm_scope(int n) {
    int r = 0;
    if (n > 0) {
        int t = 5;
        r = t;
    } else {
        int t = 9;
        r = t;
    }
    return r;
}

/* both arms return -> the if terminates every path; it stands as the function's
   trailing statement with no return after it (return-correctness via the if). */
int sign(int n) {
    if (n >= 0) {
        return 1;
    } else {
        return -1;
    }
}

/* empty arms parse, scope, and fall through to the merge; nothing touches r. */
int empty_arms(int n) {
    int r = 7;
    if (n > 0) {
    } else {
    }
    if (n > 0) {
    }
    return r;
}

/* a local read on only ONE path is still a use (read_locals is a monotonic union
   across arms, never intersected) — only_then is read in the then-arm only and
   must not be flagged unused. */
int read_one_arm(int n) {
    int only_then = 42;
    int out;
    if (n > 0) {
        out = only_then;
    } else {
        out = 1;
    }
    return out;
}

/* non-bool conditions truthy-coerce: a numeric is tested against zero, a bool
   passes through. */
int coerce_cond(int n) {
    int acc = 0;
    if (n) {
        acc = acc + 1;
    }
    bool flag = true;
    if (flag) {
        acc = acc + 10;
    }
    int zero = 0;
    if (zero) {
        acc = acc + 100;
    }
    return acc;
}

/* compound boolean conditions: short-circuit &&/|| and ! feed the branch. */
int compound(int a, int b) {
    int acc = 0;
    if (a > 0 && b > 0) {
        acc = acc + 1;
    }
    if (a > 0 || b > 0) {
        acc = acc + 10;
    }
    if (!(a > 0)) {
        acc = acc + 100;
    }
    return acc;
}

/* PPID in the condition: the post-increment reads k for the test, then bumps k
   as the condition phrase exits — before either branch. k is incremented
   regardless of which way the branch goes (verified on both paths below). */
int ppid_cond(int n) {
    int k = n;
    int taken = 0;
    if (k++ > 0) {
        taken = 1;
    }
    return taken * 1000 + k;
}

int32 main() {
    __println("both_arms(5) = " + both_arms(5));        // 10
    __println("both_arms(-5) = " + both_arms(-5));      // 20
    __println("one_arm_returns(5) = " + one_arm_returns(5));    // 99
    __println("one_arm_returns(-5) = " + one_arm_returns(-5));  // 7
    __println("guard_clause(5) = " + guard_clause(5));      // 100
    __println("guard_clause(-2) = " + guard_clause(-2));    // -1
    __println("band(-1) = " + band(-1));    // 0
    __println("band(5) = " + band(5));      // 1
    __println("band(50) = " + band(50));    // 2
    __println("band(500) = " + band(500));  // 3
    __println("chain_return(-5) = " + chain_return(-5));    // -1
    __println("chain_return(5) = " + chain_return(5));      // 1
    __println("chain_return(50) = " + chain_return(50));    // 2
    __println("clamp_low(7) = " + clamp_low(7));    // 7
    __println("clamp_low(-3) = " + clamp_low(-3));  // 0
    __println("nested(1, 1) = " + nested(1, 1));     // 1
    __println("nested(1, -1) = " + nested(1, -1));   // 2
    __println("nested(-1, 0) = " + nested(-1, 0));   // 3
    __println("arm_scope(3) = " + arm_scope(3));    // 5
    __println("arm_scope(-3) = " + arm_scope(-3));  // 9
    __println("sign(3) = " + sign(3));      // 1
    __println("sign(-3) = " + sign(-3));    // -1
    __println("empty_arms(3) = " + empty_arms(3));  // 7
    __println("read_one_arm(5) = " + read_one_arm(5));      // 42
    __println("read_one_arm(-5) = " + read_one_arm(-5));    // 1
    __println("coerce_cond(7) = " + coerce_cond(7));    // 11
    __println("coerce_cond(0) = " + coerce_cond(0));    // 10
    __println("compound(1, 1) = " + compound(1, 1));    // 11
    __println("compound(-1, 1) = " + compound(-1, 1));  // 110
    __println("ppid_cond(5) = " + ppid_cond(5));    // 1006
    __println("ppid_cond(0) = " + ppid_cond(0));    // 1
    return 0;
}

/*
negatives — one //-block uncommented per run.
*/

/* one arm initializes r, the else falls through uninitialized -> the join leaves
   r uninitialized, so the read after the if fails. */
//-EXPECT-ERROR: Use of uninitialized variable 'r'
//int neg_one_arm() {
//    int r;
//    if (true) {
//        r = 1;
//    }
//    return r;
//}

/* both arms present, but only the then-arm initializes r; the else initializes a
   DIFFERENT local -> the ∩ drops r, uninitialized after. */
//-EXPECT-ERROR: Use of uninitialized variable 'r'
//int neg_else_one_arm() {
//    int r;
//    if (true) {
//        r = 1;
//    } else {
//        int other = 2;
//        __println("other = " + other);
//    }
//    return r;
//}

/* a defaultless else-if chain: the innermost missing else keeps S, so b is not
   assigned on every path -> uninitialized after. */
//-EXPECT-ERROR: Use of uninitialized variable 'b'
//int neg_chain_nodefault(int n) {
//    int b;
//    if (n < 0) {
//        b = 0;
//    } else if (n < 10) {
//        b = 1;
//    }
//    return b;
//}

/* both arms return -> the if is abrupt, so the statement after it is dead. */
//-EXPECT-ERROR: Unreachable statement.
//int neg_unreachable() {
//    if (true) {
//        return 1;
//    } else {
//        return 2;
//    }
//    return 3;
//}

/* unreachable INSIDE an arm: a statement after a return within the then-block. */
//-EXPECT-ERROR: Unreachable statement.
//int neg_unreachable_in_arm(int n) {
//    if (n > 0) {
//        return 1;
//        n = 2;
//    }
//    return 0;
//}

/* unreachable not via an if: a plain statement after a return. */
//-EXPECT-ERROR: Unreachable statement.
//int neg_unreachable_plain() {
//    return 1;
//    int x = 2;
//    return x;
//}

/* an arm-local declared, never written, never read -> unused at arm exit. */
//-EXPECT-ERROR: Unused local variable 'u'
//int neg_arm_unused(int n) {
//    if (n > 0) {
//        int u;
//    }
//    return 0;
//}

/* an arm-local written but never read -> set-but-never-used at arm exit. */
//-EXPECT-ERROR: Local variable 'u' set but never used
//int neg_arm_set_unused(int n) {
//    if (n > 0) {
//        int u = 5;
//    }
//    return 0;
//}
