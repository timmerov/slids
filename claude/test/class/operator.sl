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

it is assumed that creating temporary objects is expensive.
while evaluating expressions we want to minimize the number
of temporary objects that are created.
fuse binary operations whenever possible.
elide expression results into fresh variables when possible.
otherwise use move semantics to transfer a temporary to an
un-elidable variable.

Binary operations on temps are fused in place when
possible — covers everything compoundable (arithmetic,
bitwise, logical):

    Class temp + Type rhs
        -> temp.op+=(rhs)

Otherwise binary operations produce a fresh temp:

    Class temp = Class lhs + Type rhs
        -> temp.op+(lhs, rhs)

Some operators return a value instead of mutating self.
The returned type must be a built-in type;
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
widened to match.

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

/* ---- stage 1: user operator-method DEFINITIONS. Every op<sym> in the catalog
   PARSES and lowers as an ordinary method (operator USES land in later stages).
   Also exercises overloading by arity: op+/op- at 0/1/2, op^ at 0 (deref) / 2
   (xor), op~/op! at 0/1, and op<-- by parameter type (value vs pointer). */
OpDefs(
    int v_
) {
    /* assignment / move / swap */
    op=(int a)                 { v_ = a; }
    op<--(int a)               { v_ = a; }              // move from a value
    op<--(mutable OpDefs^ a)   { v_ = a^.v_; }          // move from a pointer (mutable)
    op<-->(mutable OpDefs^ c)  { int t = v_; v_ = c^.v_; c^.v_ = t; }

    /* binary -> self */
    op+(int a, int b)          { v_ = a + b; }
    op-(int a, int b)          { v_ = a - b; }
    op*(int a, int b)          { v_ = a * b; }
    op/(int a, int b)          { v_ = a / b; }
    op%(int a, int b)          { v_ = a % b; }
    op&(int a, int b)          { v_ = a; }
    op|(int a, int b)          { v_ = a; }
    op^(int a, int b)          { v_ = a; }              // binary xor (arity 2)
    op<<(int a, int b)         { v_ = a; }
    op>>(int a, int b)         { v_ = a; }
    op&&(int a, int b)         { v_ = a; }
    op||(int a, int b)         { v_ = a; }
    op^^(int a, int b)         { v_ = a; }

    /* compound assignment -> self */
    op+=(int a)                { v_ = v_ + a; }
    op-=(int a)                { v_ = v_ - a; }
    op*=(int a)                { v_ = a; }
    op/=(int a)                { v_ = a; }
    op%=(int a)                { v_ = a; }
    op&=(int a)                { v_ = a; }
    op|=(int a)                { v_ = a; }
    op^=(int a)                { v_ = a; }
    op<<=(int a)               { v_ = a; }
    op>>=(int a)               { v_ = a; }
    op&&=(int a)               { v_ = a; }
    op||=(int a)               { v_ = a; }
    op^^=(int a)               { v_ = a; }

    /* comparison -> built-in */
    bool op==(int a)           { return v_ == a; }
    bool op!=(int a)           { return v_ != a; }
    bool op<(int a)            { return v_ < a; }
    bool op>(int a)            { return v_ > a; }
    bool op<=(int a)           { return v_ <= a; }
    bool op>=(int a)           { return v_ >= a; }

    /* index, dereference -> reference */
    int^ op[](int i)           { return ^v_; }
    int^ op^()                 { return ^v_; }          // deref (arity 0)

    /* unary -> built-in (arity 0) */
    bool op+()                 { return v_ > 0; }
    bool op-()                 { return v_ < 0; }
    bool op~()                 { return v_ == 0; }
    bool op!()                 { return v_ == 0; }

    /* unary-from-operand -> self (arity 1) */
    op+(int a)                 { v_ = a; }
    op-(int a)                 { v_ = a; }
    op~(int a)                 { v_ = a; }
    op!(int a)                 { v_ = a; }
}

/* ---- stage 5: convert fallback. When no op= matches the arg EXACTLY, the integer
   arg WIDENS into a matching op=. Widening is flat-cost, so a single wider overload
   dispatches with no ambiguity (there is no smallest-widening-wins tiebreak — an arg
   that widened to two overloads equally would be "Ambiguous", see overload_*.sl). ---- */
Widen(
    int64 which_
) {
    op=(int64 a)  { which_ = a + 1000; }   // a narrower int WIDENS to int64 -> op= (convert)
}

/* a user op=(Copy^) copy operator, distinguishable from the default (+100), to prove
   decl-init dispatches it for a SAME-TYPE lvalue source — and ELIDES a class rvalue. */
Copy(
    int v_
) {
    op=(Copy^ o)  { v_ = o^.v_ + 100; }
}
Copy mkCopy() { Copy c(7); return c; }

/* a container with a class-typed field, to exercise op= onto a field STORE target
   (a complex lvalue). Without op= dispatch, `box.b_ = 31` would fail int -> OpDefs. */
Box(
    OpDefs b_
) { }

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

    /* ---- stage 2: assignment / move / swap / compound USES dispatch to the
       user operators (canon 51-68, 167-180). ---- */
    OpDefs od(7);
    od = 3;                       // op=(int)       -> v_ = 3
    od += 10;                     // op+=(int)      -> v_ = 13
    __println("od = " + od.v_);   // od = 13
    OpDefs oe(50);
    od <-- oe;                    // op<--(OpDefs^)  -> od.v_ = 50
    __println("od = " + od.v_);   // od = 50
    OpDefs of(1);
    od <--> of;                   // op<-->         -> od.v_ = 1, of.v_ = 50
    __println("od = " + od.v_);   // od = 1
    __println("of = " + of.v_);   // of = 50

    /* ---- stage 3 (partial): a single binary expression dispatches to the class's
       binary operator — `dest = X op Y` -> dest.opOP(X, Y) (canon 79-80). Operand
       chains (the fuse lowering) are a later slice. ---- */
    int m = 8;
    int n = 5;
    OpDefs og(0);
    og = m + n;                   // op+(int, int)  -> og.v_ = 13
    __println("og = " + og.v_);   // og = 13

    /* a binary CHAIN fuses into the destination: head via the 2-arg op, each further
       operand via the compound op (canon 70-80). `og` is the accumulator (no temp). */
    int r = 100;
    OpDefs oh(0);
    oh = m + n + r;               // oh.op+(m,n)=13; oh.op+=(r) -> 113
    __println("oh = " + oh.v_);   // oh = 113

    /* ---- stage 4: comparison returns a built-in; index / deref return references
       (canon 87-119). OpDefs' op[]/op^ back onto the scalar v_. ---- */
    bool eq = (oh == 113);        // oh.op==(int) -> true
    __println("eq = " + eq);      // eq = true
    int oi = oh[0];               // (oh.op[](0))^ -> v_ = 113
    __println("oi = " + oi);      // oi = 113
    oh[0] = 7;                    // (oh.op[](0))^ = 7   (writes v_)
    int oj = oh^;                 // (oh.op^())^ -> 7
    __println("oj = " + oj);      // oj = 7

    /* ---- convert fallback: int8 has no exact op=; it WIDENS to the op=(int64) overload
       (widening is flat-cost, so a single overload dispatches with no ambiguity). ---- */
    Widen wc(0);
    int8 wb = 7;
    wc = wb;                      // int8 widens to int64 -> wc.op=(int64); which_ = 7+1000
    __println("wc = " + wc.which_);   // wc = 1007

    /* ---- decl-init op= (fresh var): `Widen wd = 5` is default-construct THEN wd.op=(int64)
       (5 widens) -> which_ = 5+1000, NOT construction (which_ would be 5). Proves the
       operator is dispatched at the declaration site, same as the existing-var path. ---- */
    Widen wd = 5;
    __println("wd = " + wd.which_);   // wd = 1005

    /* ---- decl-init op= — SAME-TYPE source dispatches the user op=(Copy^) (+100, so
       distinct from a default copy), and a NAMED convertible lvalue widens into op=
       (vs wd's literal). Both prove default-construct-then-op= at the declaration. ---- */
    Copy cb(5);
    Copy ca = cb;                 // same-type lvalue -> ca.op=(Copy^); v_ = 5+100
    __println("ca = " + ca.v_);   // ca = 105
    int16 wn = 9;
    Widen we = wn;                // named int16 lvalue widens -> we.op=(int64); which_ = 9+1000
    __println("we = " + we.which_);   // we = 1009

    /* ---- value-category guard: a class RVALUE decl-init ELIDES; op= must NOT run (else
       v_ would be +100). Covers a function-return rvalue and a construction rvalue. ---- */
    Copy cd = mkCopy();           // function-return rvalue -> elide; v_ = 7 (not 107)
    __println("cd = " + cd.v_);   // cd = 7
    Copy ce = Copy(11);           // construction rvalue -> elide; v_ = 11 (not 111)
    __println("ce = " + ce.v_);   // ce = 11

    /* ---- decl-init MOVE / SWAP (fresh var). `<--` dispatches op<-- (default-construct
       then move); `<-->` default-constructs then op<--> (the fresh default flows back
       into the source — "weird but allowed"). The `=` copy vs `<--` move split is by the
       init operator; a class RVALUE source on `=` elides/moves instead (not shown). ---- */
    OpDefs omsrc(77);
    OpDefs omv <-- omsrc;             // omv.op<--(OpDefs^) -> omv.v_ = 77
    __println("omv = " + omv.v_);     // omv = 77
    OpDefs ossrc(88);
    OpDefs osw <--> ossrc;            // default-construct osw(0); osw.op<-->(ossrc)
    __println("osw = " + osw.v_ + " ossrc = " + ossrc.v_);   // osw = 88 ossrc = 0

    /* ---- destructure MOVE / SWAP: each slot binds via op<-- / op<--> against the
       source (declaring slots default-construct first; reusing slots exchange). ---- */
    (OpDefs, OpDefs) dsrc = (11, 22);
    (da, db) <-- dsrc;                // da.op<--(dsrc[0])=11, db.op<--(dsrc[1])=22
    __println("da = " + da.v_ + " db = " + db.v_);   // da = 11 db = 22
    (OpDefs, OpDefs) ssrc = (33, 44);
    OpDefs sa(1);
    OpDefs sb(2);
    (sa, sb) <--> ssrc;              // per-slot swap: sa<->ssrc[0], sb<->ssrc[1]
    __println("sa = " + sa.v_ + " sb = " + sb.v_
            + " s0 = " + ssrc[0].v_ + " s1 = " + ssrc[1].v_);   // sa=33 sb=44 s0=1 s1=2

    /* ---- destructure COPY dispatches per-slot op=, same by-slot rule as move/swap:
       `(a, b) = src` -> `a = src[0]; b = src[1]`. A CONVERTING source (int -> OpDefs via
       op=(int)) proves the dispatch — without it, int -> OpDefs would not convert. ---- */
    (int, int) dcs = (11, 22);
    (OpDefs dca, OpDefs dcb) = dcs;   // dca.op=(11), dcb.op=(22)
    __println("dca = " + dca.v_ + " dcb = " + dcb.v_);   // dca = 11 dcb = 22

    /* ---- op= onto a COMPLEX lvalue (field / deref / index) dispatches the target
       type's user op=, same rule as a bare-name target (shared dispatchAssignInit
       funnel). Without dispatch each would fail to convert int -> OpDefs. ---- */
    Box sbox;
    sbox.b_ = 31;                 // field store  -> sbox.b_.op=(int) -> v_ = 31
    __println("sf = " + sbox.b_.v_);   // sf = 31
    OpDefs^ sbp = ^sbox.b_;
    sbp^ = 41;                    // deref store  -> op=(int) -> v_ = 41
    __println("sd = " + sbox.b_.v_);   // sd = 41
    OpDefs sarr[2];
    sarr[1] = 51;                 // index store  -> op=(int) -> v_ = 51
    __println("si = " + sarr[1].v_);   // si = 51

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

/* ---- stage 1 signature negatives: the op<sym> validation pass (arity, no default
   parameter values, no misplaced 'mutable'). One //-block uncommented per run. ---- */

/* an operator parameter may not carry a default value. */
//-EXPECT-ERROR: An operator parameter may not have a default value.
//Neg1(int v_) {
//    op+(int a = 5, int b) { v_ = a; }
//}

/* op+ accepts 0, 1, or 2 parameters — three is over the maximum. */
//-EXPECT-ERROR: The 'op+' operator takes 0, 1, or 2 parameters, not 3.
//Neg2(int v_) {
//    op+(int a, int b, int c) { v_ = a; }
//}

/* a binary-only operator (op*) requires exactly two parameters. */
//-EXPECT-ERROR: The 'op*' operator takes exactly 2 parameters, not 1.
//Neg3(int v_) {
//    op*(int a) { v_ = a; }
//}

/* comparison takes exactly one parameter. */
//-EXPECT-ERROR: The 'op==' operator takes exactly 1 parameter, not 0.
//Neg4(int v_) {
//    bool op==() { return v_ == 0; }
//}

/* swap takes exactly one (same-class, mutable) parameter. */
//-EXPECT-ERROR: The 'op<-->' operator takes exactly 1 parameter, not 2.
//Neg5(int v_) {
//    op<-->(mutable Neg5^ a, int b) { v_ = b; }
//}

/* index takes exactly one parameter. */
//-EXPECT-ERROR: The 'op[]' operator takes exactly 1 parameter, not 0.
//Neg6(int v_) {
//    int^ op[]() { return ^v_; }
//}

/* 'mutable' is a move/swap-only qualifier — every other operator forbids it. */
//-EXPECT-ERROR: The 'op+' operator parameter may not be 'mutable'.
//Neg7(int v_) {
//    op+(mutable int^ a, int b) { v_ = b; }
//}

/* ---- stage 1b type-dependent negatives (classify): return categories, move/swap
   mutability + same-class, and naked operators. One //-block uncommented per run. */

/* comparison must return a built-in — void (an omitted return) is not one. */
//-EXPECT-ERROR: The 'op==' operator must return a built-in type (bool, an integer, a float, or a pointer).
//Neg8(int v_) {
//    op==(int a) { v_ = a; }
//}

/* index must return a reference, not a value. */
//-EXPECT-ERROR: The 'op[]' operator must return a reference '^'.
//Neg9(int v_) {
//    int op[](int i) { return v_; }
//}

/* a produce-self operator must not spell a return type. */
//-EXPECT-ERROR: The 'op+' operator produces self and must not have a return type.
//Neg10(int v_) {
//    int op+(int a, int b) { v_ = a; return a; }
//}

/* a move operator's pointer parameter must be mutable. */
//-EXPECT-ERROR: A move operator's pointer parameter must be 'mutable'.
//Neg11(int v_) {
//    op<--(Neg11^ a) { v_ = a^.v_; }
//}

/* a swap operator's parameter must be mutable. */
//-EXPECT-ERROR: A swap operator's parameter must be 'mutable'.
//Neg12(int v_) {
//    op<-->(Neg12^ a) { v_ = 0; }
//}

/* a swap operator's parameter must reference the SAME class. */
//-EXPECT-ERROR: A swap operator's parameter must be a reference to the same class.
//NegOther(int w_) {}
//Neg13(int v_) {
//    op<-->(mutable NegOther^ a) { v_ = 0; }
//}

/* no naked operators — an operator must be a class method. */
//-EXPECT-ERROR: An operator can only be defined as a method of a class.
//op+(int a, int b) { }
