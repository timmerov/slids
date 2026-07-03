/*
test overload class operators.

Catalog of operators:

Assignment / move / swap
    = — copy assign (synthesized by default if not defined)
    <-- — move (synthesized by default if not defined)
    <--> — swap (synthesized by default if not defined; signature must be SameType^)

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

Dereference
    ^ — read/write through reference to contained object

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

    Class lhs <-- Type rhs
        -> lhs.op<--(rhs)

Moving from a pointer Type1 also sets the rhs to nullptr:

    Class lhs <-- Type^ rhs
        -> lhs.op<--(rhs); rhs = nullptr

Swap requires lhs and rhs to be the same Type1 and both
lvalues:

    Class lhs <--> SameClass rhs
        -> lhs.op<-->(rhs)

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

Indexing returns a reference. Both read and write desugar
through deref:

    Class lhs = Container rhs[Type index]
        -> lhs = rhs.op[](index)^

    Container lhs[Type index] = Class rhs
        -> lhs.op[](index)^ = rhs

Dereference returns a reference. Both read and write
desugar through deref:

    Class lhs = Iterator rhs^
        -> lhs = rhs.op^()^

    Iterator lhs^ = Class rhs
        -> lhs.op^()^ = rhs

When no overload matches exactly, types are converted
by calling the target type's op=. Integer types may be
widened to match. Smallest widening wins.

operators signatures are restricted.
most operators have no return type.
exceptions are noted.
the parameters for most operators are flexible - but
must be primitive or pointer to const.
a move pointer parameter must be explicit mutable.
the swap parameter must be explicit mutable.

accepted signature templates and simple usage:

    /* pseudo-code */
    Number x = integer | float
    Primitive b = integer | float | pointer
    ConstTypeN a,b = integer | float | pointer to const
    Type = any type
    Class c = the enclosing class type

    Class() {
        /* assignment */
        op=(ConstType a);                       obj = a;
        op<--(Number x);                        obj <-- x;
        op<--(mutable Type^ a);                 obj <-- a;
        op<-->(mutable Class^ c);               obj1 <-- obj2;

        /* binary operation */
        op+(ConstType1 a, ConstType2 b);        obj = a + b;
        op-(ConstType1 a, ConstType2 b);        obj = a - b;
        op*(ConstType1 a, ConstType2 b);        obj = a * b;
        op/(ConstType1 a, ConstType2 b);        obj = a / b;
        op%(ConstType1 a, ConstType2 b);        obj = a % b;
        op&(ConstType1 a, ConstType2 b);        obj = a & b;
        op|(ConstType1 a, ConstType2 b);        obj = a | b;
        op^(ConstType1 a, ConstType2 b);        obj = a ^ b;
        op<<(ConstType1 a, ConstType2 b);       obj = a << b;
        op>>(ConstType1 a, ConstType2 b);       obj = a >> b;
        op&&(ConstType1 a, ConstType2 b);       obj = a && b;
        op||(ConstType1 a, ConstType2 b);       obj = a || b;
        op^^(ConstType1 a, ConstType2 b);       obj = a ^^ b;

        /* augment assignment */
        op+=(ConstType a);                      obj += a;
        op-=(ConstType a);                      obj -= a;
        op*=(ConstType a);                      obj *= a;
        op/=(ConstType a);                      obj /= a;
        op%=(ConstType a);                      obj %= a;
        op&=(ConstType a);                      obj &= a;
        op|=(ConstType a);                      obj |= a;
        op^=(ConstType a);                      obj ^= a;
        op<<=(ConstType a);                     obj <<= a;
        op>>=(ConstType a);                     obj >>= a;
        op&&=(ConstType a);                     obj &&= a;
        op||=(ConstType a);                     obj ||= a;
        op^^=(ConstType a);                     obj ^^= a;

        /* comparison */
        Primitive op==(ConstType a);            b = (obj == a);
        Primitive op!=(ConstType a);            b = (obj != a);
        Primitive op<(ConstType a);             b = (obj < a);
        Primitive op>(ConstType a);             b = (obj > a);
        Primitive op<=(ConstType a);            b = (obj <= a);
        Primitive op>=(ConstType a);            b = (obj >= a);

        /* index, dereference */
        Type^ op[](ConstType a);                x = obj[a];
        Type^ op^();                            x = obj^;

        /* unary */
        Primitive op+();                        b = +obj;
        Primitive op-();                        b = -obj;
        Primitive op~();                        b = ~obj;
        Primitive op!();                        b = !obj;

        /* negation */
        op+(ConstType a);                       obj = +a;
        op-(ConstType a);                       obj = -a;
        op~(ConstType a);                       obj = ~a;
        op!(ConstType a);                       obj = !a;
    }

default move/copy operators are synthesized iff the class
does not explicitly define them.
move/copy by slot iteratively and recursively.
requires lhs and rhs to be the same type.
the rhs must be mutable for move.

    Class cls1 = cls2;

object instantiation sequence when the class does not define
a matching move/assign operator:

    allocate memory
    copy/move fields iteratively and recursively
    call ctor

object instantiation sequence when the class defines a matching
move/assign operator:

    allocate memory
    initialize fields
    call ctor
    call move/assign operator

these two declarations follow the exact same code path:

    Class cls(1,2,3);
    Class cls = (1,2,3);

*******************************************************
timmer you are here:
*******************************************************

when op= lands, we want to be able to support:

    op=( TupleType ) { ... }

in which case, the assignment form must check for an overloaded op=
where the parameter is a tuple that matches the expression type.
if no match, then fall back to initialization.

note to future self:

apparently we have move-init aka var-decl-move.
a's fields are move-copied to b.
then b's ctor is called.
huh.

    Class a(1,2,3);
    Class b <-- a;

that only works if Class uses the default move operator.
if Class overloads the move operator then we must construct first then move.
otherwise the move operator would be called before the object is initialized.
which would be bad.
*/

/*
claude says:

tbd
*/

DefaultMove(
    char c_,
    int^ p_,
    int[] q_
) {
    _() { __println("ctor " + c_); }
    ~() { __println("dtor " + c_); }
}

void print(DefaultMove^ dm) {
    __print(dm^.c_);
    if (dm^.p_ == nullptr) {
        __print(" nullptr");
    } else {
        __print(" " + dm^.p_^);
    }
    if (dm^.q_ == nullptr) {
        __println(" nullptr");
    } else {
        __println(" " + dm^.q_^);
    }
}

/* a class whose field is itself a class — move/copy must DESCEND into inner_
   (recursively applying DefaultMove's move/copy). */
Outer(
    DefaultMove inner_
) {
}

/* a class with TWO class fields — move/copy must descend into BOTH. */
Pair(
    DefaultMove a_,
    DefaultMove b_
) {
}

/* a pointer-free class — move is a pure copy (nothing to null, source intact). */
Plain(
    char c_,
    int n_
) {
    _() { __println("Plain ctor " + c_); }
    ~() { __println("Plain dtor " + c_); }
}

/* a class with an ARRAY of class — move/copy walks the elements (iterative AND
   recursive: class -> array -> element class -> pointer leaf). */
Holder(
    DefaultMove items_[2]
) {
}

int32 main() {

    int a = 42;
    int b = 7;
    int d[2] = (98, 99);

    /* ---- leaf class: the (move/copy) x (init/assign) matrix ---- */

    /* move-INIT: char copied, source pointer + iterator nulled. */
    DefaultMove mi('a', ^a, ^d[0]);
    DefaultMove mi2 <-- mi;
    print(^mi);                   // a nullptr nullptr
    print(^mi2);                  // a 42 98

    /* copy-INIT: pointer + iterator shared, source unchanged. */
    DefaultMove ci('b', ^b, ^d[1]);
    DefaultMove ci2 = ci;
    print(^ci);                   // b 7 99
    print(^ci2);                  // b 7 99

    /* copy-ASSIGN onto an existing object. */
    ci2.c_ = 'c';
    mi2 = ci2;                    // mi2 -> {c,^b,^d[1]}
    print(^mi2);                  // c 7 99
    print(^ci2);                  // c 7 99

    /* move-ASSIGN onto an existing object: source pointer + iterator nulled. */
    DefaultMove ma('d', ^a, ^d[0]);
    mi2 <-- ma;                   // mi2 -> {d,^a,^d[0]}; ma -> {d,null,null}
    print(^mi2);                  // d 42 98
    print(^ma);                   // d nullptr nullptr

    /* ---- self-copy is a no-op (self-MOVE is a compile error — see negatives) ---- */
    DefaultMove same('s', ^a, ^d[0]);
    same = same;                  // self-copy: a no-op
    print(^same);                 // s 42 98

    /* ---- recursive: a class-typed field is moved by descending into it ---- */
    Outer o(('a', ^a, ^d[0]));    // o.inner_ = {a, ^a, ^d[0]}
    Outer o2 <-- o;               // recursive move -> inner pointer + iterator nulled in o
    print(^o.inner_);             // a nullptr nullptr
    print(^o2.inner_);            // a 42 98

    /* ---- iterative: an array of class is moved element-wise ---- */
    DefaultMove arr[2] = (('a', ^a, ^d[0]), ('b', ^b, ^d[1]));
    DefaultMove brr[2] <-- arr;   // each arr[i]'s pointer + iterator nulled
    print(^arr[0]);               // a nullptr nullptr
    print(^arr[1]);               // b nullptr nullptr
    print(^brr[0]);               // a 42 98
    print(^brr[1]);               // b 7 99

    /* ---- a class with TWO class fields: move recurses into BOTH ---- */
    Pair pr(('a', ^a, ^d[0]), ('b', ^b, ^d[1]));
    Pair pr2 <-- pr;
    print(^pr.a_);                // a nullptr nullptr
    print(^pr.b_);                // b nullptr nullptr
    print(^pr2.a_);               // a 42 98
    print(^pr2.b_);               // b 7 99

    /* ---- a pointer-free class: move is a pure copy, source untouched ---- */
    Plain pl('p', 9);
    Plain pl2 <-- pl;
    __println("pl.n_ = " + pl.n_);    // 9 (nothing nulled)

    /* ---- a class with an array-of-class field: move walks the elements ---- */
    Holder h((('a', ^a, ^d[0]), ('b', ^b, ^d[1])));
    Holder h2 <-- h;
    print(^h.items_[0]);          // a nullptr nullptr
    print(^h2.items_[0]);         // a 42 98

    /* ---- move into a FIELD target (not a bare variable) ---- */
    DefaultMove srcf('z', ^a, ^d[0]);
    pr2.a_ <-- srcf;              // overwrite pr2.a_; null srcf
    print(^pr2.a_);               // z 42 98
    print(^srcf);                 // z nullptr nullptr

    return 0;
}

/*
negatives — one //-block uncommented per run.
*/

/* a self-MOVE is rejected: a whole-value move nulls the source's pointer leaves,
   so moving a value onto itself would wipe it after the no-op store. */
//-EXPECT-ERROR: Cannot move a value onto itself.
//int neg_self_move() {
//    int a = 42;
//    DefaultMove dm('a', ^a);
//    dm <-- dm;
//    return 0;
//}

/* default move/copy require the SAME type on both sides — a different class is
   not whole-value moved/copied (it falls to the field spread, which mismatches). */
//-EXPECT-ERROR: Cannot implicitly convert 'Pair' to 'char'.
//int neg_cross_type_move() {
//    int a = 42;
//    Pair pr(('a', ^a), ('b', ^a));
//    DefaultMove dm <-- pr;
//    return 0;
//}
