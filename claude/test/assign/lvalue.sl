/*
test complex expression on the lhs of an assignment.

test const correctness.
a const value cannot be the target of a write.
*/

/*
claude says:

every lvalue form on every assignment-like operation. lvalues: array
element, tuple slot, class field, deref, and composed chains (multi-dim,
deref-then-field, iterator element). operations: =, the augmented-assign
family (+= -= *= /= %= <<= >>= &= |= ^= and logical &&= ||= ^^=), ++/--,
<-- (move), <--> (swap). plus address-once (a side-effecting index is
evaluated exactly once) and negatives for the rejected leaf cases.

THE CONST-LVALUE WALL (landed 2026-07-26): a const value cannot be the
target of a write — `=`, the augmented family, `++`/`--`, and a move's
TARGET all reject, through every lvalue form (element, slot, field,
deref, whole aggregate, whole class). classify walks the chain's DECLARED
types (constAwareLvalueType — the stamped caches strip const): a const
aggregate freezes its parts, a const pointee freezes the deref; a SHALLOW
pointer (`(const int)^`) still reseats — the pointer's value is the
address alone. EXEMPT BY CANON (the lifecycle rule, uniformly — no
whole-variable carve-out): DELETE and a MOVE'S SOURCE — const constrains
a live value, delete/move-from end the value's life, and the compiler's
null afterward is a tombstone, not a mutation. The FLOW RULE closed the
wall's escape (2026-07-26, mutable.sl owns its canon): dropping const
rejects at every position and depth, so `int[] it = ^carr[0]` no longer
launders; param enforcement landed the same day (`mutable` opts out — see
tick above). DEFERRED: SWAP (its questions outrank it) and const METHODS
(a method call on a const class lvalue is unchecked — the known hole).
*/

import string;

Simple(int x_, int y_) {
}

/* a side effect (bumps its referent) used to prove a complex-lvalue index is
   evaluated exactly once. (`mutable`: param enforcement landed — a body
   writes through a reference param only when it opts out of the munge.) */
int tick(mutable int^ c) {
    c^ += 1;
    return 1;
}

int32 main() {

    {
        int arr[2];
        arr[0] = 1;
        arr[0] += 7;
        arr[1] <-- arr[0];
        arr[0] = 2;
        arr[0] <--> arr[1];
        println(String + "arr = [" + arr[0] + "," + arr[1] + "]");
    }

    {
        (int, int) tuple;
        tuple[0] = 3;
        tuple[0] += 8;
        tuple[1] <-- tuple[0];
        tuple[0] = 4;
        tuple[0] <--> tuple[1];
        println(String + "tuple = (" + tuple[0] + "," + tuple[1] + ")");
    }

    {
        Simple simple;
        simple.x_ = 5;
        simple.x_ += 9;
        simple.y_ <-- simple.x_;
        simple.x_ = 6;
        simple.x_ <--> simple.y_;
        println(String + "simple. x = " + simple.x_ + " y = " + simple.y_);
    }

    {
        int x;
        int y;
        int^ p = ^x;
        int^ q = ^y;
        p^ = 10;
        p^ += 11;
        q^ <-- p^;
        p^ = 12;
        p^ <--> q^;
        println(String + "p^ = " + p^ + " q^ = " + q^);
    }

    /* address-once: a side-effecting index runs a single time. */
    {
        int calls = 0;
        int arr[3];
        arr[0] = 10;
        arr[1] = 20;
        arr[2] = 30;
        arr[tick(^calls)] += 100;
        println(String + "addr-once: calls=" + calls + " arr1=" + arr[1]);
    }

    /* ++ / -- on every complex lvalue form (statement form). */
    {
        int arr[2];
        arr[0] = 5;
        arr[1] = 5;
        arr[0]++;
        arr[1]--;
        (int, int) t;
        t[0] = 5;
        t[0]++;
        Simple s;
        s.x_ = 5;
        s.x_++;
        int z;
        int^ pz = ^z;
        pz^ = 5;
        pz^++;
        println(String + "incdec: a0=" + arr[0] + " a1=" + arr[1] + " t0=" + t[0]
                  + " sx=" + s.x_ + " z=" + z);
    }

    /* the whole augmented-assign operator family on a complex lvalue. */
    {
        int a[1];
        a[0] = 20;
        a[0] -= 4;
        a[0] *= 3;
        a[0] /= 2;
        a[0] %= 7;
        a[0] <<= 4;
        a[0] >>= 1;
        a[0] &= 12;
        a[0] |= 1;
        a[0] ^= 3;
        println(String + "ops: a0=" + a[0]);
    }

    /* logical augmented-assign on a bool array element. */
    {
        bool b[1];
        b[0] = true;
        b[0] &&= false;
        b[0] ||= true;
        b[0] ^^= true;
        println(String + "logic: b0=" + b[0]);
    }

    /* composed chain: a multi-dim array element. */
    {
        int g[2][2];
        g[0][0] = 1;
        g[0][1] = 2;
        g[1][0] = 3;
        g[1][1] = 4;
        g[0][0] += 10;
        g[1][1] *= 5;
        println(String + "multidim: g00=" + g[0][0] + " g11=" + g[1][1]);
    }

    /* composed chain: deref then field. */
    {
        Simple s;
        s.x_ = 7;
        Simple^ ps = ^s;
        ps^.x_ += 3;
        println(String + "derefield: sx=" + s.x_);
    }

    /* composed chain: an iterator element. */
    {
        int data[3];
        data[0] = 1;
        data[1] = 2;
        data[2] = 3;
        int[] it = ^data[0];
        it[1] += 20;
        println(String + "iter: data1=" + data[1]);
    }

    /* non-int leaves: a narrow integer flexes, a float computes. */
    {
        int8 a[1];
        a[0] = 10;
        a[0] += 5;
        float f[1];
        f[0] = 1.5;
        f[0] += 2.0;
        println(String + "widths: a0=" + a[0] + " f0=" + f[0]);
    }

    /* move / swap onto the SAME complex lvalue is a self-op — rejected (the
       negatives at the bottom cover each lvalue form: deref, field, index).
       a SIDE-EFFECTING index (a call) is NOT provably the same element, so a
       self-LOOKING swap through it is ALLOWED. tick() bumps c on each evaluation
       — it runs twice. */
    {
        int c = 0;
        int sa[2] = (8, 9);
        sa[tick(^c)] <--> sa[tick(^c)];
        println(String + "tick c=" + c + " sa1=" + sa[1]);                // tick c=2 sa1=9
    }

    /* --- the const-lvalue wall: the exempt operations first. --- */

    /* reads from const are free; a SHALLOW pointer reseats (its value is
       the address alone — only the pointee is frozen). */
    {
        const int carr[2] = (10, 20);
        int rsum = carr[0] + carr[1];
        int other = 7;
        (const int)^ sq = ^carr[0];
        int before = sq^;
        sq = ^other;
        println(String + "const-read: sum=" + rsum + " b=" + before + " q=" + sq^);
    }

    /* DELETE on a const pointer: allowed, and the tombstone null lands —
       the lifecycle exemption (destruction ends the value; the null is the
       compiler's own bookkeeping). */
    {
        const Simple^ cp = new Simple(3, 4);
        int rx = cp^.x_;
        delete cp;
        println(String + "const-delete: x=" + rx + " null=" + (cp == nullptr));
    }

    /* a MOVE'S SOURCE is exempt the same way — moving from a const pointer
       nulls it, and from a const SLOT of an aggregate too (uniform: no
       whole-variable carve-out). */
    {
        int v = 9;
        const int^ cp2 = ^v;
        (const int)^ dst <-- cp2;
        int w = 4;
        (const (int^), int) ctp = (^w, 1);
        (const int)^ e2 <-- ctp[0];   // const is RECURSIVE: the slot's const
                                      // freezes the pointee too — the copy
                                      // keeps it; only the source's ZEROING
                                      // is the lifecycle exemption
        println(String + "const-movefrom: d=" + dst^ + " pnull=" + (cp2 == nullptr)
                  + " e=" + e2^ + " snull=" + (ctp[0] == nullptr));
    }

    /* augmented assign on a reference leaf is rejected. */
    //-EXPECT-ERROR: Arithmetic is not allowed on a reference
    //{
    //    int x; int y;
    //    x = 1; y = 2;
    //    (int^, int^) tr;
    //    tr[0] = ^x; tr[1] = ^y;
    //    tr[0] += 1;
    //    println(String + "" + tr[0]^);
    //}

    /* bitwise augmented assign on a float leaf is rejected. */
    //-EXPECT-ERROR: Bitwise '&' not defined on floating-point
    //{
    //    float f[1];
    //    f[0] = 1.0;
    //    f[0] &= 2.0;
    //    println(String + "" + f[0]);
    //}

    /* a narrowing augmented assign through a complex lvalue is rejected. */
    //-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
    //{
    //    int wide; wide = 300;
    //    int8 a[1];
    //    a[0] = 1;
    //    a[0] += wide;
    //    println(String + "" + a[0]);
    //}

    /* self-move / self-swap onto the SAME complex lvalue: a DEREF, a class FIELD,
       and a provably-same INDEX (a literal or a bare variable) each name ONE
       element — rejected. structural lvalue-equality: same base + same deref /
       field / index. */
    //-EXPECT-ERROR: Cannot move a value onto itself
    //{
    //    int x = 37; int^ p = ^x;
    //    p^ <-- p^;
    //}

    //-EXPECT-ERROR: Cannot swap a value with itself
    //{
    //    int x = 37; int^ p = ^x;
    //    p^ <--> p^;
    //}

    //-EXPECT-ERROR: Cannot move a value onto itself
    //{
    //    Simple s(1, 2);
    //    s.x_ <-- s.x_;
    //}

    //-EXPECT-ERROR: Cannot swap a value with itself
    //{
    //    Simple s(1, 2);
    //    s.x_ <--> s.x_;
    //}

    //-EXPECT-ERROR: Cannot swap a value with itself
    //{
    //    int a[2]; a[0] = 1; a[1] = 2;
    //    a[0] <--> a[0];
    //    println(String + "" + a[0]);
    //}

    //-EXPECT-ERROR: Cannot move a value onto itself
    //{
    //    int a[2]; int i = 0; a[0] = 1; a[1] = 2;
    //    a[i] <-- a[i];
    //    println(String + "" + a[i]);
    //}

    /* `a[i++] <--> a[i++]` is the SAME element under PPID (lowers to `a[i] <-->
       a[i]; i++; i++`) and is rejected too — isSameIndex peels the PPID bump to
       its operand `i`, so the self-swap is seen at classify. */
    //-EXPECT-ERROR: Cannot swap a value with itself
    //{
    //    int a[2]; int i = 0; a[0] = 1; a[1] = 2;
    //    a[i++] <--> a[i++];
    //    println(String + "" + a[0]);
    //}

    /* --- the const-lvalue wall: every form, every write family. --- */

    /* a const array element: plain assign... */
    //-EXPECT-ERROR: Cannot write to a const value
    //{
    //    const int ca[2] = (1, 2);
    //    ca[0] = 9;
    //    println(String + "" + ca[0]);
    //}

    /* ...the augmented family... */
    //-EXPECT-ERROR: Cannot write to a const value
    //{
    //    const int ca[2] = (1, 2);
    //    ca[0] += 9;
    //    println(String + "" + ca[0]);
    //}

    /* ...and the bump. */
    //-EXPECT-ERROR: Cannot write to a const value
    //{
    //    const int ca[2] = (1, 2);
    //    ca[0]++;
    //    println(String + "" + ca[0]);
    //}

    /* a const tuple slot. */
    //-EXPECT-ERROR: Cannot write to a const value
    //{
    //    const (int, int) ct = (3, 4);
    //    ct[0] = 9;
    //    println(String + "" + ct[0]);
    //}

    /* a PARTIALLY const tuple: the const slot walls... */
    //-EXPECT-ERROR: Cannot write to a const value
    //{
    //    (const int, int) pt = (3, 4);
    //    pt[0] = 9;
    //    println(String + "" + pt[0]);
    //}

    /* a const class field. */
    //-EXPECT-ERROR: Cannot write to a const value
    //{
    //    const Simple cs(3, 4);
    //    cs.x_ = 9;
    //    println(String + "" + cs.x_);
    //}

    /* a DEEP const pointer: the reseat... */
    //-EXPECT-ERROR: Cannot write to a const value
    //{
    //    int x = 1; int y = 2;
    //    const int^ dp = ^x;
    //    dp = ^y;
    //    println(String + "" + dp^);
    //}

    /* ...and the write through it. */
    //-EXPECT-ERROR: Cannot write to a const value
    //{
    //    int x = 1;
    //    const int^ dp = ^x;
    //    dp^ = 9;
    //    println(String + "" + dp^);
    //}

    /* a SHALLOW pointer's pointee (the reseat is the positive above). */
    //-EXPECT-ERROR: Cannot write to a const value
    //{
    //    int x = 1;
    //    (const int)^ sp = ^x;
    //    sp^ = 9;
    //    println(String + "" + sp^);
    //}

    /* a const iterator element. */
    //-EXPECT-ERROR: Cannot write to a const value
    //{
    //    const int ca[2] = (1, 2);
    //    (const int)[] ci = ^ca[0];
    //    ci[1] = 9;
    //    println(String + "" + ci[1]);
    //}

    /* the WHOLE const aggregate (its stored value carries const slots)... */
    //-EXPECT-ERROR: Cannot write to a const value
    //{
    //    const int ca[2] = (1, 2);
    //    int src[2] = (8, 9);
    //    ca = src;
    //    println(String + "" + ca[0]);
    //}

    /* ...and the whole const class (no op= dispatch — the wall is first). */
    //-EXPECT-ERROR: Cannot write to a const value
    //{
    //    const Simple cs(3, 4);
    //    Simple s2(5, 6);
    //    cs = s2;
    //    println(String + "" + cs.x_);
    //}

    /* a MOVE'S TARGET is an author write (only its SOURCE is exempt). */
    //-EXPECT-ERROR: Cannot write to a const value
    //{
    //    const int ca[2] = (1, 2);
    //    int y = 5;
    //    ca[0] <-- y;
    //    println(String + "" + ca[0]);
    //}

    return 0;
}
