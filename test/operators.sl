/*
Test operator overloading.

Catalog of operators:

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

Unary (not yet implemented)
    -, ~, !, (maybe +)

Usage rules:

The principle is: no naked operators. Every operator
must be attached to a class. For most operators, the
product is self.

Conventions for this section:
    Class is a defined slids class.
    Type is a Class or a built-in type.
    int is a placeholder for integer types.
    temp is a temporary variable used to evaluate
        an expression.

Assignment-like operations — declaration, assignment,
copy, and compound assignment — lower as follows:

    Class lhs = Type rhs
        -> lhs.op=(rhs)

Move requires rhs to be an lvalue:

    Class lhs <- Type rhs
        -> lhs.op<-(rhs)

Moving from a pointer type also sets the rhs to nullptr:

    Class lhs <- Type^ rhs
        -> lhs.op<-(rhs); rhs = nullptr

Swap requires lhs and rhs to be the same type and both
lvalues:

    Class lhs <-> SameClass rhs
        -> lhs.op<->(rhs)

Binary operations on temps are fused in place when
possible — covers everything compoundable (arithmetic,
bitwise, logical):

    Class temp + Type rhs
        -> temp.op+=(rhs)

Otherwise binary operations produce a fresh temp:

    Class temp = Class lhs + Type rhs
        -> temp.op+(lhs, rhs)

Some operators don't produce self — they return a value
instead. The returned type must be a built-in type;
otherwise the operator would fall under the binary-op
rules:

    int x = (Class lhs == Type rhs)
        -> x = lhs.op==(rhs)

Indexing is a special case. Read may return any type:

    Class lhs = Class rhs[Type index]
        -> lhs = rhs.op[](index)

Write returns an lvalue:

    Class lhs[Type index] = Class rhs
        -> lhs.op[]=(index, rhs)

When no overload matches exactly, types are converted
by calling the target type's op=. Integer types may be
widened to match. Smallest widening wins.
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
    op=(int a) {
        __println("Overload:op=(int)");
    }
    op<-(int a) {
        __println("Overload:op<-(int)");
    }
    /*op<->(int a) { }*/
    op+(int a, int b) {
        __println("Overload:op+(int,int)");
    }
    op-(int a, int b) {
        __println("Overload:op-(int,int)");
    }
    op*(int a, int b) {
        __println("Overload:op*(int,int)");
    }
    op/(int a, int b) {
        __println("Overload:op/(int,int)");
    }
    op%(int a, int b) {
        __println("Overload:op%(int,int)");
    }
    op&(int a, int b) {
        __println("Overload:op&(int,int)");
    }
    op|(int a, int b) {
        __println("Overload:op|(int,int)");
    }
    op^(int a, int b) {
        __println("Overload:op^(int,int)");
    }
    op<<(int a, int b) {
        __println("Overload:op<<(int,int)");
    }
    op>>(int a, int b) {
        __println("Overload:op>>(int,int)");
    }
    op&&(int a, int b) {
        __println("Overload:op&&(int,int)");
    }
    op||(int a, int b) {
        __println("Overload:op||(int,int)");
    }
    op^^(int a, int b) {
        __println("Overload:op^^(int,int)");
    }
    op+=(int a) {
        __println("Overload:op+=(int)");
    }
    op-=(int a) {
        __println("Overload:op-=(int)");
    }
    op*=(int a) {
        __println("Overload:op*=(int)");
    }
    op/=(int a) {
        __println("Overload:op/=(int)");
    }
    op%=(int a) {
        __println("Overload:op%=(int)");
    }
    op&=(int a) {
        __println("Overload:op&=(int)");
    }
    op|=(int a) {
        __println("Overload:op|=(int)");
    }
    op^=(int a) {
        __println("Overload:op^=(int)");
    }
    op<<=(int a) {
        __println("Overload:op<<=(int)");
    }
    op>>=(int a) {
        __println("Overload:op>>=(int)");
    }
    op&&=(int a) {
        __println("Overload:op&&=(int)");
    }
    op||=(int a) {
        __println("Overload:op||=(int)");
    }
    op^^=(int a) {
        __println("Overload:op^^=(int)");
    }
    bool op==(int a) {
        __println("Overload:op==(int)");
        return false;
    }
    bool op!=(int a) {
        __println("Overload:op!=(int)");
        return false;
    }
    bool op<(int a) {
        __println("Overload:op<(int)");
        return false;
    }
    bool op>(int a) {
        __println("Overload:op>(int)");
        return false;
    }
    bool op<=(int a) {
        __println("Overload:op<=(int)");
        return false;
    }
    bool op>=(int a) {
        __println("Overload:op>=(int)");
        return false;
    }
    int op[](int a) {
        __println("Overload:op[](int)");
        return 0;
    }
    op[]=(int a, int b) {
        __println("Overload:op[]=(int,int)");
    }

    /* correct syntax. */
    op=(Simple^ a) {
        __println("Overload:op=(Simple^)");
    }
    op<-(Simple^ a) {
        __println("Overload:op<-(Simple^)");
    }
    /*op<->(Simple^ a) { }*/
    op+(Simple^ a, Simple^ b) {
        __println("Overload:op+(Simple^,Simple^)");
    }
    op-(Simple^ a, Simple^ b) {
        __println("Overload:op-(Simple^,Simple^)");
    }
    op*(Simple^ a, Simple^ b) {
        __println("Overload:op*(Simple^,Simple^)");
    }
    op/(Simple^ a, Simple^ b) {
        __println("Overload:op/(Simple^,Simple^)");
    }
    op%(Simple^ a, Simple^ b) {
        __println("Overload:op%(Simple^,Simple^)");
    }
    op&(Simple^ a, Simple^ b) {
        __println("Overload:op&(Simple^,Simple^)");
    }
    op|(Simple^ a, Simple^ b) {
        __println("Overload:op|(Simple^,Simple^)");
    }
    op^(Simple^ a, Simple^ b) {
        __println("Overload:op^(Simple^,Simple^)");
    }
    op<<(Simple^ a, Simple^ b) {
        __println("Overload:op<<(Simple^,Simple^)");
    }
    op>>(Simple^ a, Simple^ b) {
        __println("Overload:op>>(Simple^,Simple^)");
    }
    op&&(Simple^ a, Simple^ b) {
        __println("Overload:op&&(Simple^,Simple^)");
    }
    op||(Simple^ a, Simple^ b) {
        __println("Overload:op||(Simple^,Simple^)");
    }
    op^^(Simple^ a, Simple^ b) {
        __println("Overload:op^^(Simple^,Simple^)");
    }
    op+=(Simple^ a) {
        __println("Overload:op+=(Simple^)");
    }
    op-=(Simple^ a) {
        __println("Overload:op-=(Simple^)");
    }
    op*=(Simple^ a) {
        __println("Overload:op*=(Simple^)");
    }
    op/=(Simple^ a) {
        __println("Overload:op/=(Simple^)");
    }
    op%=(Simple^ a) {
        __println("Overload:op%=(Simple^)");
    }
    op&=(Simple^ a) {
        __println("Overload:op&=(Simple^)");
    }
    op|=(Simple^ a) {
        __println("Overload:op|=(Simple^)");
    }
    op^=(Simple^ a) {
        __println("Overload:op^=(Simple^)");
    }
    op<<=(Simple^ a) {
        __println("Overload:op<<=(Simple^)");
    }
    op>>=(Simple^ a) {
        __println("Overload:op>>=(Simple^)");
    }
    op&&=(Simple^ a) {
        __println("Overload:op&&=(Simple^)");
    }
    op||=(Simple^ a) {
        __println("Overload:op||=(Simple^)");
    }
    op^^=(Simple^ a) {
        __println("Overload:op^^=(Simple^)");
    }
    bool op==(Simple^ a) {
        __println("Overload:op==(Simple^)");
        return false;
    }
    bool op!=(Simple^ a) {
        __println("Overload:op!=(Simple^)");
        return false;
    }
    bool op<(Simple^ a) {
        __println("Overload:op<(Simple^)");
        return false;
    }
    bool op>(Simple^ a) {
        __println("Overload:op>(Simple^)");
        return false;
    }
    bool op<=(Simple^ a) {
        __println("Overload:op<=(Simple^)");
        return false;
    }
    bool op>=(Simple^ a) {
        __println("Overload:op>=(Simple^)");
        return false;
    }
    Simple op[](Simple^ a) {
        __println("Overload:op[](Simple^)");
        return Simple();
    }
    op[]=(Simple^ a, Simple^ b) {
        __println("Overload:op[]=(Simple^,Simple^)");
    }

    /* compile error: swap special case */
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
    int op==(Simple^ a) {
        __println("Comparison:op==(Simple^)");
        return 0;
    }
    int op!=(Simple^ a) {
        __println("Comparison:op!=(Simple^)");
        return 0;
    }
    int op<(Simple^ a) {
        __println("Comparison:op<(Simple^)");
        return 0;
    }
    int op>(Simple^ a) {
        __println("Comparison:op>(Simple^)");
        return 0;
    }
    int op<=(Simple^ a) {
        __println("Comparison:op<=(Simple^)");
        return 0;
    }
    int op>=(Simple^ a) {
        __println("Comparison:op>=(Simple^)");
        return 0;
    }
}

MovePtr(int w_ = 0) {
    op<-(void^ ptr) {
        __println("MovePtr:op<-(void^)");
    }
}

int32 main()
{
    Overload a;
    Overload b;
    Overload c;
    Simple d;
    Simple e;
    bool result = false;
    int index = 0;
    int value = 0;
    int x = 0;

    /* correct syntax. */
    a = b;
    a <- b;
    a <-> b;
    Overload decl_copy_b = b;
    Overload decl_move_b <- b;
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
    if (a == b) { }
    value = a[b];
    a[b] = value;

    /* correct syntax. */
    a = x;
    a <- x;
    Overload decl_copy_x = x;
    Overload decl_move_x <- x;
    a = b + x;
    a = b - x;
    a = b * x;
    a = b / x;
    a = b % x;
    a = b & x;
    a = b | x;
    a = b ^ x;
    a = b << x;
    a = b >> x;
    a = b && x;
    a = b || x;
    a = b ^^ x;
    a += x;
    a -= x;
    a *= x;
    a /= x;
    a %= x;
    a &= x;
    a |= x;
    a ^= x;
    a <<= x;
    a >>= x;
    a &&= x;
    a ||= x;
    a ^^= x;
    result = (a == x);
    result = (a != x);
    result = (a < x);
    result = (a > x);
    result = (a <= x);
    result = (a >= x);
    if (a == x) { }
    value = a[x];
    a[x] = value;

    /* correct syntax. */
    a = d;
    a <- d;
    Overload decl_copy_d = d;
    Overload decl_move_d <- d;
    a = b + d;
    a = b - d;
    a = b * d;
    a = b / d;
    a = b % d;
    a = b & d;
    a = b | d;
    a = b ^ d;
    a = b << d;
    a = b >> d;
    a = b && d;
    a = b || d;
    a = b ^^ d;
    a += d;
    a -= d;
    a *= d;
    a /= d;
    a %= d;
    a &= d;
    a |= d;
    a ^= d;
    a <<= d;
    a >>= d;
    a &&= d;
    a ||= d;
    a ^^= d;
    result = (a == d);
    result = (a != d);
    result = (a < d);
    result = (a > d);
    result = (a <= d);
    result = (a >= d);
    if (a == d) { }
    e = a[d];
    a[d] = e;

    /* correct syntax. */
    Comparison a1;
    int result1 = false;
    result1 = (a1 == d);
    result1 = (a1 != d);
    result1 = (a1 < d);
    result1 = (a1 > d);
    result1 = (a1 <= d);
    result1 = (a1 >= d);

    /* clear moved pointer test. */
    MovePtr mover;
    void^ ptr = ^mover;
    mover <- ptr;
    result = (ptr == nullptr);
    __println("MoveTest[1]: (ptr==nullptr)=" + result);

    return 0;
}
