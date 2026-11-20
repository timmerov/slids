/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
test libslids' linear programming implementation.
*/

/*
claude says:
each block builds a small integer program, runs solve() then solve_integral(),
and prints solved_, get_minimum() (the minimized sum of the user variables),
and get_value() of each variable. the expected values were worked by hand:
- integral: the relaxation is already integral, no branching.
- strict: gt/lt rows tighten by one (x > 3 is x >= 4).
- both sides: constants and variables on both sides of the relation.
- one branch: 2x + 2y >= 3 relaxes to 1.5, integral minimum 2 (x + y = 2).
- parity: x == y (two rows) and x + y >= 3 relaxes to 3, integral 4. the
  stop value (3) is never reached, so the search runs to the end.
- two fractions: 2x >= 3 and 2y >= 5 relax to 4, integral 5 (x 2, y 3).
- infeasible: x <= 2 and x >= 5. solved_ false, everything reads -1.
- unused: a variable in no row is 0.
- vote: the header's example. Z 77 with ab = ba = 0 and xa + xb = 77;
  (33,44) and (34,43) are both optimal.
- is_costly false leaves a variable out of the z sum (integral spells the
  default true explicitly for y):
  - free: x + s >= 5, s <= 3, only x costly. z 2, x 2, s 3.
  - parity fixed: the parity rows with only h costly. z 2, h 2, s 2. the
    relaxation is 1.5, so the stop value (2) is reached.
  - no cost: x >= 2, x not costly. z 0, x 2.
  - free still integral: 2s >= 3, x - s >= 0, only x costly. both relax to
    1.5; z 2, x 2, s 2. a free variable is still made integral.
- threshold: solve_integral(t) looks for a z strictly below t.
  - one branch (z 2): t 3 solves z 2; t 2, 1, 0 fail.
  - parity (z 4): t 5 solves z 4; t 4 fails.
  - vote (z 77): t 78 solves z 77; t 77 fails.
  - infeasible: t 10 fails.
  - root integral (x >= 3, y >= 2, z 5): t 6 solves z 5; t 5 fails. the
    relaxation is already integral, so only the root check can reject it.
*/

import slids.linpro;
import slids.string;
alias slids;

void report(
    LP^ lp
) {
    println(String + "solved: " + lp^.solved_ + " z: " + lp^.get_minimum());
}

void report_var(
    LP^ lp,
    String^ name,
    LP:Var^ var
) {
    println(String + " " + name + " = " + lp^.get_value(var));
}

void test_integral() {
    println("---integral: x >= 3, y >= 2---");
    LP lp;
    x = lp.add_var("X");
    y = lp.add_var("Y", true);
    lp.add_row(LP:Expr + x + LP:gte + 3);
    lp.add_row(LP:Expr + y + LP:gte + 2);
    lp.solve();
    lp.solve_integral();
    report(lp);
    report_var(lp, "x", x);
    report_var(lp, "y", y);
}

void test_strict() {
    println("---strict: x > 3, x < 10---");
    LP lp;
    x = lp.add_var("X");
    lp.add_row(LP:Expr + x + LP:gt + 3);
    lp.add_row(LP:Expr + x + LP:lt + 10);
    lp.solve();
    lp.solve_integral();
    report(lp);
    report_var(lp, "x", x);
}

void test_both_sides() {
    println("---both sides: 5 + x - y >= 9, y >= 1 + 0---");
    LP lp;
    x = lp.add_var("X");
    y = lp.add_var("Y");
    lp.add_row(LP:Expr + 5 + x - y + LP:gte + 9);
    lp.add_row(LP:Expr + y + LP:gte + 1 + 0);
    lp.solve();
    lp.solve_integral();
    report(lp);
    report_var(lp, "x", x);
    report_var(lp, "y", y);
}

void test_one_branch() {
    println("---one branch: 2x + 2y >= 3---");
    LP lp;
    x = lp.add_var("X");
    y = lp.add_var("Y");
    lp.add_row(LP:Expr + x + x + y + y + LP:gte + 3);
    lp.solve();
    lp.solve_integral();
    report(lp);
    sum = lp.get_value(x) + lp.get_value(y);
    println(String + " x + y = " + sum);
}

void test_parity() {
    println("---parity: x == y, x + y >= 3---");
    LP lp;
    x = lp.add_var("X");
    y = lp.add_var("Y");
    lp.add_row(LP:Expr + x - y + LP:gte + 0);
    lp.add_row(LP:Expr + x - y + LP:lte + 0);
    lp.add_row(LP:Expr + x + y + LP:gte + 3);
    lp.solve();
    lp.solve_integral();
    report(lp);
    report_var(lp, "x", x);
    report_var(lp, "y", y);
}

void test_two_fractions() {
    println("---two fractions: 2x >= 3, 2y >= 5---");
    LP lp;
    x = lp.add_var("X");
    y = lp.add_var("Y");
    lp.add_row(LP:Expr + x + x + LP:gte + 3);
    lp.add_row(LP:Expr + y + y + LP:gte + 5);
    lp.solve();
    lp.solve_integral();
    report(lp);
    report_var(lp, "x", x);
    report_var(lp, "y", y);
}

void test_infeasible() {
    println("---infeasible: x <= 2, x >= 5---");
    LP lp;
    x = lp.add_var("X");
    lp.add_row(LP:Expr + x + LP:lte + 2);
    lp.add_row(LP:Expr + x + LP:gte + 5);
    lp.solve();
    lp.solve_integral();
    report(lp);
    report_var(lp, "x", x);
}

void test_unused() {
    println("---unused: x >= 2, w in no row---");
    LP lp;
    x = lp.add_var("X");
    w = lp.add_var("W");
    lp.add_row(LP:Expr + x + LP:gte + 2);
    lp.solve();
    lp.solve_integral();
    report(lp);
    report_var(lp, "x", x);
    report_var(lp, "w", w);
}

void test_vote() {
    println("---vote: the header example---");
    LP lp;
    xa = lp.add_var("XA");
    xb = lp.add_var("XB");
    ab = lp.add_var("AB");
    ba = lp.add_var("BA");
    a = 90;
    b = 80;
    w = 100;
    x = 200;
    baw = 40;
    xaw = 150;
    /* a beats w and x. */
    lp.add_row(LP:Expr + a - ab + ba + xa + LP:gte + w);
    lp.add_row(LP:Expr + a - ab + ba + xa + LP:gte + x - xa - xb);
    /* b beats w and x. */
    lp.add_row(LP:Expr + b + ab - ba + xb + LP:gte + w);
    lp.add_row(LP:Expr + b + ab - ba + xb + LP:gte + x - xa - xb);
    /* limit x to a,b votes. */
    lp.add_row(LP:Expr + xa + xb + LP:lte + xaw);
    /* limit a to b votes. */
    lp.add_row(LP:Expr + ab + LP:lte + a);
    /* limit b to a votes. */
    lp.add_row(LP:Expr + ba + LP:lte + baw);
    lp.solve();
    lp.solve_integral();
    report(lp);
    report_var(lp, "xa", xa);
    report_var(lp, "xb", xb);
    report_var(lp, "ab", ab);
    report_var(lp, "ba", ba);
}

void test_free() {
    println("---free: x + s >= 5, s <= 3, s not costly---");
    LP lp;
    x = lp.add_var("X");
    s = lp.add_var("S", false);
    lp.add_row(LP:Expr + x + s + LP:gte + 5);
    lp.add_row(LP:Expr + s + LP:lte + 3);
    lp.solve();
    lp.solve_integral();
    report(lp);
    report_var(lp, "x", x);
    report_var(lp, "s", s);
}

void test_parity_fixed() {
    println("---parity fixed: h == s, h + s >= 3, s not costly---");
    LP lp;
    h = lp.add_var("H");
    s = lp.add_var("S", false);
    lp.add_row(LP:Expr + h - s + LP:gte + 0);
    lp.add_row(LP:Expr + h - s + LP:lte + 0);
    lp.add_row(LP:Expr + h + s + LP:gte + 3);
    lp.solve();
    lp.solve_integral();
    report(lp);
    report_var(lp, "h", h);
    report_var(lp, "s", s);
}

void test_no_cost() {
    println("---no cost: x >= 2, x not costly---");
    LP lp;
    x = lp.add_var("X", false);
    lp.add_row(LP:Expr + x + LP:gte + 2);
    lp.solve();
    lp.solve_integral();
    report(lp);
    report_var(lp, "x", x);
}

void test_free_integral() {
    println("---free still integral: 2s >= 3, x - s >= 0, s not costly---");
    LP lp;
    x = lp.add_var("X");
    s = lp.add_var("S", false);
    lp.add_row(LP:Expr + s + s + LP:gte + 3);
    lp.add_row(LP:Expr + x - s + LP:gte + 0);
    lp.solve();
    lp.solve_integral();
    report(lp);
    report_var(lp, "x", x);
    report_var(lp, "s", s);
}

void threshold_one_branch(
    int threshold
) {
    println(String + "---threshold " + threshold + ": 2x + 2y >= 3---");
    LP lp;
    x = lp.add_var("X");
    y = lp.add_var("Y");
    lp.add_row(LP:Expr + x + x + y + y + LP:gte + 3);
    lp.solve();
    lp.solve_integral(threshold);
    report(lp);
}

void threshold_parity(
    int threshold
) {
    println(String + "---threshold " + threshold + ": x == y, x + y >= 3---");
    LP lp;
    x = lp.add_var("X");
    y = lp.add_var("Y");
    lp.add_row(LP:Expr + x - y + LP:gte + 0);
    lp.add_row(LP:Expr + x - y + LP:lte + 0);
    lp.add_row(LP:Expr + x + y + LP:gte + 3);
    lp.solve();
    lp.solve_integral(threshold);
    report(lp);
}

void threshold_vote(
    int threshold
) {
    println(String + "---threshold " + threshold + ": the header example---");
    LP lp;
    xa = lp.add_var("XA");
    xb = lp.add_var("XB");
    ab = lp.add_var("AB");
    ba = lp.add_var("BA");
    lp.add_row(LP:Expr + 90 - ab + ba + xa + LP:gte + 100);
    lp.add_row(LP:Expr + 90 - ab + ba + xa + LP:gte + 200 - xa - xb);
    lp.add_row(LP:Expr + 80 + ab - ba + xb + LP:gte + 100);
    lp.add_row(LP:Expr + 80 + ab - ba + xb + LP:gte + 200 - xa - xb);
    lp.add_row(LP:Expr + xa + xb + LP:lte + 150);
    lp.add_row(LP:Expr + ab + LP:lte + 90);
    lp.add_row(LP:Expr + ba + LP:lte + 40);
    lp.solve();
    lp.solve_integral(threshold);
    report(lp);
}

void threshold_infeasible(
    int threshold
) {
    println(String + "---threshold " + threshold + ": x <= 2, x >= 5---");
    LP lp;
    x = lp.add_var("X");
    lp.add_row(LP:Expr + x + LP:lte + 2);
    lp.add_row(LP:Expr + x + LP:gte + 5);
    lp.solve();
    lp.solve_integral(threshold);
    report(lp);
}

void threshold_root_integral(
    int threshold
) {
    println(String + "---threshold " + threshold + ": x >= 3, y >= 2---");
    LP lp;
    x = lp.add_var("X");
    y = lp.add_var("Y");
    lp.add_row(LP:Expr + x + LP:gte + 3);
    lp.add_row(LP:Expr + y + LP:gte + 2);
    lp.solve();
    lp.solve_integral(threshold);
    report(lp);
}

void test_threshold() {
    threshold_one_branch(3);
    threshold_one_branch(2);
    threshold_one_branch(1);
    threshold_one_branch(0);
    threshold_parity(5);
    threshold_parity(4);
    threshold_vote(78);
    threshold_vote(77);
    threshold_infeasible(10);
    threshold_root_integral(6);
    threshold_root_integral(5);
}

int32 main() {
    test_integral();
    test_strict();
    test_both_sides();
    test_one_branch();
    test_parity();
    test_two_fractions();
    test_infeasible();
    test_unused();
    test_vote();
    test_free();
    test_parity_fixed();
    test_no_cost();
    test_free_integral();
    test_threshold();
    return 0;
}
