/*
test nested functions.

a nested function has read/write visibility to the local variables
and the parameters of the host function.

    int foo(int x) {
        int y = 0;
        z = bar(x);

        int bar(int a) {
            int b = y;
            return b;
        }
        return z;
    }

nested functions may recurse on themselves.

notes:

mulitple nesting is not currently supported.
needs to be revisited.

calling sibling nested functions is not currently supported.
needs to be revisited.

*/

/*
claude says:

- a nested function is lifted to a top-level function; captured host
  locals/params are passed by reference (the host alloca's address), so reads
  see the host's current value at call time and writes propagate back. (A
  read-only capture is observably the same as by-value; the by-value form is a
  deferred optimization.)
- visible in the host body (incl. before its definition) and its own body
  (self-recursion). Deep nesting + sibling calls are unsupported.
*/

/* a nested function using only its own parameter (no capture). */
int basic(int x) {
    int dbl(int a) {
        return a + a;
    }
    return dbl(x);
}

/* a read-only capture of a host local. */
int read_cap(int x) {
    int y = 10;
    int bar(int a) {
        return a + y;
    }
    return bar(x);
}

/* a WRITTEN capture (by reference) — the host sees the accumulated change. */
int write_cap(int x) {
    int acc = 0;
    void add(int a) {
        acc = acc + a;
    }
    add(x);
    add(x);
    return acc;
}

/* a nested function recursing on itself. */
int recurse(int n) {
    int fact(int k) {
        if (k <= 1) {
            return 1;
        }
        return k * fact(k - 1);
    }
    return fact(n);
}

/* the canon shape: the nested function is called before its definition, and
   reads a host local. */
int call_before(int x) {
    int y = 0;
    z = bar(x);
    int bar(int a) {
        int b = y;
        return b + a;
    }
    return z;
}

/* a void nested function with a bare `return;`, capturing host vars by read and
   by write. */
int void_nested(int n) {
    int r = 0;
    void classify() {
        if (n > 0) {
            r = 1;
            return;
        }
        r = -1;
    }
    classify();
    return r;
}

/* a nested function with an optional (default) parameter. */
int with_def(int x) {
    int g(int a, int b = 10) {
        return a + b;
    }
    return g(x);
}

/* a nested forward declaration (signature only), defined later in the body —
   like a file-scope forward declaration. */
int fwd_nested(int x) {
    int bar(int a);
    z = bar(x);
    int bar(int a) {
        return a + 1;
    }
    return z;
}

int32 main() {
    __println("basic(5) = " + basic(5));              // 10
    __println("read_cap(5) = " + read_cap(5));        // 15
    __println("write_cap(5) = " + write_cap(5));      // 10
    __println("recurse(5) = " + recurse(5));          // 120
    __println("call_before(7) = " + call_before(7));  // 7
    __println("void_nested(5) = " + void_nested(5));      // 1
    __println("void_nested(-5) = " + void_nested(-5));    // -1
    __println("with_def(5) = " + with_def(5));            // 15
    __println("fwd_nested(5) = " + fwd_nested(5));        // 6
    return 0;
}

/* deep nesting (a nested function inside a nested function) is unsupported. */
//-EXPECT-ERROR: Nested functions may not contain further nested functions.
//int neg_deep(int x) {
//    int mid(int a) {
//        int inner(int b) {
//            return b;
//        }
//        return inner(a);
//    }
//    return mid(x);
//}

/* a non-void nested function must return on all paths, like any function. */
//-EXPECT-ERROR: Function 'bar' must end with a return statement.
//int neg_noret(int x) {
//    int bar(int a) {
//    }
//    return bar(x);
//}

/* one nested function calling another (a sibling) is unsupported. */
//-EXPECT-ERROR: Calling a sibling nested function is not supported.
//int neg_sibling(int x) {
//    int a(int v) {
//        return b(v);
//    }
//    int b(int v) {
//        return v + 1;
//    }
//    return a(x);
//}

/* a captured host variable must be definitely-assigned at the call. */
//-EXPECT-ERROR: Use of uninitialized variable 'y'.
//int neg_cap_uninit() {
//    int y;
//    int bar() {
//        return y;
//    }
//    return bar();
//}

/* a nested forward declaration with no matching definition is an orphan. */
//-EXPECT-ERROR: Function 'bar' is declared but never defined.
//int neg_decl_no_def(int x) {
//    int bar(int a);
//    return x;
//}

/* two definitions of one nested function is a duplicate definition. */
//-EXPECT-ERROR: Duplicate definition of 'bar'.
//int neg_decl_two_defs(int x) {
//    int bar(int a) {
//        return a;
//    }
//    int bar(int a) {
//        return a + 1;
//    }
//    return bar(x);
//}

/* nested functions do not overload — a same-named nested function with a
   conflicting signature is a duplicate declaration. */
//-EXPECT-ERROR: Duplicate declaration of 'bar'.
//int neg_decl_sig_mismatch(int x) {
//    int bar(int a);
//    int bar(int a, int b) {
//        return a + b;
//    }
//    return x;
//}
