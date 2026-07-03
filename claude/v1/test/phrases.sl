/*
PPID — pre/post-inc/dec via phrases.

Each statement, function/method/ctor argument, tuple-literal element,
if/while/long-for cond, switch scrutinee, long-for init slot, and
&&/|| right operand is its own phrase. Pre advances fire at phrase
entry; post advances fire at phrase exit. A skipped phrase fires
neither.
*/

Counter(int v_ = 0) {
    op+(Counter^ a, Counter^ b) { v_ = a^.v_ + b^.v_; }
    op=(Counter^ rhs) { v_ = rhs^.v_; }
}

void take_two(int a, int b) {
    __println("take_two a=" + a + " b=" + b);
}

int32 main() {
    __println("== test01: multi-pre extract ==");
    /* both pres fire at stmt entry; both reads see the advanced value. */
    int a = 5;
    int x = ++a + ++a;
    __println("a=" + a + " x=" + x);

    __println("== test02: mixed pre/post ==");
    /* pre at entry → 6; reads see 6; post at `;` → 7. */
    int b = 5;
    int y = ++b + b++;
    __println("b=" + b + " y=" + y);

    __println("== test03: if-cond pre ==");
    /* pre fires at cond entry; body sees advanced. */
    int c = 0;
    if (++c > 0) {
        __println("c=" + c);
    }

    __println("== test04: if-cond post ==");
    /* post fires at cond `)` before body. */
    int d = 0;
    if (d++ == 0) {
        __println("d=" + d);
    }

    __println("== test05: while-cond post ==");
    /* post fires each iteration at `)`; body sees advanced. */
    int e = 0;
    while (e++ < 3) {
        __println("e=" + e);
    }
    __println("after-while e=" + e);

    __println("== test06: && short-circuits right ==");
    /* left false → right phrase doesn't run; pre/post don't fire. */
    int f = 0;
    int g = 100;
    if (f != 0 && ++g > 0) {
        __println("unreached");
    }
    __println("g=" + g);

    __println("== test07: || short-circuits right ==");
    /* left true → right phrase doesn't run. */
    int h = 1;
    int i = 100;
    if (h != 0 || i++ > 0) {
        __println("h=" + h);
    }
    __println("i=" + i);

    __println("== test08: per-arg flush ==");
    /* first arg's post fires before second arg evaluates. */
    int j = 5;
    take_two(j++, j);

    __println("== test09: switch scrutinee post ==");
    /* post fires at scrutinee `)` before case body. */
    int k = 0;
    switch (k++) {
    case 0:
        __println("k=" + k);
        break;
    }

    __println("== test10: long-for init slot ==");
    /* slot 2 reads m before post; post fires at slot 2 exit, so cond sees
       advanced m. Per-slot phrase: body prints m=1 n=0. Outer-leaking post
       would give m=0 n=0. */
    for (int m = 0, int n = m++) (m < 2) { m = 99; } {
        __println("m=" + m + " n=" + n);
    }

    __println("== test11: compound-assign single-eval slid LHS ==");
    /* p++^ += rhs: p advances exactly once. */
    Counter arr[3] = (Counter(10), Counter(20), Counter(30));
    Counter[] p = ^arr[0];
    Counter rhs(1);
    p++^ += rhs;
    __println("arr[0]=" + arr[0].v_ + " arr[1]=" + arr[1].v_ + " arr[2]=" + arr[2].v_);
    intptr off = p - ^arr[0];
    __println("p offset=" + off);

    return 0;
}
