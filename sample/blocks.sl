/*
unit tests for default block names.
name-able blocks (for, while top/bottom, switch) get a default label
matching their construct keyword. an explicit ":label;" overrides it.
println traces inside each block reveal the executed path.
*/

int32 main() {
    __println("== test01: for default name break ==");
    for int i in (0..3) {
        __println("for i=" + i);
        if (i == 1) {
            break for;
        }
        __println("after-if i=" + i);
    }
    __println("after-for");

    __println("== test02: for default name continue ==");
    for int ic in (0..3) {
        __println("for ic=" + ic);
        if (ic == 1) {
            continue for;
        }
        __println("body ic=" + ic);
    }
    __println("after-for");

    __println("== test03: while default name break ==");
    int n = 0;
    while (n < 3) {
        __println("while n=" + n);
        if (n == 1) {
            break while;
        }
        n = n + 1;
    }
    __println("after-while n=" + n);

    __println("== test04: while default name continue ==");
    int m = 0;
    while (m < 3) {
        m = m + 1;
        __println("while m=" + m);
        if (m == 2) {
            continue while;
        }
        __println("body m=" + m);
    }
    __println("after-while");

    __println("== test05: bottom-while default name break ==");
    int b = 0;
    while {
        __println("bw b=" + b);
        b = b + 1;
        if (b == 2) {
            break while;
        }
    } (b < 5);
    __println("after-bw b=" + b);

    __println("== test06: switch default name break ==");
    switch (2) {
    case 1:
        __println("case 1");
        break switch;
    case 2:
        __println("case 2");
        break switch;
    case 3:
        __println("case 3");
        break switch;
    }
    __println("after-switch");

    __println("== test07: nested fors, inner default vs outer label ==");
    for int o in (0..3) {
        __println("outer o=" + o);
        for int j in (0..3) {
            __println("  inner j=" + j);
            if (j == 1) {
                break for;
            }
            __println("  after-if j=" + j);
        }
        __println("after-inner o=" + o);
    } :outer;
    __println("after-outer");

    __println("== test08: nested fors, inner break to outer label ==");
    for int oo in (0..3) {
        __println("outer oo=" + oo);
        for int jj in (0..3) {
            __println("  inner jj=" + jj);
            if (oo == 1 && jj == 1) {
                break outer;
            }
            __println("  after-if jj=" + jj);
        }
    } :outer;
    __println("after-outer");

    __println("== test09: for-tuple default name break ==");
    for int t in (10, 20, 30) {
        __println("t=" + t);
        if (t == 20) {
            break for;
        }
    }
    __println("after-for-tuple");

    __println("== test10: switch inside for, break switch only kills switch ==");
    for int k in (0..2) {
        __println("k=" + k);
        switch (k) {
        case 0:
            __println("  case 0");
            break switch;
        case 1:
            __println("  case 1");
            break switch;
        }
        __println("after-switch k=" + k);
    }
    __println("after-for");

    __println("== test11: naked break in for ==");
    for int p in (0..3) {
        __println("for p=" + p);
        if (p == 1) {
            break;
        }
        __println("after-if p=" + p);
    }
    __println("after-for");

    __println("== test12: naked continue in for ==");
    for int q in (0..3) {
        __println("for q=" + q);
        if (q == 1) {
            continue;
        }
        __println("body q=" + q);
    }
    __println("after-for");

    __println("== test13: naked break in switch inside for ==");
    for int r in (0..2) {
        __println("r=" + r);
        switch (r) {
        case 0:
            __println("  case 0");
            break;
        case 1:
            __println("  case 1");
            break;
        }
        __println("after-switch r=" + r);
    }
    __println("after-for");

    __println("== test14: naked break in while ==");
    int s = 0;
    while (s < 3) {
        __println("while s=" + s);
        if (s == 1) {
            break;
        }
        s = s + 1;
    }
    __println("after-while s=" + s);

    __println("== test15: naked continue in while ==");
    int u = 0;
    while (u < 3) {
        u = u + 1;
        __println("while u=" + u);
        if (u == 2) {
            continue;
        }
        __println("body u=" + u);
    }
    __println("after-while");

    __println("== test16: break 1 in switch in for exits the for ==");
    for int v in (0..3) {
        __println("for v=" + v);
        switch (v) {
        case 1:
            __println("  case 1, break 1");
            break 1;
        }
        __println("after-switch v=" + v);
    }
    __println("after-for");

    __println("== test17: continue 1 in switch in for skips iteration ==");
    for int w in (0..3) {
        __println("for w=" + w);
        switch (w) {
        case 1:
            __println("  case 1, continue 1");
            continue 1;
        }
        __println("after-switch w=" + w);
    }
    __println("after-for");

    __println("== test18: break 2 counts loops only, ignores switch ==");
    for int x in (0..3) {
        __println("outer x=" + x);
        for int y in (0..3) {
            __println("  inner y=" + y);
            switch (y) {
            case 1:
                __println("    case 1, break 2");
                break 2;
            }
            __println("  after-switch y=" + y);
        }
        __println("after-inner x=" + x);
    }
    __println("after-outer");

    __println("== test19: continue 2 counts loops only, ignores switch ==");
    for int xa in (0..2) {
        __println("outer xa=" + xa);
        for int ya in (0..3) {
            __println("  inner ya=" + ya);
            switch (ya) {
            case 1:
                __println("    case 1, continue 2");
                continue 2;
            }
            __println("  after-switch ya=" + ya);
        }
        __println("after-inner xa=" + xa);
    }
    __println("after-outer");

    __println("== test20: nested switches, outer named ==");
    switch (1) {
    case 1:
        __println("outer case 1");
        switch (2) {
        case 1:
            __println("  inner case 1");
            break switch;
        case 2:
            __println("  inner case 2, break outer_sw");
            break outer_sw;
        }
        __println("after-inner-switch");
    case 2:
        __println("outer case 2 (should not run)");
    } :outer_sw;
    __println("after-outer-switch");

    return 0;
}
