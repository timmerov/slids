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

Unary
    +, -, ~, !

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

Unary on a slid operand has two forms — fresh-temp or
self-only. Arity 0 mirrors comparison: self only, no rhs,
returned type must be a built-in. Arity 1 produces self
from the operand:

    Class temp = - Type operand
        -> temp.op-(operand)         (arity 1, returns self)

    if (- Class operand) { }
        -> operand.op-()             (arity 0, returns built-in)

Of the unary operators, + and - also accept a binary form
(covered above); ~ and ! do not.

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
    op<-(mutable Overload^ a) {
        __println("Overload:op<-(Overload^)");
    }
    op<->(mutable Overload^ a) {
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
    /* unary arity 0: self only, no operand. Built-in return like comparison. */
    bool op+() {
        __println("Overload:op+()");
        return false;
    }
    bool op-() {
        __println("Overload:op-()");
        return false;
    }
    bool op~() {
        __println("Overload:op~()");
        return false;
    }
    bool op!() {
        __println("Overload:op!()");
        return false;
    }
    /* unary arity 1: returns self. */
    op+(Overload^ a) {
        __println("Overload:op+(Overload^)");
    }
    op-(Overload^ a) {
        __println("Overload:op-(Overload^)");
    }
    op~(Overload^ a) {
        __println("Overload:op~(Overload^)");
    }
    op!(Overload^ a) {
        __println("Overload:op!(Overload^)");
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
    /* unary arity 1: int operand. */
    op+(int a) {
        __println("Overload:op+(int)");
    }
    op-(int a) {
        __println("Overload:op-(int)");
    }
    op~(int a) {
        __println("Overload:op~(int)");
    }
    op!(int a) {
        __println("Overload:op!(int)");
    }

    /* correct syntax. */
    op=(Simple^ a) {
        __println("Overload:op=(Simple^)");
    }
    op<-(mutable Simple^ a) {
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
    /* unary arity 1: Simple^ operand. */
    op+(Simple^ a) {
        __println("Overload:op+(Simple^)");
    }
    op-(Simple^ a) {
        __println("Overload:op-(Simple^)");
    }
    op~(Simple^ a) {
        __println("Overload:op~(Simple^)");
    }
    op!(Simple^ a) {
        __println("Overload:op!(Simple^)");
    }

    /* compile error: swap special case */
    // op<->(int a) { }
    // op<->(Simple^ a) { }

    /* compile error: not enough parameters. */
    // op=() { }
    // op<-() { }
    // op<->() { }
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

    /* compile error: too many parameters; ~ and ! have no binary form. */
    // op~(Overload^ a, Overload^ b) { }
    // op!(Overload^ a, Overload^ b) { }
}

/* All cases here violate "must return built-in type". */
BadReturn(int dummy_ = 0) {
    /* unary arity 0 — must return built-in (bool/int/pointer). */
    // Overload op+() { return Overload(); }
    // Overload op-() { return Overload(); }
    // Overload op~() { return Overload(); }
    // Overload op!() { return Overload(); }
    /* comparison — must return built-in. */
    // Overload op==(Simple^ a) { return Overload(); }
    // Overload op!=(Simple^ a) { return Overload(); }
    // Overload op<(Simple^ a) { return Overload(); }
    // Overload op>(Simple^ a) { return Overload(); }
    // Overload op<=(Simple^ a) { return Overload(); }
    // Overload op>=(Simple^ a) { return Overload(); }
}

/* All cases here violate 'mutable' rules. Isolated so each
   negative line stands alone when uncommented. */
BadMutable(int dummy_ = 0) {
    /* move/swap with pointer param require 'mutable'. */
    // op<-(Overload^ a) { }
    // op<->(Overload^ a) { }
    /* 'mutable' applies only to pointer types '^' and '[]'. */
    // op<-(mutable int a) { }
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
    op<-(mutable void^ ptr) {
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
    /* unary arity 1: a = -b lowers to a.op-(b). */
    a = -b;
    a = +b;
    a = ~b;
    a = !b;
    /* unary arity 0: -a (no slid LHS) lowers to a.op-() — built-in result. */
    result = -a;
    result = +a;
    result = ~a;
    result = !a;
    if (-a) { }

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
    /* unary arity 1: int operand. */
    a = -x;
    a = +x;
    a = ~x;
    a = !x;

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
    /* unary arity 1: Simple^ operand. */
    a = -d;
    a = +d;
    a = ~d;
    a = !d;

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

    /* compile-error catalog (verified one-by-one). */
    __println("1: Not allowed: op<->(int a)");
    __println("2: Not allowed: op<->(Simple^ a)");
    __println("3: Not allowed: op=()");
    __println("4: Not allowed: op<-()");
    __println("5: Not allowed: op<->()");
    __println("6: Not allowed: op*(Overload^ a)");
    __println("7: Not allowed: op/(Overload^ a)");
    __println("8: Not allowed: op%(Overload^ a)");
    __println("9: Not allowed: op&(Overload^ a)");
    __println("10: Not allowed: op|(Overload^ a)");
    __println("11: Not allowed: op^(Overload^ a)");
    __println("12: Not allowed: op<<(Overload^ a)");
    __println("13: Not allowed: op>>(Overload^ a)");
    __println("14: Not allowed: op&&(Overload^ a)");
    __println("15: Not allowed: op||(Overload^ a)");
    __println("16: Not allowed: op^^(Overload^ a)");
    __println("17: Not allowed: op+=()");
    __println("18: Not allowed: op-=()");
    __println("19: Not allowed: op*=()");
    __println("20: Not allowed: op/=()");
    __println("21: Not allowed: op%=()");
    __println("22: Not allowed: op&=()");
    __println("23: Not allowed: op|=()");
    __println("24: Not allowed: op^=()");
    __println("25: Not allowed: op<<=()");
    __println("26: Not allowed: op>>=()");
    __println("27: Not allowed: op&&=()");
    __println("28: Not allowed: op||=()");
    __println("29: Not allowed: op^^=()");
    __println("30: Not allowed: bool op==()");
    __println("31: Not allowed: bool op!=()");
    __println("32: Not allowed: bool op<()");
    __println("33: Not allowed: bool op>()");
    __println("34: Not allowed: bool op<=()");
    __println("35: Not allowed: bool op>=()");
    __println("36: Not allowed: Overload op[]()");
    __println("37: Not allowed: op[]=(Overload^ a)");
    __println("38: Not allowed: op=(Overload^ a, Overload^ b)");
    __println("39: Not allowed: op<-(Overload^ a, Overload^ b)");
    __println("40: Not allowed: op<->(Overload^ a, Overload^ b)");
    __println("41: Not allowed: op+(Overload^ a, Overload^ b, Overload^ c)");
    __println("42: Not allowed: op-(Overload^ a, Overload^ b, Overload^ c)");
    __println("43: Not allowed: op*(Overload^ a, Overload^ b, Overload^ c)");
    __println("44: Not allowed: op/(Overload^ a, Overload^ b, Overload^ c)");
    __println("45: Not allowed: op%(Overload^ a, Overload^ b, Overload^ c)");
    __println("46: Not allowed: op&(Overload^ a, Overload^ b, Overload^ c)");
    __println("47: Not allowed: op|(Overload^ a, Overload^ b, Overload^ c)");
    __println("48: Not allowed: op^(Overload^ a, Overload^ b, Overload^ c)");
    __println("49: Not allowed: op<<(Overload^ a, Overload^ b, Overload^ c)");
    __println("50: Not allowed: op>>(Overload^ a, Overload^ b, Overload^ c)");
    __println("51: Not allowed: op&&(Overload^ a, Overload^ b, Overload^ c)");
    __println("52: Not allowed: op||(Overload^ a, Overload^ b, Overload^ c)");
    __println("53: Not allowed: op^^(Overload^ a, Overload^ b, Overload^ c)");
    __println("54: Not allowed: op+=(Overload^ a, Overload^ b)");
    __println("55: Not allowed: op-=(Overload^ a, Overload^ b)");
    __println("56: Not allowed: op*=(Overload^ a, Overload^ b)");
    __println("57: Not allowed: op/=(Overload^ a, Overload^ b)");
    __println("58: Not allowed: op%=(Overload^ a, Overload^ b)");
    __println("59: Not allowed: op&=(Overload^ a, Overload^ b)");
    __println("60: Not allowed: op|=(Overload^ a, Overload^ b)");
    __println("61: Not allowed: op^=(Overload^ a, Overload^ b)");
    __println("62: Not allowed: op<<=(Overload^ a, Overload^ b)");
    __println("63: Not allowed: op>>=(Overload^ a, Overload^ b)");
    __println("64: Not allowed: op&&=(Overload^ a, Overload^ b)");
    __println("65: Not allowed: op||=(Overload^ a, Overload^ b)");
    __println("66: Not allowed: op^^=(Overload^ a, Overload^ b)");
    __println("67: Not allowed: bool op==(Overload^ a, Overload^ b)");
    __println("68: Not allowed: bool op!=(Overload^ a, Overload^ b)");
    __println("69: Not allowed: bool op<(Overload^ a, Overload^ b)");
    __println("70: Not allowed: bool op>(Overload^ a, Overload^ b)");
    __println("71: Not allowed: bool op<=(Overload^ a, Overload^ b)");
    __println("72: Not allowed: bool op>=(Overload^ a, Overload^ b)");
    __println("73: Not allowed: Overload op[](Overload^ a, Overload^ b)");
    __println("74: Not allowed: op[]=(Overload^ a, Overload^ b, Overload^ c)");
    __println("75: Not allowed: op~(Overload^ a, Overload^ b)");
    __println("76: Not allowed: op!(Overload^ a, Overload^ b)");
    __println("77: Not allowed: Overload op+() (arity-0 unary returns class)");
    __println("78: Not allowed: Overload op-() (arity-0 unary returns class)");
    __println("79: Not allowed: Overload op~() (arity-0 unary returns class)");
    __println("80: Not allowed: Overload op!() (arity-0 unary returns class)");
    __println("81: Not allowed: Overload op==(Simple^ a) (comparison returns class)");
    __println("82: Not allowed: Overload op!=(Simple^ a) (comparison returns class)");
    __println("83: Not allowed: Overload op<(Simple^ a) (comparison returns class)");
    __println("84: Not allowed: Overload op>(Simple^ a) (comparison returns class)");
    __println("85: Not allowed: Overload op<=(Simple^ a) (comparison returns class)");
    __println("86: Not allowed: Overload op>=(Simple^ a) (comparison returns class)");
    __println("87: Not allowed: op<-(Overload^ a) (move pointer param missing 'mutable')");
    __println("88: Not allowed: op<->(Overload^ a) (swap pointer param missing 'mutable')");
    __println("89: Not allowed: op<-(mutable int a) ('mutable' only on '^' or '[]')");

    return 0;
}
