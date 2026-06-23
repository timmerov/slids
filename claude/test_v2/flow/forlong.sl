/*
test long form for loop.
including break, continue, and labels.

for (varlist) (cond) {update} {body}

parentheses and curly brackets around the clauses are required.
any of the clauses may be empty.

varlist is a comma separated tuple of variable declarations.
variable types may be inferred (when that lands) from the initializer.
initializers are optional for typed variables.
fresh variables in the var list are allocated once, outside the
update and body code blocks.

an empty condition clause is always true.

the update code block may not continue, break, or return.

long for expands to the following pseudo-code:

    (varlist)
    WHILE (cond) IS TRUE DO:
        {body}
        {update}
    END-WHILE

variables are re-used from an enclosing scope when possible.
only an issue when type inference lands.

    int x = -1;
    for (x = 0) (x < 10) { ++x; } {
        __println(x);
    }
    __println("x should be 10: " + x);

optional labels follow the closing curly bracket.
the default name of a for loop is 'for'.
when labels land.

    for (int x = 0) (x < 10) { ++x; } {
        __println(x);
    } :loop;

break and continue may use an optional number or name to break from
nested for loops.
when labels land.

    for () (cond1) {} {
        for () (cond2) {} {
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

break or continue to an undefined label or invalid number is a compile error.

shadowing a loop variable in the code body is valid.
in other words, there are 3 scope frames: for, update, body.

    for (int x = 0) (x < 10) { ++x; } {
        int x = 42;
        __println("prints 42: " + x);
    }
*/

/*
claude says:

design (explicit-types round; typeless/reuse + labels deferred):
- node kForLongStmt, children = [cond, update-block, body-block, varlist-decl*].
  varlist decls are children[3..]; explicit type required each (typeless errors
  at parse — deferred, needs inferred-init).
- the other for shapes (range/array/enum/tuple/class) will desugar TO this node;
  so it is handled NATIVELY in every stage (not lowered to while).
- 3 scope frames: for-scope (varlist) > {update-frame, body-frame} as siblings.
  body may shadow a for-var (reuses Session-1 block scoping + entry-id allocas).
- exec order (per canon pseudo-code): test cond; if true run body then update;
  repeat. pre-condition / possibly-zero, so post-loop init-set = post-varlist S
  (like a pre-condition while). continue -> update (then re-test); break -> exit.
- update clause may not break/continue/return: resolve update with in_for_update
  + for_update_floor; a break/continue at the update's own level or a return
  anywhere in the update errors. (a nested loop inside the update gives its own
  legal break/continue target.)
- codegen: hoist varlist allocas to entry; init -> head(cond) -> body -> update
  -> head; LoopCtx{ header=update, exit=exit }.
*/

/* basic long-for: body runs before update, so the body sees i = 0..n-1. */
int sum_for(int n) {
    int s = 0;
    for (int i = 0) (i < n) { ++i; } {
        s = s + i;
    }
    return s;
}

/* empty varlist + empty update clause; the body drives everything. */
int empty_clauses(int n) {
    int i = 0;
    int s = 0;
    for () (i < n) {} {
        s = s + i;
        ++i;
    }
    return s;
}

/* an empty condition is always true; break is the only exit. */
int for_empty_cond(int n) {
    int i = 0;
    for (int j = 0) () { ++j; } {
        if (j >= n) {
            break;
        }
        i = j;
    }
    return i;
}

/* a constant-true for condition (here explicit `(true)`) with no break never
   exits, so it is a non-completing terminator: this non-void function ends in
   one with no trailing return, exiting only via the return inside the body. */
int for_true_terminator(int n) {
    for (int i = 0) (true) { ++i; } {
        if (i >= n) {
            return i;
        }
    }
}

/* multiple variables in the varlist, updated together. */
int two_vars(int n) {
    int sum = 0;
    for (int i = 0, int j = n) (i < j) { ++i; --j; } {
        sum = sum + 1;
    }
    return sum;
}

/* continue jumps to the UPDATE (then re-tests) — so the loop variable still
   advances on a continue. if continue went to the test instead, this would spin
   forever; terminating + summing the odds proves continue -> update. */
int for_continue(int n) {
    int s = 0;
    for (int i = 0) (i < n) { ++i; } {
        if (i % 2 == 0) {
            continue;
        }
        s = s + i;
    }
    return s;
}

/* break exits the for immediately. */
int for_break(int n) {
    int i = 0;
    for (int k = 0) (k < 100) { ++k; } {
        if (k >= n) {
            break;
        }
        i = k;
    }
    return i;
}

/* nested for loops. */
int for_grid(int rows, int cols) {
    int total = 0;
    for (int r = 0) (r < rows) { ++r; } {
        for (int c = 0) (c < cols) { ++c; } {
            total = total + 1;
        }
    }
    return total;
}

/* 3 scope frames: the body declares its own `x` (frame 3) shadowing the for-var
   `x` (frame 1); the update's `++x` still drives the for-var, so the loop runs n
   times while the body's x stays 42. */
int for_shadow(int n) {
    int sum = 0;
    for (int x = 0) (x < n) { ++x; } {
        int x = 42;
        sum = sum + x;
    }
    return sum;
}

/* a typed for-var may omit its initializer; here `tmp` is written then read in
   the body (not read in the condition), so it is definitely assigned at use. */
int for_typed_noinit(int n) {
    int k = 0;
    int sum = 0;
    for (int tmp) (k < n) { ++k; } {
        tmp = k * 2;
        sum = sum + tmp;
    }
    return sum;
}

/* body-then-update DA: the update reads `x`, assigned in the body (which runs
   first each iteration). resolved body-first, so the update sees it. */
int for_update_reads_body(int n) {
    int last = 0;
    for (int x) (last < n) { last = x; } {
        x = last + 1;
    }
    return last;
}

/* break in an inner for exits only the inner for; the outer continues. */
int for_nested_break(int rows, int cols) {
    int count = 0;
    for (int r = 0) (r < rows) { ++r; } {
        for (int c = 0) (c < cols) { ++c; } {
            if (c == 2) {
                break;
            }
            count = count + 1;
        }
    }
    return count;
}

/* continue in an inner for restarts only the inner for (runs its update). */
int for_nested_continue(int rows, int cols) {
    int total = 0;
    for (int r = 0) (r < rows) { ++r; } {
        for (int c = 0) (c < cols) { ++c; } {
            if (c == 1) {
                continue;
            }
            total = total + 1;
        }
    }
    return total;
}

/* a break inside a loop nested in the UPDATE clause is legal — it targets the
   nested loop, not the for (the update may not break the for itself). */
int for_break_in_update(int n) {
    int sum = 0;
    for (int i = 0) (i < n) {
        while (true) {
            ++i;
            break;
        }
    } {
        sum = sum + i;
    }
    return sum;
}

/* PPID in the condition: i is post-incremented as the test runs each iteration
   (the condition is re-evaluated as a phrase). */
int for_ppid_cond(int n) {
    int count = 0;
    for (int i = 0) (i++ < n) {} {
        count = count + 1;
    }
    return count;
}

/* PPID in a varlist initializer (runs once): i takes j's value, then j bumps. */
int for_ppid_varinit(int n) {
    int j = 5;
    int sum = 0;
    for (int i = j++) (i < n) { ++i; } {
        sum = sum + 1;
    }
    return sum * 100 + j;
}

/* && in the for condition lowers to a phi in the loop header. */
int for_and_cond(int n) {
    int s = 0;
    for (int k = 0) (k < n && s < 100) { ++k; } {
        s = s + k;
    }
    return s;
}

/* an empty body; the update does the work. */
int for_empty_body(int n) {
    int c = 0;
    for (int i = 0) (i < n) { c = c + 1; ++i; } {
    }
    return c;
}

/* a local declared in the update block (its own frame), used there. */
int for_update_local(int n) {
    int sum = 0;
    for (int i = 0) (i < n) { int step = 2; i = i + step; } {
        sum = sum + i;
    }
    return sum;
}

/* a typeless loop var with an initializer — its type is inferred from the init
   (here int from `0`); behaves exactly like the explicit `for (int i = 0)`. */
int for_typeless(int n) {
    int s = 0;
    for (i = 0) (i < n) { ++i; } {
        s = s + i;
    }
    return s;
}

/* a typeless loop var whose name is ALREADY in scope reuses that local rather
   than declaring a fresh one. The varlist init runs once unconditionally, so the
   reused `x` is observable after the loop, holding the last iterated value. */
int for_reuse(int n) {
    int x = -1;
    for (x = 0) (x < n) { ++x; } {
    }
    return x;
}

/* a typeless loop var with NO initializer reuses the enclosing local without
   re-initializing it (a no-op slot); `x` keeps its current value as the loop
   starts. body-then-update sees x = 0..n-1. */
int for_noinit_reuse(int n) {
    int x = 0;
    int s = 0;
    for (x) (x < n) { ++x; } {
        s = s + x;
    }
    return s;
}

/* a mixed varlist: a typeless var (inferred) beside an explicitly-typed one. */
int for_typeless_mixed(int n) {
    int sum = 0;
    for (i = 0, int j = n) (i < j) { ++i; --j; } {
        sum = sum + 1;
    }
    return sum;
}

/* two typeless vars in one varlist, each inferred from its own initializer. */
int for_two_typeless(int n) {
    int sum = 0;
    for (i = 0, j = n) (i < j) { ++i; --j; } {
        sum = sum + 1;
    }
    return sum;
}

/* a typeless fresh for-var (frame 1) shadowed by a body-local `x` (frame 3); the
   update's `++x` drives the for-var while the body's x stays 42. */
int for_typeless_shadow(int n) {
    int sum = 0;
    for (x = 0) (x < n) { ++x; } {
        int x = 42;
        sum = sum + x;
    }
    return sum;
}

/* PPID in a typeless varlist initializer (runs once): i takes j's value, then j
   bumps; i's type is inferred from the init expression. */
int for_typeless_ppid(int n) {
    int j = 5;
    int sum = 0;
    for (i = j++) (i < n) { ++i; } {
        sum = sum + 1;
    }
    return sum * 100 + j;
}

/* nested typeless fors. */
int for_typeless_nested(int rows, int cols) {
    int total = 0;
    for (r = 0) (r < rows) { ++r; } {
        for (c = 0) (c < cols) { ++c; } {
            total = total + 1;
        }
    }
    return total;
}

/* a class to enlarge the per-iteration frame so a leak, if any, is unmistakable. */
SpaceEater(int yum[3]) { }

/* a body local in a `for` body is alloca'd ONCE (hoisted to the function entry
   block), not per iteration: its stack address is the SAME across iterations, so
   ptr[0] == ptr[1] and no leak is reported. */
bool stack_leak_for() {
    int^ ptr[2];
    for (i = 0) (i < 2) { ++i; } {
        int x = 42;
        int y[4] = (1, 2, 3, 4);
        SpaceEater ick;
        x += y[0] + ick.yum[0];
        ptr[i] = ^x;
    }
    return (ptr[0] != ptr[1]);
}

int32 main() {
    __println("sum_for(5) = " + sum_for(5));                // 10
    __println("empty_clauses(4) = " + empty_clauses(4));    // 6
    __println("for_empty_cond(5) = " + for_empty_cond(5));  // 4
    __println("for_true_terminator(4) = " + for_true_terminator(4));    // 4
    __println("two_vars(10) = " + two_vars(10));            // 5
    __println("for_continue(6) = " + for_continue(6));      // 9
    __println("for_break(5) = " + for_break(5));            // 4
    __println("for_grid(3, 4) = " + for_grid(3, 4));        // 12
    __println("for_shadow(3) = " + for_shadow(3));          // 126
    __println("for_typed_noinit(3) = " + for_typed_noinit(3));  // 6
    __println("for_update_reads_body(3) = " + for_update_reads_body(3));    // 3
    __println("for_nested_break(3, 5) = " + for_nested_break(3, 5));        // 6
    __println("for_nested_continue(2, 4) = " + for_nested_continue(2, 4));  // 6
    __println("for_break_in_update(3) = " + for_break_in_update(3));        // 3
    __println("for_ppid_cond(3) = " + for_ppid_cond(3));                    // 3
    __println("for_ppid_varinit(8) = " + for_ppid_varinit(8));              // 306
    __println("for_and_cond(5) = " + for_and_cond(5));                      // 10
    __println("for_empty_body(4) = " + for_empty_body(4));                  // 4
    __println("for_update_local(6) = " + for_update_local(6));              // 6
    __println("for_typeless(5) = " + for_typeless(5));                      // 10
    __println("for_reuse(4) = " + for_reuse(4));                            // 4
    __println("for_noinit_reuse(4) = " + for_noinit_reuse(4));              // 6
    __println("for_typeless_mixed(10) = " + for_typeless_mixed(10));        // 5
    __println("for_two_typeless(10) = " + for_two_typeless(10));            // 5
    __println("for_typeless_shadow(3) = " + for_typeless_shadow(3));        // 126
    __println("for_typeless_ppid(8) = " + for_typeless_ppid(8));            // 306
    __println("for_typeless_nested(3, 4) = " + for_typeless_nested(3, 4));  // 12
    __println("stack leak detected: " + stack_leak_for());                  // false

    /* a reference / iterator as the long-for condition. non-null enters; the update
       nulls it, so the body runs once. */
    {
        int fx = 5;
        for (int^ fp = ^fx) (fp) { fp = nullptr; } {
            __println("for-ref body");
        }
        int fa[2] = (1, 2);
        for (int[] fit = ^fa[0]) (fit) { fit = nullptr; } {
            __println("for-iter body");
        }
    }
    return 0;
}

/*
negatives — one //-block uncommented per run.
*/

/* the update clause may not break. */
//-EXPECT-ERROR: A 'break' statement is not allowed in a for-loop update clause.
//int neg_update_break(int n) {
//    for (int i = 0) (i < n) { break; } {
//        __println(i);
//    }
//    return 0;
//}

/* the update clause may not continue. */
//-EXPECT-ERROR: A 'continue' statement is not allowed in a for-loop update clause.
//int neg_update_continue(int n) {
//    for (int i = 0) (i < n) { continue; } {
//        __println(i);
//    }
//    return 0;
//}

/* the update clause may not return. */
//-EXPECT-ERROR: A 'return' statement is not allowed in a for-loop update clause.
//int neg_update_return(int n) {
//    for (int i = 0) (i < n) { return 0; } {
//        __println(i);
//    }
//    return 0;
//}

/* a typed for-var with no initializer, read in the condition before it is ever
   written, is uninitialized. */
//-EXPECT-ERROR: Use of uninitialized variable 'i'
//int neg_for_var_uninit(int n) {
//    for (int i) (i < n) { ++i; } {
//        __println(i);
//    }
//    return 0;
//}

/* a continue reaches the update before the body assigns x, so the update's read
   of x is not satisfied on every path to it (body-out ∩ continue_accum drops x). */
//-EXPECT-ERROR: Use of uninitialized variable 'x'
//int neg_for_continue_undercut(int n) {
//    int k = 0;
//    for (int x) (k < n) { x = x + 1; } {
//        ++k;
//        if (k < 0) {
//            continue;
//        }
//        x = 7;
//    }
//    return 0;
//}

/* a for is possibly-zero, so a body-only assignment doesn't initialize r after
   the loop. */
//-EXPECT-ERROR: Use of uninitialized variable 'r'
//int neg_for_init_escape(int n) {
//    int r;
//    for (int i = 0) (i < n) { ++i; } {
//        r = 1;
//    }
//    return r;
//}

/* a for with a non-constant condition is never a terminator (it may run zero
   times / fall through), so a non-void function ending in one needs a trailing
   return. (a constant-true for with no break IS a terminator — see
   for_true_terminator above.) */
//-EXPECT-ERROR: must end with a return statement
//int neg_for_no_return(int n) {
//    for (int i = 0) (i < n) { ++i; } {
//        n = n - 1;
//    }
//}

/* a constant-true for with no break never exits, so code after it is
   unreachable. */
//-EXPECT-ERROR: Unreachable statement.
//int neg_for_unreachable_after(int n) {
//    for (int i = 0) (true) { ++i; } {
//        n = n + 1;
//    }
//    return n;
//}

/* return is banned transitively in the update — even inside a nested loop. */
//-EXPECT-ERROR: A 'return' statement is not allowed in a for-loop update clause.
//int neg_update_nested_return(int n) {
//    for (int i = 0) (i < n) {
//        while (true) {
//            return 0;
//        }
//    } {
//        n = n - 1;
//    }
//    return 0;
//}

/* a typeless for-var with no initializer and no enclosing local to reuse has no
   type to infer. */
//-EXPECT-ERROR: Cannot infer the type of 'k'; it has no initializer.
//int neg_typeless_undeclared(int n) {
//    for (k) (k < n) { ++k; } {
//        __println(k);
//    }
//    return 0;
//}

/* a typeless no-init for-var whose name resolves to a constant cannot be reused
   as a loop variable. */
//-EXPECT-ERROR: Cannot use constant 'c' as a loop variable.
//int neg_typeless_const(int n) {
//    const int c = 5;
//    for (c) (n > 0) { } {
//        __println(c);
//    }
//    return 0;
//}

/* likewise a function name. */
//-EXPECT-ERROR: Cannot use function 'g' as a loop variable.
//int neg_typeless_function(int n) {
//    for (g) (n > 0) { } {
//        n = n - 1;
//    }
//    return 0;
//}
//int g() { return 1; }

/* a typeless WITH-init for-var that reuses a constant is an assignment to a
   const. */
//-EXPECT-ERROR: Cannot assign to constant 'c'.
//int neg_typeless_reuse_const(int n) {
//    const int c = 5;
//    for (c = 0) (c < n) { } {
//        __println(c);
//    }
//    return 0;
//}

/* a fresh typeless for-var leaves scope at the end of the loop, so a read after
   the loop is unresolved (no enclosing local of that name exists). */
//-EXPECT-ERROR: Unresolved identifier 'i'.
//int neg_typeless_fresh_escape(int n) {
//    for (i = 0) (i < n) { ++i; } {
//    }
//    return i;
//}

/* the long-for condition clause is author-written, so its type must coerce to bool
   (numeric / pointer-like); a TUPLE, ARRAY, or CLASS value is rejected — the same
   rule as the if / while condition, distinct message. */
//-EXPECT-ERROR: A for condition must be a condition expression
//int neg_for_tuple() {
//    (int, int) t = (1, 2);
//    for (int x = 0) (t) { x++; } {
//        __println("dead");
//    }
//    return 0;
//}

//-EXPECT-ERROR: A for condition must be a condition expression
//int neg_for_array() {
//    int a[2] = (1, 2);
//    for (int x = 0) (a) { x++; } {
//        __println("dead");
//    }
//    return 0;
//}

//-EXPECT-ERROR: A for condition must be a condition expression
//int neg_for_class() {
//    Box(int v_) { }
//    Box b(1);
//    for (int x = 0) (b) { x++; } {
//        __println("dead");
//    }
//    return 0;
//}

/* a VOID-typed for-condition (a `void^` dereference). */
//-EXPECT-ERROR: A for condition must be a condition expression; type 'void' is not
//int neg_for_void() {
//    int x = 5;
//    void^ p = ^x;
//    for (int i = 0) (p^) { i++; } {
//        __println("dead");
//    }
//    return 0;
//}
