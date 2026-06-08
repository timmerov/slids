/*
test loop over range.

    for (var : range) {body}

var and range are required.
body may be empty.
parentheses and curly brackets are required.

a range is:

    start .. cmp end op step

start, end, step are range-expressions compatible with var.
cmp is optional, < (default) > <= >= !=
op step are optional, default +1.
op is + - * / << >>

a range-expression is a unary-expression.
for documentation purposes, it's a constant, a variable,
or a parenthesized expression.

examples:

    0..10
    0..<=10
    10..>0-1
    begin..end+step
    (3*x)..(4*x)+(y/2)
    1..<=256<<1

there are no checks for infinite loops.
this may change in the future.

ranges where start and end are constants and
the first condition test is false is a compile error.

desugars to:

    for (
        type var = start,
        type _$end = end,
        type _$step = step
    ) (
        var cmp _$end
    ) {
        var = var op _$step;
    } {
        body
    }

start, end, step are evaluated once at the start of the loop.

notes:
does cmp == make any sense?
does op % & | ^ && || ^^ make any sense?
lean no.
*/

/* basic ascending range, default cmp `<` step `+1`: body sees 0..n-1. */
int sum_range(int n) {
    int s = 0;
    for (int i : 0..n) {
        s = s + i;
    }
    return s;
}

/* inclusive upper bound via `<=`. */
int sum_incl(int n) {
    int s = 0;
    for (int i : 0..<=n) {
        s = s + i;
    }
    return s;
}

/* descending: `>` cmp with a `-` step. */
int sum_down(int n) {
    int s = 0;
    for (int i : n..>0-1) {
        s = s + i;
    }
    return s;
}

/* `<<` step — count the powers of two up to n inclusive. */
int powers(int n) {
    int c = 0;
    for (int i : 1..<=n<<1) {
        c = c + 1;
    }
    return c;
}

/* `*` step. */
int mul_step(int n) {
    int c = 0;
    for (int i : 1..<=n*3) {
        c = c + 1;
    }
    return c;
}

/* parenthesized compound bounds + step. */
int parens(int x, int y) {
    int c = 0;
    for (int i : (3*x)..(4*x)+(y/2)) {
        c = c + 1;
    }
    return c;
}

/* variable bounds + variable step (not constant -> no empty-range check). */
int var_bounds(int lo, int hi, int by) {
    int c = 0;
    for (int i : lo..hi+by) {
        c = c + 1;
    }
    return c;
}

/* a constant range whose first test is TRUE is valid (not flagged). */
int const_range() {
    int s = 0;
    for (int i : 0..5) {
        s = s + i;
    }
    return s;
}

/* an empty body still parses and runs the loop. */
int empty_body_range(int n) {
    int last = 0;
    for (int i : 0..n) {
    }
    return last;
}

/* break exits a ranged-for. */
int range_break(int n) {
    int last = 0;
    for (int i : 0..100) {
        if (i >= n) {
            break;
        }
        last = i;
    }
    return last;
}

/* continue jumps to the update, so the loop var still advances (else it would
   spin) — sum the odds in [0,n). */
int range_continue(int n) {
    int s = 0;
    for (int i : 0..n) {
        if (i % 2 == 0) {
            continue;
        }
        s = s + i;
    }
    return s;
}

/* `/` step with `>=` cmp (descending by integer halving). */
int div_step(int n) {
    int c = 0;
    for (int i : n..>=1/2) {
        c = c + 1;
    }
    return c;
}

/* `>>` step with `>=` cmp. */
int shr_step(int n) {
    int c = 0;
    for (int i : n..>=1>>1) {
        c = c + 1;
    }
    return c;
}

/* `!=` cmp (valid — loops while i != end). */
int ne_cmp(int n) {
    int s = 0;
    for (int i : 0..!=n) {
        s = s + i;
    }
    return s;
}

/* a float range. */
int float_range() {
    int c = 0;
    for (float f : 0.0..1.0+0.25) {
        c = c + 1;
    }
    return c;
}

/* a char range. */
int char_range() {
    int c = 0;
    for (char ch : 'a'..'e') {
        c = c + 1;
    }
    return c;
}

/* boundary: start == end with `<=` iterates once (a valid constant range). */
int boundary_le() {
    int c = 0;
    for (int i : 5..<=5) {
        c = c + 1;
    }
    return c;
}

/* a unary-minus operand (negative start). */
int unary_operand(int n) {
    int c = 0;
    for (int i : -n..0) {
        c = c + 1;
    }
    return c;
}

int five() {
    return 5;
}

/* a call as a range bound (evaluated once into the hidden _$end). */
int call_operand() {
    int s = 0;
    for (int i : 0..five()) {
        s = s + i;
    }
    return s;
}

/* nested ranged-fors. */
int nested_range(int rows, int cols) {
    int total = 0;
    for (int r : 0..rows) {
        for (int c : 0..cols) {
            total = total + 1;
        }
    }
    return total;
}

/* PPID in a bound — evaluated once at loop start (n bumps once, not per pass). */
int ppid_operand(int n) {
    int c = 0;
    for (int i : 0..n++) {
        c = c + 1;
    }
    return c * 10 + n;
}

/* a typeless ranged loop var — its type is inferred from `start` (int here). */
int range_typeless(int n) {
    int s = 0;
    for (i : 0..n) {
        s = s + i;
    }
    return s;
}

/* a typeless ranged loop var whose name is already in scope reuses that local;
   after the loop it holds the last iterated value (n - here the loop stops once
   row reaches n, so row == n). */
int range_typeless_reuse(int n) {
    int row = 99;
    for (row : 0..n) {
    }
    return row;
}

/* a typeless char range — the loop var infers `char` from the start literal, so
   the synthesized `var = var + step` stays char. */
int range_typeless_char() {
    int c = 0;
    for (ch : 'a'..'e') {
        c = c + 1;
    }
    return c;
}

/* typeless with an inclusive `<=` bound. */
int range_typeless_incl(int n) {
    int s = 0;
    for (i : 0..<=n) {
        s = s + i;
    }
    return s;
}

/* typeless with an `op step` clause (end = n, step = *3). */
int range_typeless_step(int n) {
    int c = 0;
    for (i : 1..<=n*3) {
        c = c + 1;
    }
    return c;
}

/* typeless over int64 bounds — the loop var infers int64 from `start`, and the
   synthesized `_$end` takes the loop var's type (matching an explicit range). */
int range_typeless_wide(int64 lo, int64 hi) {
    int c = 0;
    for (i : lo..hi) {
        c = c + 1;
    }
    return c;
}

/* a labeled break from the inner loop exits the OUTER ranged-for. */
int range_labeled_break() {
    int c = 0;
    for (int a : 0..3) {
        for (int b : 0..3) {
            c = c + 1;
            if (b == 1) {
                break scan;
            }
        }
    } :scan;
    return c;
}

/* a numbered break exits both ranged-fors at once. */
int range_numbered_break() {
    int c = 0;
    for (int a : 0..3) {
        for (int b : 0..3) {
            c = c + 1;
            if (b == 1) {
                break 2;
            }
        }
    }
    return c;
}

/* a labeled continue restarts the OUTER ranged-for. */
int range_labeled_continue() {
    int c = 0;
    for (int a : 0..3) {
        for (int b : 0..3) {
            if (b == 1) {
                continue outer;
            }
            c = c + 1;
        }
    } :outer;
    return c;
}

/* nested TYPELESS ranged-fors — each synthesized `_$end` is minted per loop in
   desugar, so the inner bound can't clobber the outer's. */
int nested_typeless_range(int a, int b) {
    int total = 0;
    for (i : 0..a) {
        for (j : 0..b) {
            total = total + 1;
        }
    }
    return total;
}

/* a PRE-increment in a bound — bumped once at loop start (like the post-inc case,
   but the bumped value IS the bound). n=5 -> bound 6, 6 iterations, n ends 6. */
int ppid_pre_operand(int n) {
    int c = 0;
    for (int i : 0..++n) {
        c = c + 1;
    }
    return c * 10 + n;
}

/* an explicit int64-typed loop var over int64 bounds. */
int range_int64_typed(int64 lo, int64 hi) {
    int c = 0;
    for (int64 i : lo..hi) {
        c = c + 1;
    }
    return c;
}

/* a runtime-empty non-constant range (lo > hi) iterates zero times — no compile
   error, since the bounds aren't constants. */
int range_runtime_empty(int lo, int hi) {
    int c = 0;
    for (int i : lo..hi) {
        c = c + 1;
    }
    return c;
}

/* the bounds need only be COMPATIBLE with the loop var, not the same type — a
   char bound widens into an int var: 97..'e'(101) -> 4. */
int range_char_bound() {
    int c = 0;
    for (int i : 97..'e') {
        c = c + 1;
    }
    return c;
}

/* the reverse widening — an int bound into an int64 var. */
int range_int_bound_wide(int n) {
    int c = 0;
    for (int64 i : 0..n) {
        c = c + 1;
    }
    return c;
}

int32 main() {
    __println("sum_range(5) = " + sum_range(5));        // 10
    __println("sum_incl(5) = " + sum_incl(5));          // 15
    __println("sum_down(4) = " + sum_down(4));          // 10
    __println("powers(256) = " + powers(256));          // 9
    __println("mul_step(27) = " + mul_step(27));        // 4
    __println("parens(2, 2) = " + parens(2, 2));        // 2
    __println("var_bounds(0, 10, 2) = " + var_bounds(0, 10, 2));    // 5
    __println("const_range() = " + const_range());      // 10
    __println("empty_body_range(5) = " + empty_body_range(5));      // 0
    __println("range_break(5) = " + range_break(5));    // 4
    __println("range_continue(6) = " + range_continue(6));          // 9
    __println("div_step(100) = " + div_step(100));      // 7
    __println("shr_step(256) = " + shr_step(256));      // 9
    __println("ne_cmp(5) = " + ne_cmp(5));              // 10
    __println("float_range() = " + float_range());      // 4
    __println("char_range() = " + char_range());        // 4
    __println("boundary_le() = " + boundary_le());      // 1
    __println("unary_operand(3) = " + unary_operand(3));            // 3
    __println("call_operand() = " + call_operand());    // 10
    __println("nested_range(3, 4) = " + nested_range(3, 4));        // 12
    __println("ppid_operand(5) = " + ppid_operand(5));  // 56
    __println("range_typeless(5) = " + range_typeless(5));              // 10
    __println("range_typeless_reuse(7) = " + range_typeless_reuse(7));  // 7
    __println("range_typeless_char() = " + range_typeless_char());      // 4
    __println("range_typeless_incl(5) = " + range_typeless_incl(5));    // 15
    __println("range_typeless_step(27) = " + range_typeless_step(27));  // 4
    __println("range_typeless_wide(2, 7) = " + range_typeless_wide(2, 7));  // 5
    __println("range_labeled_break() = " + range_labeled_break());      // 2
    __println("range_numbered_break() = " + range_numbered_break());    // 2
    __println("range_labeled_continue() = " + range_labeled_continue());  // 3
    __println("nested_typeless_range(3, 4) = " + nested_typeless_range(3, 4));  // 12
    __println("ppid_pre_operand(5) = " + ppid_pre_operand(5));          // 66
    __println("range_int64_typed(2, 7) = " + range_int64_typed(2, 7));  // 5
    __println("range_runtime_empty(5, 0) = " + range_runtime_empty(5, 0));  // 0
    __println("range_char_bound() = " + range_char_bound());            // 4
    __println("range_int_bound_wide(5) = " + range_int_bound_wide(5));  // 5
    return 0;
}

/*
negatives — one //-block uncommented per run.
*/

/* a constant empty range (5 < 0 is false) — the body can never run. */
//-EXPECT-ERROR: Invalid range.
//int neg_range_empty() {
//    for (int i : 5..0) {
//        __println(i);
//    }
//    return 0;
//}

/* constant empty range via `>` (0 > 10 is false). */
//-EXPECT-ERROR: Invalid range.
//int neg_range_gt() {
//    for (int i : 0..>10) {
//        __println(i);
//    }
//    return 0;
//}

/* constant empty range via `!=` (0 != 0 is false). */
//-EXPECT-ERROR: Invalid range.
//int neg_range_ne() {
//    for (int i : 0..!=0) {
//        __println(i);
//    }
//    return 0;
//}

/* constant empty range via `<=` (5 <= 4 is false). */
//-EXPECT-ERROR: Invalid range.
//int neg_range_le() {
//    for (int i : 5..<=4) {
//        __println(i);
//    }
//    return 0;
//}

/* constant empty range via `>=` (0 >= 10 is false). */
//-EXPECT-ERROR: Invalid range.
//int neg_range_ge() {
//    for (int i : 0..>=10) {
//        __println(i);
//    }
//    return 0;
//}

/* boundary: 5 < 5 is false -> empty. */
//-EXPECT-ERROR: Invalid range.
//int neg_range_boundary() {
//    for (int i : 5..<5) {
//        __println(i);
//    }
//    return 0;
//}

/* a constant float empty range (0.0 < -1.0 is false). */
//-EXPECT-ERROR: Invalid range.
//int neg_range_float() {
//    for (float f : 0.0..-1.0) {
//        __println(f);
//    }
//    return 0;
//}

/* a typeless empty range is flagged the same as an explicit one. */
//-EXPECT-ERROR: Invalid range.
//int neg_range_typeless_empty() {
//    for (i : 5..0) {
//        __println(i);
//    }
//    return 0;
//}

/* a typeless loop var infers int from `start`, so an int64 bound narrows — same
   rejection as the explicit `for (int i : 0..hi)`. */
//-EXPECT-ERROR: Cannot implicitly narrow 'int64' to 'int'; use an explicit type conversion.
//int neg_range_typeless_narrow(int64 hi) {
//    for (i : 0..hi) {
//        __println(i);
//    }
//    return 0;
//}

/* `==` is not a valid range comparator (only < <= > >= != are). */
//-EXPECT-ERROR: Invalid range comparator
//int neg_range_eqeq() {
//    for (int i : 0..==5) {
//        __println(i);
//    }
//    return 0;
//}

/* `%` is not a valid range step operator (only + - * / << >> are). */
//-EXPECT-ERROR: Invalid range step operator
//int neg_range_mod_step() {
//    for (int i : 0..<=10 % 2) {
//        __println(i);
//    }
//    return 0;
//}

/* a bitwise `&` step operator is rejected the same way. */
//-EXPECT-ERROR: Invalid range step operator
//int neg_range_and_step() {
//    for (int i : 0..<=10 & 2) {
//        __println(i);
//    }
//    return 0;
//}

/* a logical `&&` step operator is rejected the same way. */
//-EXPECT-ERROR: Invalid range step operator
//int neg_range_andand_step() {
//    for (int i : 0..<=10 && 2) {
//        __println(i);
//    }
//    return 0;
//}
