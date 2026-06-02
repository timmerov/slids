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
