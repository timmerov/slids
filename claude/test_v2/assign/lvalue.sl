/*
test complex expression on the lhs of an assignment.

*/

/*
claude says:

every lvalue form on every assignment-like operation. lvalues: array
element, tuple slot, class field, deref, and composed chains (multi-dim,
deref-then-field, iterator element). operations: =, the augmented-assign
family (+= -= *= /= %= <<= >>= &= |= ^= and logical &&= ||= ^^=), ++/--,
<-- (move), <--> (swap). plus address-once (a side-effecting index is
evaluated exactly once) and negatives for the rejected leaf cases.
*/

Simple(int x_, int y_) {
}

/* a side effect (bumps its referent) used to prove a complex-lvalue index is
   evaluated exactly once. */
int tick(int^ c) {
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
        __println("arr = [" + arr[0] + "," + arr[1] + "]");
    }

    {
        (int, int) tuple;
        tuple[0] = 3;
        tuple[0] += 8;
        tuple[1] <-- tuple[0];
        tuple[0] = 4;
        tuple[0] <--> tuple[1];
        __println("tuple = (" + tuple[0] + "," + tuple[1] + ")");
    }

    {
        Simple simple;
        simple.x_ = 5;
        simple.x_ += 9;
        simple.y_ <-- simple.x_;
        simple.x_ = 6;
        simple.x_ <--> simple.y_;
        __println("simple. x = " + simple.x_ + " y = " + simple.y_);
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
        __println("p^ = " + p^ + " q^ = " + q^);
    }

    /* address-once: a side-effecting index runs a single time. */
    {
        int calls = 0;
        int arr[3];
        arr[0] = 10;
        arr[1] = 20;
        arr[2] = 30;
        arr[tick(^calls)] += 100;
        __println("addr-once: calls=" + calls + " arr1=" + arr[1]);
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
        __println("incdec: a0=" + arr[0] + " a1=" + arr[1] + " t0=" + t[0]
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
        __println("ops: a0=" + a[0]);
    }

    /* logical augmented-assign on a bool array element. */
    {
        bool b[1];
        b[0] = true;
        b[0] &&= false;
        b[0] ||= true;
        b[0] ^^= true;
        __println("logic: b0=" + b[0]);
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
        __println("multidim: g00=" + g[0][0] + " g11=" + g[1][1]);
    }

    /* composed chain: deref then field. */
    {
        Simple s;
        s.x_ = 7;
        Simple^ ps = ^s;
        ps^.x_ += 3;
        __println("derefield: sx=" + s.x_);
    }

    /* composed chain: an iterator element. */
    {
        int data[3];
        data[0] = 1;
        data[1] = 2;
        data[2] = 3;
        int[] it = ^data[0];
        it[1] += 20;
        __println("iter: data1=" + data[1]);
    }

    /* non-int leaves: a narrow integer flexes, a float computes. */
    {
        int8 a[1];
        a[0] = 10;
        a[0] += 5;
        float f[1];
        f[0] = 1.5;
        f[0] += 2.0;
        __println("widths: a0=" + a[0] + " f0=" + f[0]);
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
        __println("tick c=" + c + " sa1=" + sa[1]);                // tick c=2 sa1=9
    }

    /* augmented assign on a reference leaf is rejected. */
    //-EXPECT-ERROR: Arithmetic is not allowed on a reference
    //{
    //    int x; int y;
    //    x = 1; y = 2;
    //    (int^, int^) tr;
    //    tr[0] = ^x; tr[1] = ^y;
    //    tr[0] += 1;
    //    __println("" + tr[0]^);
    //}

    /* augmented assign on a non-primitive (sub-array) leaf is rejected. */
    //-EXPECT-ERROR: No common type for 'int[2]' and 'int'
    //{
    //    int g[2][2];
    //    g[0][0] = 1; g[0][1] = 2; g[1][0] = 3; g[1][1] = 4;
    //    g[0] += 1;
    //    __println("" + g[0][0]);
    //}

    /* bitwise augmented assign on a float leaf is rejected. */
    //-EXPECT-ERROR: Bitwise '&' not defined on floating-point
    //{
    //    float f[1];
    //    f[0] = 1.0;
    //    f[0] &= 2.0;
    //    __println("" + f[0]);
    //}

    /* a narrowing augmented assign through a complex lvalue is rejected. */
    //-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
    //{
    //    int wide; wide = 300;
    //    int8 a[1];
    //    a[0] = 1;
    //    a[0] += wide;
    //    __println("" + a[0]);
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
    //    __println("" + a[0]);
    //}

    //-EXPECT-ERROR: Cannot move a value onto itself
    //{
    //    int a[2]; int i = 0; a[0] = 1; a[1] = 2;
    //    a[i] <-- a[i];
    //    __println("" + a[i]);
    //}

    /* `a[i++] <--> a[i++]` is the SAME element under PPID (lowers to `a[i] <-->
       a[i]; i++; i++`) and should be rejected too, but the bump isn't lifted at
       classify so isSameLvalue doesn't see it yet — deferred (todo). */
    //-EXPECT-ERROR-DEFERRED: a[i++] self-swap needs the PPID-lifted-bump view in isSameLvalue
    //{
    //    int a[2]; int i = 0; a[0] = 1; a[1] = 2;
    //    a[i++] <--> a[i++];
    //    __println("" + a[0]);
    //}

    return 0;
}
