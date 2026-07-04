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
opeartor parameters may not have default values.
a move pointer parameter must be explicit mutable.
the swap parameter must be explicit mutable.
the parameters to all other operators may not be mutable.

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
        op<-->(mutable Class^ c);               obj1 <--> obj2;

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
        Type^ op[](ConstType a);                obj[a] = obj[b];
        Type^ op^();                            obj1^ = obj2^;

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

*/

/*
claude says:

 — IMPLEMENTATION PLAN for landing user-defined operator overloading.
(The spec above is canon. This section is my notes.)

STATUS QUO (what exists vs what is missing)
  * LANDED: the SYNTHESIZED default copy / move / swap — the per-field iterative+
    recursive walk (nulls pointer/iterator leaves on move; see codegen.cpp move husk
    ~2212). Exercised by the DefaultMove/Outer/Pair/Plain/Holder tests below.
  * NOT LANDED: user-written `op<sym>(...)` overloads. The `op` keyword LEXES
    (lex.cpp:329 -> token::kOp) but grammar.cpp NEVER consumes kOp, so an operator
    method does not even PARSE today. And every operator EXPRESSION on a class operand
    currently ERRORS in classify with "Operator 'X' is not defined on type 'Y'".
  * FILE-LAYOUT NOTE: the real phases are grammar.cpp (parser), classify.cpp,
    codegen.cpp — there is NO parser.cpp / codegen_expr.cpp / codegen_stmt.cpp (the
    CLAUDE.md pipeline section names those; they are stale for v2).

ARCHITECTURE (the one decision that matters)
  Do NOT port v1/compiler/codegen_overload.cpp. v1 resolved operators by matching
  type-name STRINGS in codegen (argBindsToParam(std::string,std::string)). v2 types in
  classify on widen::TypeRef and codegen is string-free. So operator overloading is a
  CLASSIFY-side desugar: rewrite each operator expression form into a method CALL
  (self.op<sym>(args)) and resolve it with the EXISTING typed overload engine
  `pickOverload` (classify.cpp:1839; recv_offset=1 for methods, as at classify.cpp:3079).
  The smallest-widening-wins convert rule layers on pickOverload's existing widening.

INTERNAL NAMING
  Source `op<--` (move) / `op<-->` (swap) are the current spellings; keep the method
  name as the source spelling (op<--, op<-->) — do NOT reintroduce v1's obsolete
  op<- / op<-> shorthand. Copy is op=. op^ is OVERLOADED by arity: op^() = dereference,
  op^(a,b) = binary xor.

LOWERING CONTRACT (from the spec above — implement as classify rewrites)
  assignment   Class lhs = rhs            -> lhs.op=(rhs)
  move         Class lhs <-- rhs          -> lhs.op<--(rhs)         (+ null rhs if a ptr)
  swap         Class lhs <--> rhs         -> lhs.op<-->(rhs)
  binary FUSE  (fresh temp) temp OP rhs   -> temp.op OP= (rhs)      in place
  binary FRESH temp = a OP b              -> temp.opOP(a, b)        (a,b passed in)
  compound     lhs OP= rhs                -> lhs.opOP=(rhs)
  comparison   x = (a == b)               -> x = a.op==(b)          (built-in result)
  index        rhs[i]                     -> rhs.op[](i)^           (deref -> lvalue)
  deref        rhs^                        -> rhs.op^()^             (deref -> lvalue)
  unary arity1 temp = -operand            -> temp.op-(operand)      (returns self)
  unary arity0 if (-a)                    -> a.op-()                (built-in result)
  convert      no exact overload          -> target.op=(...) + integer widen, smallest wins
  The FUSE-vs-FRESH split keys on "is the lhs a fresh temp" (v1: isFreshSlidTemp) — the
  one subtle bit; index/deref lower THROUGH the existing `^`-deref lvalue machinery.

SIGNATURE VALIDATION (v2 spec == v1 enforcement; port v1 rules into a classify/resolve
pass — v1 did arity in the parser, but v2 has types resolved later, so do it post-resolve)
  ARITY (explicit params, self implicit) — v1 parser.cpp:1267:
    op= op<-- op<--> : 1 | op+ op- : 0,1,2 | op* op/ op% op& op| op<< op>> op&& op|| op^^ : 2
    op^ : 0 (deref) or 2 (xor) | op~ op! : 0,1 | compound op+=..op^^= : 1
    comparison op==..op>= : 1 | op[] : 1
  NO DEFAULT PARAM VALUES on any operator (v1 parser.cpp:1283; spec line 131).
  MUTABLE (v1 parser.cpp:1298): op<-- / op<--> REQUIRE `mutable` on a pointer param;
    EVERY other operator FORBIDS `mutable` on any param (spec line 134).
  SWAP: op<--> takes exactly one `SameClass^` param (v1 codegen.cpp:371).
  RETURN TYPE: comparison ops and unary ARITY-0 (op+/-/~/! with 0 params) must return a
    BUILT-IN / Primitive (bool/int/float/pointer), never a class/self (v1 codegen.cpp:379).
    op[] and op^ must return a REFERENCE `Type^` — v2 SHOULD enforce this; v1 did NOT
    (it only return-checks comparison + arity-0 unary). Every other operator has no
    return (returns self).
  Params must be Primitive or a (non-mutable) reference; a bare `Type^` IS the
  non-mutable "pointer to const" — NOT a `const` keyword requirement (matches the v1
  reference test, which uses bare `Overload^`/`Simple^`/`int` params throughout).

DEFAULT-SYNTHESIS INTERACTION
  A user op= / op<-- / op<--> SHADOWS the synthesized default (define-iff-not-defined,
  spec 207). The two ctor sequences (spec 215-228): NO matching op -> copy/move fields
  then ctor; matching op -> init fields, ctor, THEN call the op. Wire the
  user-op lookup where the default class-assign dispatch lives (classify checkSlidAssign
  classify.cpp:836/2059 — today it defers: "class target is deferred (no op= yet)"
  classify.cpp:365).

STAGING (each stage lands with a ported slice of v1/test/operators.sl into the body below)
  1. PARSER: consume kOp + the full operator-symbol set (incl. [] as two tokens, the
     <-- / <--> arrows, and the compound `=` combos) into an `op<sym>` method name.
     Then the VALIDATION pass (arity / mutable / return / swap / no-defaults) — this
     alone unlocks the whole negative catalog (v1 has ~90 negatives).
  2. assignment / move / swap / compound (op= op<-- op<--> op+=..) — composes with the
     existing default class-assign dispatch.
  3. binary + unary (the FUSE-vs-FRESH split).
  4. comparison (built-in return) + index/deref (through `^`).
  5. convert fallback (target op= + smallest-widening).

TEST PLAN
  Port v1/test/operators.sl in the same stage order (Overload defines every op across
  the Overload^/int/Simple^ param families; Comparison = non-bool return; MovePtr = move
  nulls source; BadReturn / BadMutable = negatives). Keep the default-operator tests that
  are already below. One //-EXPECT-ERROR per negative, matching the v2 messages.
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
