/*
test that logical operations &&, ||, ^^, ! and their compound forms
&&=, ||=, ^^= work across all built-in operand types.

truthy semantics: 0 / 0.0 / nullptr are false; everything else true.
result of &&, ||, ^^, ! is always bool (i32 0 or 1).
compound forms widen the bool back to the lvalue's type:
  integer  → zext   (0 or 1)
  float    → uitofp (0.0 or 1.0)
  pointer  → rejected (logical compound assign produces bool, not a pointer)
*/

int32 main() {

    /* === logical && — short-circuit === */
    bool b1 = true && false;
    int  i1 = 3 && 0;
    int  i2 = 3 && 5;
    float64 f1 = 3.14;
    float64 f2 = 0.0;
    bool fb1 = f1 && f2;
    bool fb2 = f1 && f1;
    int x = 7;
    int^ p = ^x;
    int^ q = nullptr;
    bool pb1 = p && q;
    bool pb2 = p && p;
    __println("&&: " + b1 + " " + i1 + " " + i2 + " " + fb1 + " " + fb2 + " " + pb1 + " " + pb2);

    /* === logical || — short-circuit === */
    bool b3 = false || true;
    int  i3 = 0 || 0;
    int  i4 = 0 || 5;
    bool fb3 = f2 || f2;
    bool fb4 = f1 || f2;
    bool pb3 = q || q;
    bool pb4 = q || p;
    __println("||: " + b3 + " " + i3 + " " + i4 + " " + fb3 + " " + fb4 + " " + pb3 + " " + pb4);

    /* === logical ^^ — always evaluates both === */
    bool b5 = true ^^ false;
    bool b6 = true ^^ true;
    int  i5 = 3 ^^ 0;
    int  i6 = 3 ^^ 5;
    bool fb5 = f1 ^^ f2;
    bool fb6 = f1 ^^ f1;
    bool pb5 = p ^^ q;
    bool pb6 = p ^^ p;
    __println("^^: " + b5 + " " + b6 + " " + i5 + " " + i6 + " " + fb5 + " " + fb6 + " " + pb5 + " " + pb6);

    /* === logical ! === */
    bool n1 = !true;
    bool n2 = !false;
    bool n3 = !0;
    bool n4 = !42;
    bool n5 = !f1;
    bool n6 = !f2;
    bool n7 = !p;
    bool n8 = !q;
    __println("!: " + n1 + " " + n2 + " " + n3 + " " + n4 + " " + n5 + " " + n6 + " " + n7 + " " + n8);

    /* === compound assigns — bool semantics, value extended to lvalue width === */
    int ca = 3;
    ca &&= 2;
    int cb = 3;
    cb ||= 0;
    int cc = 3;
    cc ^^= 2;
    __println("int compound: " + ca + " " + cb + " " + cc);

    bool ba = true;
    ba ^^= true;
    bool bb = false;
    bb ||= true;
    __println("bool compound: " + ba + " " + bb);

    float64 fa = 3.14;
    fa &&= 1.0;
    float64 fb = 3.14;
    fb ^^= 1.0;
    __println("float compound: " + fa + " " + fb);

    /* === if-condition truthy on each type === */
    if (i2)    { __println("if(i2) yes"); }    else { __println("if(i2) no"); }
    if (f1)    { __println("if(f1) yes"); }    else { __println("if(f1) no"); }
    if (f2)    { __println("if(f2) yes"); }    else { __println("if(f2) no"); }
    if (p)     { __println("if(p)  yes"); }    else { __println("if(p)  no"); }
    if (q)     { __println("if(q)  yes"); }    else { __println("if(q)  no"); }

    /* negative: logical compound assign on a pointer (iter) lvalue is rejected */
    int arr[3] = (1, 2, 3);
    int[] iter = ^arr[0];
    //-EXPECT-ERROR: logical compound assign produces bool, cannot assign to pointer
    //iter ^^= 1;

    return 0;
}
