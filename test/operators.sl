/*
test operator overloading

Assignment / move / swap
    = — copy assign (synthesized by default if not defined)
    <- — move (synthesized by default if not defined)
    <-> — swap (synthesized by default if not defined; signature must be SameType^)

  Arithmetic
    +, -, *, /, %

  Bitwise
    &, |, ^, <<, >>

  Logical
    &&, ||, ^^

  Compound assignment
    +=, -=, *=, /=, %=, &=, |=, ^=, <<=, >>=, &&=, ||=, ^^=

  Comparison
    ==, !=, <, >, <=, >=

  Indexing
    [] — read
    []= — write
*/

Simple(int x_ = 0) {
}

/*
overload all operators.
*/
Overload(int y_ = 0) {
    /* correct syntax. */
    op=(Overload^ a) {
        __println("Overload:op=(Overload^)");
    }
    op<-(Overload^ a) {
        __println("Overload:op<-(Overload^)");
    }
    op<->(Overload^ a) {
        __println("Overload:op<->(Overload^)");
    }
    op+(Overload^ a, Overload^ b) {
        __println("Overload:op+(Overload^,Overload^)");
    }
    op-(Overload^ a, Overload^ b) {
        __println("Overload:op-(Overload^,Overload^)");
    }
    op*(Overload^ a, Overload^ b) {
        __println("Overload:op*(Overload^,Overload^)");
    }
    op/(Overload^ a, Overload^ b) {
        __println("Overload:op/(Overload^,Overload^)");
    }
    op%(Overload^ a, Overload^ b) {
        __println("Overload:op%(Overload^,Overload^)");
    }
    op&(Overload^ a, Overload^ b) {
        __println("Overload:op&(Overload^,Overload^)");
    }
    op|(Overload^ a, Overload^ b) {
        __println("Overload:op|(Overload^,Overload^)");
    }
    op^(Overload^ a, Overload^ b) {
        __println("Overload:op^(Overload^,Overload^)");
    }
    op<<(Overload^ a, Overload^ b) {
        __println("Overload:op<<(Overload^,Overload^)");
    }
    op>>(Overload^ a, Overload^ b) {
        __println("Overload:op>>(Overload^,Overload^)");
    }
    op&&(Overload^ a, Overload^ b) {
        __println("Overload:op&&(Overload^,Overload^)");
    }
    op||(Overload^ a, Overload^ b) {
        __println("Overload:op||(Overload^,Overload^)");
    }
    op^^(Overload^ a, Overload^ b) {
        __println("Overload:op^^(Overload^,Overload^)");
    }
    op+=(Overload^ a) {
        __println("Overload:op+=(Overload^)");
    }
    op-=(Overload^ a) {
        __println("Overload:op-=(Overload^)");
    }
    op*=(Overload^ a) {
        __println("Overload:op*=(Overload^)");
    }
    op/=(Overload^ a) {
        __println("Overload:op/=(Overload^)");
    }
    op%=(Overload^ a) {
        __println("Overload:op%=(Overload^)");
    }
    op&=(Overload^ a) {
        __println("Overload:op&=(Overload^)");
    }
    op|=(Overload^ a) {
        __println("Overload:op|=(Overload^)");
    }
    op^=(Overload^ a) {
        __println("Overload:op^=(Overload^)");
    }
    op<<=(Overload^ a) {
        __println("Overload:op<<=(Overload^)");
    }
    op>>=(Overload^ a) {
        __println("Overload:op>>=(Overload^)");
    }
    op&&=(Overload^ a) {
        __println("Overload:op&&=(Overload^)");
    }
    op||=(Overload^ a) {
        __println("Overload:op||=(Overload^)");
    }
    op^^=(Overload^ a) {
        __println("Overload:op^^=(Overload^)");
    }
    bool op==(Overload^ a) {
        __println("Overload:op==(Overload^)");
        return false;
    }
    bool op!=(Overload^ a) {
        __println("Overload:op!=(Overload^)");
        return false;
    }
    bool op<(Overload^ a) {
        __println("Overload:op<(Overload^)");
        return false;
    }
    bool op>(Overload^ a) {
        __println("Overload:op>(Overload^)");
        return false;
    }
    bool op<=(Overload^ a) {
        __println("Overload:op<=(Overload^)");
        return false;
    }
    bool op>=(Overload^ a) {
        __println("Overload:op>=(Overload^)");
        return false;
    }
    int op[](Overload^ a) {
        __println("Overload:op[](Overload^)");
        return 0;
    }
    op[]=(Overload^ a, int b) {
        __println("Overload:op[]=(Overload^,int)");
    }

    /* correct syntax. */
    op=(int a) { }
    op<-(int a) { }
    /*op<->(int a) { }*/
    op+(int a, int b) { }
    op-(int a, int b) { }
    op*(int a, int b) { }
    op/(int a, int b) { }
    op%(int a, int b) { }
    op&(int a, int b) { }
    op|(int a, int b) { }
    op^(int a, int b) { }
    op<<(int a, int b) { }
    op>>(int a, int b) { }
    op&&(int a, int b) { }
    op||(int a, int b) { }
    op^^(int a, int b) { }
    op+=(int a) { }
    op-=(int a) { }
    op*=(int a) { }
    op/=(int a) { }
    op%=(int a) { }
    op&=(int a) { }
    op|=(int a) { }
    op^=(int a) { }
    op<<=(int a) { }
    op>>=(int a) { }
    op&&=(int a) { }
    op||=(int a) { }
    op^^=(int a) { }
    bool op==(int a) { return false; }
    bool op!=(int a) { return false; }
    bool op<(int a) { return false; }
    bool op>(int a) { return false; }
    bool op<=(int a) { return false; }
    bool op>=(int a) { return false; }
    int op[](int a) { return 0; }
    op[]=(int a, int b) { }

    /* correct syntax. */
    op=(Simple^ a) { }
    op<-(Simple^ a) { }
    /*op<->(Simple^ a) { }*/
    op+(Simple^ a, Simple^ b) { }
    op-(Simple^ a, Simple^ b) { }
    op*(Simple^ a, Simple^ b) { }
    op/(Simple^ a, Simple^ b) { }
    op%(Simple^ a, Simple^ b) { }
    op&(Simple^ a, Simple^ b) { }
    op|(Simple^ a, Simple^ b) { }
    op^(Simple^ a, Simple^ b) { }
    op<<(Simple^ a, Simple^ b) { }
    op>>(Simple^ a, Simple^ b) { }
    op&&(Simple^ a, Simple^ b) { }
    op||(Simple^ a, Simple^ b) { }
    op^^(Simple^ a, Simple^ b) { }
    op+=(Simple^ a) { }
    op-=(Simple^ a) { }
    op*=(Simple^ a) { }
    op/=(Simple^ a) { }
    op%=(Simple^ a) { }
    op&=(Simple^ a) { }
    op|=(Simple^ a) { }
    op^=(Simple^ a) { }
    op<<=(Simple^ a) { }
    op>>=(Simple^ a) { }
    op&&=(Simple^ a) { }
    op||=(Simple^ a) { }
    op^^=(Simple^ a) { }
    bool op==(Simple^ a) { return false; }
    bool op!=(Simple^ a) { return false; }
    bool op<(Simple^ a) { return false; }
    bool op>(Simple^ a) { return false; }
    bool op<=(Simple^ a) { return false; }
    bool op>=(Simple^ a) { return false; }
    Simple op[](Simple^ a) { return Simple(); }
    op[]=(Simple^ a, Simple^ b) { }

    /* compile error: move special case */
    // op<->(int a) { }
    // op<->(Simple^ a) { }

    /* compile error: not enough parameters. */
    // op=() { }
    // op<-() { }
    // op<->() { }
    // op+(Overload^ a) { }
    // op-(Overload^ a) { }
    // op*(Overload^ a) { }
    // op/(Overload^ a) { }
    // op%(Overload^ a) { }
    // op&(Overload^ a) { }
    // op|(Overload^ a) { }
    // op^(Overload^ a) { }
    // op<<(Overload^ a) { }
    // op>>(Overload^ a) { }
    // op&&(Overload^ a) { }
    // op||(Overload^ a) { }
    // op^^(Overload^ a) { }
    // op+=() { }
    // op-=() { }
    // op*=() { }
    // op/=() { }
    // op%=() { }
    // op&=() { }
    // op|=() { }
    // op^=() { }
    // op<<=() { }
    // op>>=() { }
    // op&&=() { }
    // op||=() { }
    // op^^=() { }
    // bool op==() { }
    // bool op!=() { }
    // bool op<() { }
    // bool op>() { }
    // bool op<=() { }
    // bool op>=() { }
    // Overload op[]() { }
    // op[]=(Overload^ a) { }

    /* compile error: too many parameters. */
    // op=(Overload^ a, Overload^ b) { }
    // op<-(Overload^ a, Overload^ b) { }
    // op<->(Overload^ a, Overload^ b) { }
    // op+(Overload^ a, Overload^ b, Overload^ c) { }
    // op-(Overload^ a, Overload^ b, Overload^ c) { }
    // op*(Overload^ a, Overload^ b, Overload^ c) { }
    // op/(Overload^ a, Overload^ b, Overload^ c) { }
    // op%(Overload^ a, Overload^ b, Overload^ c) { }
    // op&(Overload^ a, Overload^ b, Overload^ c) { }
    // op|(Overload^ a, Overload^ b, Overload^ c) { }
    // op^(Overload^ a, Overload^ b, Overload^ c) { }
    // op<<(Overload^ a, Overload^ b, Overload^ c) { }
    // op>>(Overload^ a, Overload^ b, Overload^ c) { }
    // op&&(Overload^ a, Overload^ b, Overload^ c) { }
    // op||(Overload^ a, Overload^ b, Overload^ c) { }
    // op^^(Overload^ a, Overload^ b, Overload^ c) { }
    // op+=(Overload^ a, Overload^ b) { }
    // op-=(Overload^ a, Overload^ b) { }
    // op*=(Overload^ a, Overload^ b) { }
    // op/=(Overload^ a, Overload^ b) { }
    // op%=(Overload^ a, Overload^ b) { }
    // op&=(Overload^ a, Overload^ b) { }
    // op|=(Overload^ a, Overload^ b) { }
    // op^=(Overload^ a, Overload^ b) { }
    // op<<=(Overload^ a, Overload^ b) { }
    // op>>=(Overload^ a, Overload^ b) { }
    // op&&=(Overload^ a, Overload^ b) { }
    // op||=(Overload^ a, Overload^ b) { }
    // op^^=(Overload^ a, Overload^ b) { }
    // bool op==(Overload^ a, Overload^ b) { }
    // bool op!=(Overload^ a, Overload^ b) { }
    // bool op<(Overload^ a, Overload^ b) { }
    // bool op>(Overload^ a, Overload^ b) { }
    // bool op<=(Overload^ a, Overload^ b) { }
    // bool op>=(Overload^ a, Overload^ b) { }
    // Overload op[](Overload^ a, Overload^ b) { }
    // op[]=(Overload^ a, Overload^ b, Overload^ c) { }
}

Comparison(int z_ = 0) {
    /* correct syntax: may return not-bool */
    int op==(Simple^ a) { return 0; }
    int op!=(Simple^ a) { return 0; }
    int op<(Simple^ a) { return 0; }
    int op>(Simple^ a) { return 0; }
    int op<=(Simple^ a) { return 0; }
    int op>=(Simple^ a) { return 0; }
}

int32 main()
{
    Overload a;
    Overload b;
    Overload c;
    bool result = false;
    int index = 0;
    int value = 0;

    /* correct syntax. */
    a = b;
    a <- b;
    a <-> b;
    a = b + c;
    a = b - c;
    a = b * c;
    a = b / c;
    a = b % c;
    a = b & c;
    a = b | c;
    a = b ^ c;
    a = b << c;
    a = b >> c;
    a = b && c;
    a = b || c;
    a = b ^^ c;
    a += b;
    a -= b;
    a *= b;
    a /= b;
    a %= b;
    a &= b;
    a |= b;
    a ^= b;
    a <<= b;
    a >>= b;
    a &&= b;
    a ||= b;
    a ^^= b;
    result = (a == b);
    result = (a != b);
    result = (a < b);
    result = (a > b);
    result = (a <= b);
    result = (a >= b);
    value = a[index];
    a[index] = value;

    /* correct syntax. */
    Comparison a1;
    Comparison b1;
    int result1 = false;
    result1 = (a1 == b1);
    result1 = (a1 != b1);
    result1 = (a1 < b1);
    result1 = (a1 > b1);
    result1 = (a1 <= b1);
    result1 = (a1 >= b1);

    return 0;
}
