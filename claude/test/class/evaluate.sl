/*
optimize the evaluation of expressions that need temporary class objects.

    String s = String + a + b + c + d;

if the String class defines op= for a's type and op+= for b,c,d's types
then that entire variable declaration statement can be evalutated with
zero temporary String objects.

    s = a; s += b; s += c; s += d;

in cases where lhs is existing object variable and rhs is a temp,
move (not copy) semantics should be used when possible.

    String s; s = String + a + b + c + d;
    temp = a; temp += b; temp += c; temp += d; s <-- temp;

basic assumptions:

    A a;
    B b;
    C c;
    D d;
    String:op=(String);
    String:op<--(String);
    String:op=(A);
    String:op=(B);
    String:op=(C);
    String:op=(D);

and we want to evaluate these chains:

    String s1 = String + a + b + c + d;
    String s2; s2 = String + a + b + c + d;
    String s3;
    String s4 = s3 + a + b + c + d;
    String s5; s5 = s3 + a + b + c + d;

String must define += or + operators or both.

    String:op+=(A);
    String:op+=(B);
    String:op+=(C);
    String:op+=(D);

    String:op+(String, A);
    String:op+(String, B);
    String:op+(String, C);
    String:op+(String, D);

if only += then the chains become:

    String s1 = a; s1 += b; s1 += c; s1 += d;
    s2 = a; s2 += b; s2 += c; s2 += d;
    String s4 = s3; s4 += b; s4 += c; s4 += d;
    s5 = s3; s5 += b; s5 += c; s5 += d;

if only + then the chains become (ignoring ping-pong):

    temp1 = a + b; temp2 = temp1 + c; String s1 = temp2 + d;
    temp1 = a + b; temp2 = temp1 + c; s2 = temp2 + d;
    temp1 = s3 + a; temp2 = temp1 + b; temp3 = temp2 + c; String s4 = temp3 + d;
    temp1 = s3 + a; temp2 = temp1 + b; temp3 = temp2 + c; s5 = temp3 + d;

if both then the chains become:

    String s1 = a + b; s1 += c; s1 += d;
    s2 = a + b; s2 += c; s2 += d;
    String s4 = s3 + b; s4 += c; s4 += d;
    s5 = s3 + b; s5 += c; s5 += d;


notes:

some things deferred from other landings that might apply here:

  - Copy-into-target instead of move — the expression-temp is copied into an existing-var target (one extra copy).
  - only-op+= seed — a class with op+= but no 2-arg op+ can't seed a fused chain.
  - Ping-pong two-buffer chains (only op+, no op+=).

check the dtor for these temps run at the end of the statement.
not the end of the enclosing scope.
i think we should be using the sequence-expression machinery here.
seq-exp = ( init/construct, function-call, destruct )

    int x = fn(String + "Hello");
    x = fn(String + "Hello");
    fn(String + "Hello");

this produces 11 ctor/dtors.

    ((a, b), c) = ((Class(1), Class(2)), Class(3));

does not compile:

    ((a, b), c) <--  ((Class(1), Class(2)), Class(3));
    ((a, b), c) <--> ((Class(1), Class(2)), Class(3));

some claude detritus:

  - Chains a = b + c + d — the whole fuse/fresh/seed/ping-pong design, and the elide-if-fresh / move-if-existing / never-copy
  tail. Chains currently error (cleanly — not a miscompile).
  - Unary (a = -b → a.op-(b) is an easy 1-arg rewrite; if(-a) → a.op-() is stage-4-like).
  - Aliasing a = a + c (the direct rewrite reads+writes a — needs a temp).
  - Decl-init binary Class r = a + b (same kVarDeclStmt gap as decl-init op=).
  - Only-op+= head (seed via op=) and ping-pong (only op+).
  - User op<-- for the =-class-rvalue implicit move (class rvalues currently take the existing default move/elide — wiring a
  user op<-- into that return/move path is separate). [i think this is use move semantics

*/

/*
claude says:

these tests cover the evaluation cases that are currently CORRECT: a temp
created to evaluate an expression has its dtor run at the end of the phrase
(the statement, or a condition's evaluation), not at the end of the enclosing
scope. a NESTED arg temp in a var-decl / assign / return rhs whose VALUE is a
scalar is wrapped in a kSeqExpr, so its dtor also runs at the statement -- this
was the bug of bug.sl.

cases B/C/E/F/G/H/I: the discarded-call, method-receiver, condition, and basic
scalar-decl paths. cases 1-8: the seq-wrap fix across statement kinds and edges
-- 1 assign rhs, 2 return rhs (temp dies before the fn unwinds its locals), 3a/3b
a ++ in the value child / in the temp's construction arg (the lowerInPhrase
assert-relaxation path), 4 two reverse-ordered temps, 5 a loop-body decl (per-
iteration, no growth), 6 a pointer-valued rhs, 7 a CLASS-valued rhs -- once the
boundary where the wrap gave up, now covered too, 8 aug-assign (lowers to assign,
so covered too).

THE RHS TEMP SEQ IS NO LONGER SCALAR-ONLY (case 7). A nested arg temp in a var-decl /
assign / return rhs is STATEMENT-scoped whatever the rhs VALUE is. It used to be scalar-
only, because a CLASS-valued rhs is built IN PLACE by the statement's sret paths and those
matched on the RAW call node -- wrapping it in a seq hid the call and forced an extra copy,
so the wrap was declined and the temp fell back to enclosing-scope lifetime. Codegen now
OPENS the seq at those three statements (openRhsSeq): it constructs the temps, hands the
sret path the seq's VALUE child, and destroys the temps after -- so construction in place
and statement-scoped temps hold at ONCE. Case 7 pins it, and block P got the same fix for
free (its `_$dsrc` spill is a class-bearing tuple, so its accumulators had leaked for
exactly the same reason).

cases J and K: the CHAIN LADDER. Classes Acc and Str print their ctor/dtor, so every
temporary announces itself and the golden IS the temp count. Acc has a 2-arg op+ (plus
op= and op+=); Str has ONLY op= and op+= -- the canon 51-56 class, and the only one that
can exercise the seed decompose. Both blocks cost the MINIMUM: a chain bound to fresh
storage builds straight into it (zero temps -- J1/J2, K1/K2/K3, and K6 through NRVO); a
chain bound to a LIVE object takes exactly one temp, moved in and destroyed at the
semicolon (J3, K4/K5). Dispatch is on the LHS OPERAND's class throughout -- the target
never gets a say (that would invert precedence, which is what killed classify's old
target-keyed tryLowerBinaryChain). J's counts were 12 ctor/dtor pairs before the ladder
landed and are 7 now; that shrinkage was the test.

case U: the ARITY-1 UNARY, on the SAME road as the binary. `-a` PRODUCES a class value, so
it is a chain node too: classify stamps it (the operand's class picks the operator -- never
the assignment target, which would invert precedence exactly as the deleted target-keyed
fuse did), and desugar's lowering answers the one question that matters, where the
accumulator lives. Fresh storage IS the accumulator (U1 decl, U6 return through NRVO): zero
temps. A LIVE target takes one temp, moved in through the class's op<-- and dead at the
semicolon (U2). A unary at a chain's HEAD collapses into the accumulator like a head
construction (U3); anywhere else it is an ordinary operand with its own temp (U4 in a chain,
U5 as a call arg, U7 over a sub-chain). The old path built a `_$optmp` in classify and COPIED
it into the destination -- an extra object at every site, a copy where the author's op<--
belonged, and a temp that outlived its statement.

NOT a defect -- SPEC (see nameless.sl): a bare `Class(5);` statement is FORM 1, an unnamed
local VARIABLE. It is initialized at site, its ctor runs, and its dtor runs at the END OF
SCOPE, exactly like a named local. Only the EXPRESSION form (form 2 -- a construction used
inline as a receiver / arg / field read) is a temporary that dies at the statement.

A TUPLE OF CLASSES IS EVALUATED SLOT BY SLOT, into the storage that already owns it. The
source shape decides how (classify's destructure source model + codegen's tuple<->literal
bridge), and NO shape materializes the tuple twice:
  - a tuple LITERAL is taken APART: element i is built directly into slot i. A construction
    element runs ONE ctor, in place (block V) -- it is not built as a temp and copied in.
    An lvalue element dispatches that class's op= into the slot (the transfer invariant: a
    class copy is its operator, never a blit -- a whole-value build would have loaded and
    blitted every element right past a user's op=).
  - an LVALUE source is INDEXED per slot -- `src[i]`, cloned, no temp. Nesting recurses on
    it, so a nested slot needs no sub-tuple temp of its own, and COPY / MOVE / SWAP all
    work at any depth (block V): a move nulls the real leaf, a swap exchanges with it.
  - only an RVALUE source (a call, a chain) still spills to `_$dsrc`, because it must be
    evaluated exactly once. So must a literal that READS A TARGET: `(sa, sb) = (sb, sa)` is
    a swap, and per-slot stores would alias (tuple/destructure.sl pins it).

A CLASS CAN ONLY BE COPIED INTO -- so it has to EXIST first. Every binding is alloc, init,
ctor, THEN the transfer (block W). It used to FILL the storage from the source and only THEN
run the ctor hooks, so the constructor landed on top of the copied value: a ctor that WRITES
its own field silently threw the copy away (`D dx = dy;` yielded the ctor's value, not dy's).
Every class in this file only PRINTS in its ctor, which is why it lived here so long -- a
printing ctor shows a wrong ORDER, and only a WRITING one (class Ord) shows a wrong ANSWER.
classify peels the transfer off the declaration (applyTransferSplit) and re-emits it against
the constructed object, per SLOT -- so `(Ord(1), wa)` still BUILDS slot 0 in place and only
copies into slot 1. What decides whether a copy happens at all is the SOURCE: an rvalue has
no object to copy FROM, so it elides and builds in place, exactly as before.

deliberately NOT correct yet (still-broken / deferred):
  - ping-pong: a class with a 2-arg op+ but NO op+= can't fuse (acc.op+(acc,c) would
    read acc while writing it), so each such step starts a fresh buffer -- one temp per
    un-fusable operand. Two alternating buffers would need none.
  - a class FIELD initialized from a class LVALUE (`Holder h( c )`) is still FILLED and then
    constructed over -- the same bug one level down, and NOT fixable the same way. A
    construction's arguments are FIELD INITIALIZERS, and a ctor must see its fields ALREADY
    INITIALIZED (Q4's Hook computes `a_ + b_` in its ctor body and must read what was passed,
    not the defaults). So a field's copy cannot be hoisted past the enclosing ctor the way a
    tuple slot's can: it has to land BETWEEN the field's own ctor and the enclosing ctor body.
  - a GLOBAL class initialized from a class LVALUE (`global Add gb = ga;`) is still FILLED and
    then constructed over -- the same wrong answer at a third site. A global's initializer is
    lowered into a synthesized LAZY CTOR (desugar), so the declarator split that fixed the
    local never reaches it. Untested on purpose: a golden here would pin the wrong answer.
*/

Class(int a_) {
    _() { __println("Class:ctor: " + a_); }
    ~() { __println("Class:dtor: " + a_); }
    void inc() { ++a_; }
}

int fn(Class^ c) {
    c^.inc();
    return c^.a_;
}

int fn2(Class^ x, Class^ y) {
    return x^.a_ + y^.a_;
}

// a scalar-returning function with a NAMED local, used to observe that a temp in
// a `return fn(Class(n))` rhs is destroyed BEFORE the function unwinds its locals.
int ret_rhs() {
    Class keep = Class(70);
    return fn(Class(71));
}

// a POINTER-returning function: exercises the guard's kPointer arm (a class temp
// arg to a call whose VALUE is a pointer is still seq-wrapped).
int^ ptr_rhs(Class^ c, int^ p) {
    c^.inc();
    return p;
}

// a CLASS-returning function (built in place / RVO): the guard-negative boundary --
// its class temp arg is NOT seq-wrapped, so the temp keeps enclosing-scope lifetime.
Class make(Class^ c) {
    c^.inc();
    return Class(c^.a_ + 100);
}

// An accumulator class for the binary-chain TEMP BASELINE (case J). Its operators are
// spec-conforming: a binary op's FIRST parameter is the enclosing class (Acc^), so
// `a + b` dispatches on the LHS OPERAND's class -- never on the assignment target.
// ctor/dtor PRINT, and that is the whole point: temps are COUNTABLE here. operator.sl's
// OpDefs has no ctor/dtor, so a fuse and a no-fuse print the same thing there -- which is
// how the old target-keyed chain fuse survived with green goldens. Counting lives here.
Acc(int v_) {
    _() { __println("Acc:ctor: " + v_); }
    ~() { __println("Acc:dtor: " + v_); }
    op=(Acc^ r)         { v_ = r^.v_; }
    op<--(mutable Acc^ r) { __println("Acc:op<--: " + r^.v_); v_ = r^.v_; }
    op+=(Acc^ r)        { v_ += r^.v_; }
    op-=(Acc^ r)        { v_ -= r^.v_; }
    op+(Acc^ x, Acc^ y) { v_ = x^.v_ + y^.v_; }
    // a chain inside a METHOD body -- the chain lowering runs in a lifted method exactly
    // as it does in a free function.
    int combine(Acc^ x, Acc^ y) {
        Acc t = x^ + y^ + x^;
        return t.v_;
    }
}

// A class-typed FIELD, for a chain stored into `box.a_`.
Box(Acc a_) { }

// A tuple-of-classes returning function: block Z6 spills it (an operand is read by EVERY
// slot, so one that cannot be re-read must be evaluated exactly once).
(Acc, Acc) mkAccPair() { return (Acc(1), Acc(2)); }

// Block Z7's observer. Ord (the block-W observer) has no operators at all -- that is the
// point of it -- so it cannot be a chain operand. Zord is the same idea with the operators
// a chain needs: a ctor that WRITES its own field, so a slot that is FILLED and only then
// constructed reads back 99 instead of the sum. It does not print; Z7 reads VALUES.
Zord(int v_) {
    _()  { v_ = 99; }
    ~()  { }
    op=(Zord^ r)           { v_ = r^.v_; }
    op+(Zord^ x, Zord^ y)  { v_ = x^.v_ + y^.v_; }
    int get() { return v_; }
}

// THE ORDER OF CONSTRUCTION (block W). Its ctor WRITES its own field, which is the only
// thing that makes the order OBSERVABLE: if the ctor runs AFTER the copy it overwrites the
// copied value, and the variable ends up holding 99 instead of what it was copied from.
// Every counting class in this file merely PRINTS in its ctor, which is exactly why the bug
// lived here for so long -- a printing ctor shows a wrong ORDER, a writing ctor shows a
// wrong ANSWER. It has no operators at all: the DEFAULT copy/move is the path that was
// broken (a user op= was already dispatched as `x.op=(y)` AFTER the construct, and so was
// always right), and it deliberately does not print -- W reads VALUES back.
Ord(int v_) {
    _()  { v_ = 99; }
    ~()  { }
    int get() { return v_; }
}

// The TUPLE-OF-CLASSES counter (block V). Every lifecycle op prints, including all three
// transfers -- a tuple slot filled by a blit instead of by the operator is then VISIBLE
// (a missing Trk:op= line), which is what the transfer invariant is about. A move husks
// its source so V4 can show the source really was emptied.
// A class whose FIELDS are classes, and a tuple-of-classes returning function: block X spills
// the returned tuple and spreads it across the fields.
Trkpair(Trk a_, Trk b_) { }
(Trk, Trk) mkTrkTup() { return (Trk(1), Trk(2)); }

Trk(int v_) {
    _()  { __println("Trk:ctor: " + v_); }
    ~()  { __println("Trk:dtor: " + v_); }
    op=(Trk^ r)             { __println("Trk:op=: " + r^.v_); v_ = r^.v_; }
    op<--(mutable Trk^ r)   { __println("Trk:op<--: " + r^.v_); v_ = r^.v_; r^.v_ = 0; }
    op<-->(mutable Trk^ r)  {
        __println("Trk:op<-->: " + r^.v_);
        int t = v_;
        v_ = r^.v_;
        r^.v_ = t;
    }
}

int take(Acc^ a) { return a^.v_; }

// An INHERITED operator. Its params are typed as the BASE, so a DERIVED operand must bind
// to a `BaseOp^` param -- a derived VALUE into a base REFERENCE. That is an upcast (the base
// is the derived's slot-0 sub-object, same address, nothing sliced), NOT a conversion. It
// used to be rejected, which made every user-written base operator dead from a derived
// operand: `DerOp + DerOp` reported "Operator '+' is not defined", and `DerOp += DerOp` fell
// through the aug-assign hole into INVALID IR (a struct `add`).
BaseOp(int v_) {
    op=(BaseOp^ r)            { v_ = r^.v_; }
    op+=(BaseOp^ r)           { v_ += r^.v_; }
    op+(BaseOp^ x, BaseOp^ y) { v_ = x^.v_ + y^.v_; }
    int get() { return v_; }
}
BaseOp : DerOp( ) { }

// A VIRTUAL operator -- the chain's op call must dispatch through the vtable.
Vop(int v_) {
    virtual op+=(Vop^ r) { v_ += r^.v_ + 1000; }
    op=(Vop^ r)          { v_ = r^.v_; }
    int get() { return v_; }
}

// SHIFT and LOGICAL chains. stampClassBinary is shared by the arith, shift and logical arms,
// but only + and - were covered.
Shf(int v_) {
    op=(Shf^ r)           { v_ = r^.v_; }
    op<<=(Shf^ r)         { v_ = v_ << r^.v_; }
    op&&=(Shf^ r)         { v_ = v_ & r^.v_; }
    op<<(Shf^ x, Shf^ y)  { v_ = x^.v_ << y^.v_; }
    op&&(Shf^ x, Shf^ y)  { v_ = x^.v_ & y^.v_; }
}

// A destination whose class DIFFERS from the chain's result class -- not the chain lowering's
// business: the chain yields an Acc, and the binding funnel converts it through Oth's op=(Acc^).
Oth(int w_) { op=(Acc^ r) { w_ = r^.v_ + 500; } }

// A chain inside a CONSTRUCTOR and a DESTRUCTOR body.
Hook(int v_, Acc a_, Acc b_) {
    _() { Acc t = a_ + b_;  v_ = t.v_;  __println("Hook:ctor chain: " + v_); }
    ~() { Acc t = a_ + b_;  __println("Hook:dtor chain: " + t.v_); }
}

// A GLOBAL as a chain destination (a live target -- one temp, moved in). A global takes its
// fields as DATA (`gacc(100)`); `= Acc(100)` is a construction EXPRESSION and is rejected.
global Acc gacc(100);

// The BLAST RADIUS of "a derived value binds to a base reference param". Two overloads, one
// taking the BASE and one the DERIVED: an EXACT derived param must still WIN. The new rung-2
// upcast must not steal from an exact match -- if it ever did, the failure would be SILENT
// (the wrong overload, right types), which no other test in the suite would notice.
Rank(int v_) { int tag() { return v_; } }
Rank : Der( ) { }
int pick(Rank^ b) { return 100; }
int pick(Der^ d)  { return 200; }
int base_only(Rank^ b) { return 300 + b^.tag(); }

// A VIRTUAL base. A derived VALUE passed to a `VBase^` param is the SAME address (the base is
// the derived's slot-0 sub-object, vptr included), so DYNAMIC DISPATCH must survive the
// upcast. Also silent if it broke.
VBase(int v_) {
    _() { }
    virtual ~() { }
    virtual int who() { return 1; }
}
VBase : VDer( ) {
    virtual int who() { return 2; }
}
int ask(VBase^ b) { return b^.who(); }

// A hook-less class with a POINTER field: a move must run the operator AND leave the source a
// valid husk (the null-the-leaf half of a move).
Ptr(int^ p_) {
    op=(Ptr^ r)           { __println("Ptr:op=");   p_ = r^.p_; }
    op<--(mutable Ptr^ r) { __println("Ptr:op<--"); p_ = r^.p_; r^.p_ = nullptr; }
}

// The ONLY-op+= class of canon 39-56: op= and op+= and NO 2-arg op+. Acc cannot
// exercise this -- with a 2-arg op+ the head pair is one call, so the seed decompose
// (`acc.op=(x); acc.op+=(y)`) never runs. ctor/dtor PRINT, so temps stay countable.
Str(int v_) {
    _() { __println("Str:ctor: " + v_); }
    ~() { __println("Str:dtor: " + v_); }
    op=(Str^ r)  { v_ = r^.v_; }
    op<--(mutable Str^ r) { __println("Str:op<--: " + r^.v_); v_ = r^.v_; }
    op+=(Str^ r) { v_ += r^.v_; }
}

// A TRIVIAL class -- NO ctor, NO dtor. This is the bucket where codegen's transfer gates
// used to ask "does this type run hooks?" and, hearing no, fall through to a BLIT TWIN that
// re-implemented the transfer inline and silently skipped the class's operator. Acc and Str
// cannot catch that: they have constructors, so they were on the dispatching side of the
// gate all along. Triv has nothing but operators, and they PRINT -- so the golden pins that
// a whole-class transfer runs the class's move/copy function even with no hooks at all.
Triv(int v_) {
    op=(Triv^ r)           { __println("Triv:op=: " + r^.v_); v_ = r^.v_; }
    op<--(mutable Triv^ r) { __println("Triv:op<--: " + r^.v_); v_ = r^.v_; }
    op+=(Triv^ r)          { v_ += r^.v_; }
}

// Buf: a 2-arg op+ but NO op+= -- a chain on it CANNOT fuse, because `acc.op+(acc, c)`
// would read the accumulator while writing it. So every un-fusable operand starts a fresh
// BUFFER: one extra object per step. Each buffer must die at the SEMICOLON. (Reusing two
// alternating buffers -- "ping-pong" -- would need none of them; still deferred.)
Buf(int v_) {
    _() { __println("Buf:ctor: " + v_); }
    ~() { __println("Buf:dtor: " + v_); }
    op=(Buf^ r)         { v_ = r^.v_; }
    op+(Buf^ x, Buf^ y) { v_ = x^.v_ + y^.v_; }
}

// Sw: hook-less, with a printing SWAP as well as copy/move -- the swap transfer gate has
// no other test in the suite.
Sw(int v_) {
    op=(Sw^ r)            { __println("Sw:op=: " + r^.v_); v_ = r^.v_; }
    op<--(mutable Sw^ r)  { __println("Sw:op<--: " + r^.v_); v_ = r^.v_; }
    op<-->(mutable Sw^ r) { __println("Sw:op<-->"); int t = v_; v_ = r^.v_; r^.v_ = t; }
}

// Block Y's class. It is SILENT (the block reads VALUES back, not a print order) and -- unlike
// Ord -- it KEEPS its constructor argument, because Y is about WHICH source got read and HOW
// MANY TIMES, not about the construct-vs-copy order that Ord's overwriting ctor exists to catch.
Val(int v_) {
    _()  { }
    ~()  { }
    int get() { return v_; }
}
global int y_calls = 0;      // how many times the source EXPRESSION was evaluated
global int y_effects = 0;    // the side effects of the source's elements
((Val, Val), Val) mkValNest() { return ((Val(1), Val(2)), Val(3)); }
(Val, Val) valCounted() { y_calls = y_calls + 1; return (Val(4), Val(5)); }
Val valBump(int n) { y_effects = y_effects + n; return Val(n); }

// A copy-init inside a NESTED function. A nested function's body is a STATEMENT in its host, so
// it reaches the desugar passes only through the host -- the copy-into ordering (block W) has to
// hold there too, and it is the one place a pass that walks program-scope functions would miss.
void yNestedHost() {
    Val a = Val(1);
    a.v_ = 7;
    void inner() {
        Val b = a;
        __println("Y6: nested copy=" + b.get());                                   // 7
    }
    inner();
}

// A chain in a RETURN rhs: the accumulator is the returned local, which NRVO aliases
// to the sret slot -- so the caller's variable IS the accumulator and no temp exists.
Str chain_ret(Str^ x, Str^ y) {
    return Str + x^ + y^;
}

// Neg: an ARITY-1 UNARY producer. `-a` yields a whole class value, exactly as a binary does,
// so it takes the binary's road -- stamped in classify, lowered in desugar -- and the
// destination is the accumulator wherever that destination is raw storage. It used to build a
// `_$optmp` in classify and COPY it into the target: an extra object, a copy where the class's
// move belonged, and a temp that outlived its statement. ctor/dtor PRINT, so the count is the
// assertion, as everywhere else in this file.
Neg(int v_) {
    _() { __println("Neg:ctor: " + v_); }
    ~() { __println("Neg:dtor: " + v_); }
    op=(Neg^ r)           { v_ = r^.v_; }
    op<--(mutable Neg^ r) { __println("Neg:op<--: " + r^.v_); v_ = r^.v_; }
    op+=(Neg^ r)          { v_ += r^.v_; }
    op-(Neg^ x)           { v_ = 0 - x^.v_; }
}

int take_neg(Neg^ n) { return n^.v_; }

// A unary in a RETURN rhs -- NRVO aliases the accumulator to the sret slot, so the CALLER's
// variable is the accumulator and no temporary exists at all.
Neg neg_ret(Neg^ x) {
    return -x^;
}

int32 main() {

    // B: a temp as the sole arg of a discarded call statement.
    //    the temp dies at the semicolon (before "B end").
    __println("B: bare call, one temp arg");
    fn(Class(10));
    __println("B end");

    // C: two temp args of a discarded call statement.
    //    both die at the semicolon, in reverse construction order.
    __println("C: bare call, two temp args");
    fn2(Class(1), Class(2));
    __println("C end");

    // E: a discarded method call on a temp receiver.
    //    the receiver temp dies at the semicolon.
    __println("E: method call on temp receiver");
    Class(3).inc();
    __println("E end");

    // F: a nested arg temp in a var-decl rhs whose VALUE is a scalar. The temp
    //    dies at the end of the DECLARATION (before "F end"), while the declared
    //    variable x keeps its own (scope) lifetime. This was the bug of bug.sl.
    __println("F: nested temp in a scalar var-decl rhs");
    int x = fn(Class(4));
    __println("F end x=" + x);

    // G: a construction as the direct rhs of a declaration builds in place --
    //    NO temp is created; cd lives to the end of the scope.
    __println("G: build into declared var (no temp)");
    Class cd = Class(9);
    __println("G end cd=" + cd.a_);

    // H: a temp in an if-condition dies at the condition-phrase exit,
    //    BEFORE the body runs.
    __println("H: temp in if-condition");
    if (fn(Class(20)) > 0) {
        __println("H body");
    }
    __println("H end");

    // I: a temp in a while-condition is rebuilt and destroyed on EACH
    //    evaluation of the condition (here: twice -- one true, one false).
    __println("I: temp in while-condition");
    int n = 0;
    while (fn(Class(30)) > 0 && n < 1) {
        __println("I body");
        n = n + 1;
    }
    __println("I end");

    // ---- coverage for the seq-wrap fix: a nested arg temp in a scalar-valued
    //      var-decl / assign / return rhs dies at the STATEMENT (value + timing). ----

    // 1: ASSIGN rhs (x already declared). The temp dies at the assignment
    //    statement (dtor before "1 end"); x holds the value.
    __println("1: assign rhs");
    {
        int x1 = 0;
        x1 = fn(Class(10));
        __println("1 end x1=" + x1);          // expect dtor 11 BEFORE this
    }

    // 2: RETURN rhs. The temp is destroyed BEFORE the function unwinds its named
    //    local -- so inside ret_rhs the order is: ctor 70(keep), ctor 71(temp),
    //    dtor 72(temp, at the return), dtor 70(keep, on unwind).
    __println("2: return rhs (ordering inside ret_rhs)");
    {
        int r = ret_rhs();
        __println("2 end r=" + r);
    }

    // 3a: a ++ in the VALUE child of the seq-wrapped rhs. The temp dies at the
    //     statement; the post-inc of a applies (a: 100 -> 101).
    __println("3a: PPID in the value child");
    {
        int a = 100;
        int x = fn(Class(30)) + a++;
        __println("3a end x=" + x + " a=" + a);   // 31 + 100 = 131, a=101
    }

    // 3b: a ++ INSIDE the temp's construction arg (the assert-relaxation path:
    //     lowerInPhrase recurses into the seq's temp-decl init). Class takes b's
    //     value (40), then b bumps to 41; the temp dies at the statement.
    __println("3b: PPID in the temp construction arg");
    {
        int b = 40;
        int x = fn(Class(b++));
        __println("3b end x=" + x + " b=" + b);   // temp 40->inc 41, b=41
    }

    // 4: two NON-chained temps in one decl rhs -- both die at the statement, in
    //    reverse construction order (2 then 1).
    __println("4: two temps in a decl rhs");
    {
        int x = fn2(Class(1), Class(2));
        __println("4 end x=" + x);                // reverse dtor 2,1 BEFORE this
    }

    // 5: a seq-wrapped decl in a LOOP body -- each iteration constructs and
    //    destroys its own temp at the statement (no leak / no stack growth).
    __println("5: seq-wrapped decl in a loop body");
    {
        int i = 0;
        while (i < 2) {
            int xl = fn(Class(50 + i));
            __println("5 body xl=" + xl);          // dtor (51/52) BEFORE this
            i = i + 1;
        }
        __println("5 end");
    }

    // 6: a POINTER-valued rhs (guard's kPointer arm) -- the class temp is still
    //    seq-wrapped and dies at the statement; q aliases slot.
    __println("6: pointer-valued rhs");
    {
        int slot = 5;
        int^ q = ptr_rhs(Class(60), ^slot);
        __println("6 end q=" + q^);                // dtor 61 BEFORE this
    }

    // 7: a CLASS-valued rhs (built IN PLACE) and its arg temp, which is STATEMENT-scoped
    //    like every other: dtor 81 runs BEFORE "7 end", and y (181) lives to the block
    //    end. Both halves at once is the whole point -- y is still constructed in place
    //    (two ctors, no extra copy), because codegen OPENS the temp seq (openRhsSeq) and
    //    hands the sret path the seq's VALUE, rather than desugar declining to wrap a
    //    class-valued rhs at all. That decline was the old behavior this case used to pin:
    //    the temp fell back to ENCLOSING-scope lifetime and died at the block end.
    __println("7: class-valued rhs (arg temp dies at the statement)");
    {
        Class y = make(Class(80));
        __println("7 end y=" + y.a_);              // dtor 81 ALREADY ran
    }                                              // block end: dtor 181 (y)

    // 8: augmented assignment on a scalar -- lowers to a plain assign before the
    //    lift pass, so its arg temp is seq-wrapped too and dies at the statement.
    __println("8: aug-assign rhs");
    {
        int acc = 0;
        acc += fn(Class(90));
        __println("8 end acc=" + acc);             // dtor 91 BEFORE this
    }

    // ---- J: BINARY-CHAIN TEMP BASELINE ----
    //
    // This pins how many temporaries a class binary chain costs TODAY. Evaluation is
    // bottom-up and precedence-correct: `a + b + c` is ((a+b)+c), so the inner `+` builds
    // a temp, the outer `+` builds another from it, and only then is the result bound to
    // the target. Dispatch is on the LHS OPERAND's class throughout; the target never
    // gets a say (that would invert precedence).
    //
    // The ctor/dtor lines below ARE the assertion -- each temp announces itself. When
    // Stage 3 proper lands (the temp-keyed fuse of canon 78-88, lowered in DESUGAR where
    // operand types exist: fuse into a temp when the lhs is already a fresh temp, then
    // elide-into-fresh / move-into-existing), these counts SHRINK -- and that shrinkage
    // is the test. Today's numbers are the "before".
    __println("J: binary-chain temp baseline");
    {
        Acc ja = Acc(1);
        Acc jb = Acc(2);
        Acc jc = Acc(3);

        __println("J1: depth-1 decl-init  jd = ja + jb");
        Acc jd = ja + jb;
        __println("J1 end jd=" + jd.v_);           // 3

        __println("J2: depth-2 chain      je = ja + jb + jc");
        Acc je = ja + jb + jc;
        __println("J2 end je=" + je.v_);           // 6

        __println("J3: existing target    jf = ja + jb + jc");
        Acc jf = Acc(0);
        jf = ja + jb + jc;
        __println("J3 end jf=" + jf.v_);           // 6

        __println("J end (locals dtor next)");
    }

    // ---- K: THE LADDER (class Str -- op= and op+=, no 2-arg op+) ----
    //
    // Canon 51-56. Every chain here costs ZERO temps except K4/K5, whose target is a
    // LIVE object: a live target can never BE the accumulator (making it one means
    // re-running its constructor), so those take one temp and MOVE it in -- and the
    // temp dies at the semicolon, not at the block end.
    __println("K: only-op+= ladder");
    {
        Str ka = Str(1);
        Str kb = Str(2);

        // K1: a FRESH construction at the head collapses INTO the accumulator, which
        //     is then seeded with op= (cheaper than op+= on an empty object) and fused
        //     with op+= for every later operand.  kc(); kc = ka; kc += kb;
        __println("K1: fresh head       Str kc = Str + ka + kb");
        Str kc = Str + ka + kb;
        __println("K1 end kc=" + kc.v_);           // 3

        // K2: a real OPERAND at the head, with no 2-arg op+ to take the pair: the head
        //     DECOMPOSES to the seed + one fuse.  kd(); kd = ka; kd += kb;
        __println("K2: operand head     Str kd = ka + kb");
        Str kd = ka + kb;
        __println("K2 end kd=" + kd.v_);           // 3

        // K3: a head constructed WITH ARGUMENTS -- op= is FORBIDDEN here (it would
        //     discard the ctor args), so the first operand fuses with op+= too.
        //     ke(10); ke += ka; ke += kb;
        __println("K3: ctor-arg head    Str ke = Str(10) + ka + kb");
        Str ke = Str(10) + ka + kb;
        __println("K3 end ke=" + ke.v_);           // 13

        // K4: a LIVE target -- one temp, moved in, destroyed at the semicolon (its
        //     dtor prints BEFORE "K4 end").
        __println("K4: live target      kf = ka + kb");
        Str kf = Str(0);
        kf = ka + kb;
        __println("K4 end kf=" + kf.v_);           // 3

        // K5: a LIVE target with a fresh head -- same one temp; the head construction
        //     collapses into the TEMP, never into kf.
        __println("K5: live, fresh head kf = Str + ka + kb");
        kf = Str + ka + kb;
        __println("K5 end kf=" + kf.v_);           // 3

        // K6: a chain in a RETURN rhs -- NRVO makes kg the accumulator: zero temps.
        __println("K6: return rhs       Str kg = chain_ret(ka, kb)");
        Str kg = chain_ret(^ka, ^kb);
        __println("K6 end kg=" + kg.v_);           // 3

        __println("K end (locals dtor next)");
    }

    // ---- L: THE TRIVIAL BUCKET (class Triv -- no ctor, no dtor) ----
    //
    // A whole-class transfer ALWAYS runs the class's transfer operator. Never a blit. Triv
    // has no hooks, so before the transfer gates were fixed every one of these went through
    // an inline load/store twin and the operators below never ran -- a chain's move into a
    // live target silently dropped the author's op<--. These prints are the proof they run.
    __println("L: trivial bucket (no ctor/dtor)");
    {
        Triv la = Triv(1);
        Triv lb = Triv(2);

        // L1: a fresh target -- the chain builds straight into it. The seed runs op=.
        __println("L1: fresh target  Triv lc = la + lb");
        Triv lc = la + lb;
        __println("L1 end lc=" + lc.v_);           // 3

        // L2: a LIVE target -- the temp is MOVED in, through op<--, not blitted.
        __println("L2: live target   ld = la + lb");
        Triv ld = Triv(0);
        ld = la + lb;
        __println("L2 end ld=" + ld.v_);           // 3

        // L3: a hand-written move + copy on a hook-less class -- the same operators.
        __println("L3: hand-written  ld <-- lc; le = ld");
        ld <-- lc;
        Triv le = Triv(0);
        le = ld;
        __println("L3 end le=" + le.v_);           // 3

        __println("L end");
    }

    // ---- M: THE REMAINING DESTINATIONS AND SHAPES ----
    //
    // Every place a chain can appear, and every shape it can take. Where there is fresh
    // storage of the chain's class, the chain builds into it (zero temps); everywhere else
    // it takes ONE accumulator that dies at the semicolon.
    __println("M: chain destinations and shapes");
    {
        Acc ma = Acc(10);
        Acc mb = Acc(3);
        Acc mc = Acc(4);

        // M1: a CALL ARGUMENT -- no destination at all, so the accumulator is a lifted
        //     temp; its dtor prints BEFORE "M1 end".
        __println("M1: call arg      take(ma + mb)");
        int m1 = take(ma + mb);
        __println("M1 end m1=" + m1);                // 13

        // M2: a store through a DEREF -- the receiver of the move is `mp`, not `^(mp^)`.
        __println("M2: deref store   mp^ = ma + mb");
        Acc md = Acc(0);
        Acc^ mp = ^md;
        mp^ = ma + mb;
        __println("M2 end md=" + md.v_);             // 13

        // M3: a store into a class FIELD.
        __println("M3: field store   mbox.a_ = ma + mb");
        Box mbox = Box(Acc(0));
        mbox.a_ = ma + mb;
        __println("M3 end mbox.a_=" + mbox.a_.v_);   // 13

        // M4: a store into an ARRAY ELEMENT.
        __println("M4: array store   marr[1] = ma + mb");
        Acc marr[2];
        marr[1] = ma + mb;
        __println("M4 end marr[1]=" + marr[1].v_);   // 13

        // M5: MIXED operators -- each node of the chain fuses with ITS OWN compound
        //     operator, so the `-` really subtracts. Zero temps.
        __println("M5: mixed ops     me = ma + mb - mc");
        Acc me = ma + mb - mc;
        __println("M5 end me=" + me.v_);             // 9

        // M6: a CONSTRUCTION as a NON-head operand. Only a HEAD construction collapses into
        //     the accumulator; anywhere else it is an ordinary operand, materialized into a
        //     temp that dies at the semicolon.
        __println("M6: ctor operand  mf = ma + Acc(7)");
        Acc mf = ma + Acc(7);
        __println("M6 end mf=" + mf.v_);             // 17

        // M7: a parenthesized SUB-CHAIN on the right. It is not part of the left spine, so
        //     it lowers into its own temp first, which dies at the semicolon.
        __println("M7: sub-chain     mg = ma + (mb + mc)");
        Acc mg = ma + (mb + mc);
        __println("M7 end mg=" + mg.v_);             // 17

        // M8: a chain in a LOOP BODY -- built and destroyed per iteration, no growth.
        __println("M8: loop body");
        int mi = 0;
        while (mi < 2) {
            Acc ml = ma + mb;
            __println("M8 body ml=" + ml.v_);        // 13
            mi = mi + 1;
        }
        __println("M8 end");

        __println("M end (locals dtor next)");
    }

    // ---- N: THE UN-FUSABLE CHAIN (class Buf -- a 2-arg op+, no op+=) ----
    //
    // With no compound operator there is nothing to fuse into, so each later operand starts
    // a fresh BUFFER. The buffers are the cost; their LIFETIME is the test. Every one of
    // them dies at the semicolon (its dtor prints before the "end" line), never at the
    // enclosing scope -- which is what a buffer emitted as a plain sibling of the
    // declaration would have done.
    __println("N: un-fusable chain (no op+=)");
    {
        Buf na = Buf(1);
        Buf nb = Buf(2);
        Buf nc = Buf(3);

        // N1: one operand pair -- the 2-arg op+ takes it, no buffer needed. Zero temps.
        __println("N1: no buffer     Buf nd = na + nb");
        Buf nd = na + nb;
        __println("N1 end nd=" + nd.v_);             // 3

        // N2: one un-fusable continuation -- ONE buffer, dead at the semicolon. The
        //     declared variable is still the final accumulator.
        __println("N2: one buffer    Buf ne = na + nb + nc");
        Buf ne = na + nb + nc;
        __println("N2 end ne=" + ne.v_);             // 6

        // N3: the same chain into a LIVE target -- the buffer AND the accumulator are both
        //     temps, both dead at the semicolon.
        __println("N3: live target   nf = na + nb + nc");
        Buf nf = Buf(0);
        nf = na + nb + nc;
        __println("N3 end nf=" + nf.v_);             // 6

        __println("N end (locals dtor next)");
    }

    // ---- O: HOOK-LESS TRANSFERS THROUGH ALL THREE GATES ----
    //
    // A whole-class transfer ALWAYS runs the class's operator -- copy, move AND swap --
    // whether the class has hooks or not, and whether it stands alone or sits inside an
    // aggregate (the transfer walks per leaf). Sw has no ctor/dtor, so these prints are the
    // only thing in the suite that would notice a gate falling back to a blit.
    __println("O: hook-less transfers");
    {
        Sw oa = Sw(1);
        Sw ob = Sw(2);

        __println("O1: swap          oa <--> ob");
        oa <--> ob;
        __println("O1 end oa=" + oa.v_ + " ob=" + ob.v_);      // 2 1

        __println("O2: tuple COPY    ou = ot");
        (Sw, Sw) ot = (Sw(7), Sw(8));
        (Sw, Sw) ou = (Sw(0), Sw(0));
        ou = ot;
        __println("O2 end ou=" + ou[0].v_ + "," + ou[1].v_);   // 7,8

        __println("O3: tuple MOVE    ov <-- ot");
        (Sw, Sw) ov = (Sw(0), Sw(0));
        ov <-- ot;
        __println("O3 end ov=" + ov[0].v_ + "," + ov[1].v_);   // 7,8

        // O4: an ARRAY of a hook-less class -- the per-leaf walk is form-agnostic.
        __println("O4: array COPY    obrr = oarr");
        Sw o7 = Sw(7);
        Sw o8 = Sw(8);
        Sw oarr[2];
        Sw obrr[2];
        oarr[0] = o7;
        oarr[1] = o8;
        obrr = oarr;
        __println("O4 end obrr=" + obrr[0].v_ + "," + obrr[1].v_);   // 7,8

        __println("O end");
    }

    // ---- P: DESTRUCTURE SLOTS ----
    //
    // A tuple LITERAL source is taken APART -- element i is assigned to slot i directly, with
    // no `_$dsrc` spill tuple in between. Each slot is then a chain with a LIVE destination:
    // one statement-scoped accumulator, MOVED in (Acc:op<--), dead at the semicolon. Two
    // objects for two chains. It used to spill the literal and copy each chain TWICE (once
    // into the spill slot, once into the target) -- four extra objects -- because the chains
    // sat inside the spill's tuple literal and so were nobody's direct source.
    //
    // The spill is not gone, just no longer paid for nothing: an RVALUE source (a call) still
    // spills, since it must be evaluated exactly once. So does a literal that reads a target
    // -- `(sa, sb) = (sb, sa)` is a SWAP, and per-slot stores would alias.
    //
    // Compare S4 (DECLARING slots): a fresh slot IS the accumulator, so it costs no temp and
    // no move at all -- the same "raw storage vs live object" question the declarator funnel
    // asks everywhere else.
    __println("P: destructure slots");
    {
        Acc pa = Acc(10);
        Acc pb = Acc(3);
        Acc pc = Acc(0);
        Acc pd = Acc(0);

        __println("P1: (pc, pd) = (pa + pb, pa + pa)");
        (pc, pd) = (pa + pb, pa + pa);
        __println("P1 end pc=" + pc.v_ + " pd=" + pd.v_);      // 13 20

        __println("P end (locals dtor next)");
    }

    // ---- Q: WHERE THE OPERATOR COMES FROM ----
    //
    // The chain lowering names an operator by the entry id classify resolved, so it does not
    // care where that operator was DECLARED -- an inherited one, a virtual one, or one used
    // from inside another method's body. These pin that.
    __println("Q: operator sources");
    {
        // Q1: an INHERITED operator. The operand is a DerOp; the operator's params are typed
        //     BaseOp^. A derived value binding to a base reference is an UPCAST (same
        //     address, nothing sliced) -- it used to be rejected, killing every base operator.
        __println("Q1: inherited      DerOp qd = qa + qb + qc");
        DerOp qa(1);
        DerOp qb(2);
        DerOp qc(4);
        DerOp qd = qa + qb + qc;
        __println("Q1 end qd=" + qd.get());              // 7

        // Q2: a VIRTUAL operator -- the fuse dispatches through the vtable, not statically.
        //     Vop has no 2-arg op+, so this is seed(op=) + two virtual op+= calls.
        __println("Q2: virtual        Vop vd = va + vb + vc");
        Vop va(1);
        Vop vb(2);
        Vop vc(4);
        Vop vd = va + vb + vc;
        __println("Q2 end vd=" + vd.get());              // 1 + (2+1000) + (4+1000) = 2007

        // Q3: a chain inside a METHOD body (a lifted method is a function like any other).
        __println("Q3: method body    qm.combine(qx, qy)");
        Acc qm = Acc(0);
        Acc qx = Acc(10);
        Acc qy = Acc(3);
        int q3 = qm.combine(^qx, ^qy);
        __println("Q3 end q3=" + q3);                    // 10 + 3 + 10 = 23

        // Q4: a chain inside a CONSTRUCTOR body and a DESTRUCTOR body.
        __println("Q4: ctor/dtor body");
        {
            Acc ha = Acc(1);
            Acc hb = Acc(2);
            Hook qh(0, ha, hb);
            __println("Q4 end qh.v_=" + qh.v_);          // 3   (dtor chain prints after)
        }

        __println("Q end (locals dtor next)");
    }

    // ---- R: THE OTHER OPERATOR KINDS, AND THE LAST DESTINATIONS ----
    //
    // classify stamps a class binary through ONE helper, shared by the arith, SHIFT and
    // LOGICAL arms -- but only + and - were exercised. And two destinations were untested:
    // a GLOBAL, and a target whose class is NOT the chain's result class.
    __println("R: other kinds and destinations");
    {
        // R1: a SHIFT chain -- fuses with op<<= like any other compound operator.
        __println("R1: shift chain    Shf rs = ra << rb << rc");
        Shf ra(1);
        Shf rb(2);
        Shf rc(1);
        Shf rs = ra << rb << rc;
        __println("R1 end rs=" + rs.v_);                 // (1<<2)<<1 = 8

        // R2: a LOGICAL binary on a class -- a produce-self op, no short-circuit.
        __println("R2: logical        Shf rl = ra && rb");
        Shf rl = ra && rb;
        __println("R2 end rl=" + rl.v_);                 // 1 & 2 = 0

        // R3: a GLOBAL destination -- a LIVE target, so one temp, moved in (op<-- prints),
        //     dead at the semicolon.
        __println("R3: global target  gacc = ra2 + rb2");
        Acc ra2 = Acc(10);
        Acc rb2 = Acc(3);
        gacc = ra2 + rb2;
        __println("R3 end gacc=" + gacc.v_);             // 13

        // R4: the destination's class is NOT the chain's result class. The chain yields an
        //     Acc; Oth has an op=(Acc^), so the ordinary binding funnel converts. The chain
        //     lowering does not elide here -- the exact-type check is the only guard.
        __println("R4: cross-class    Oth ro = ra2 + rb2");
        Oth ro = ra2 + rb2;
        __println("R4 end ro.w_=" + ro.w_);              // 13 + 500 = 513

        // R5: a chain in a SWITCH case body and a DO-WHILE body -- the structural walkers.
        __println("R5: switch + do-while bodies");
        int rk = 1;
        switch (rk) {
            1: {
                Acc rsw = ra2 + rb2;
                __println("R5 switch rsw=" + rsw.v_);    // 13
            }
            default: { }
        }
        int ri = 0;
        while {
            Acc rdw = ra2 + rb2;
            __println("R5 do-while rdw=" + rdw.v_);      // 13, twice
            ri = ri + 1;
        } (ri < 2);
        __println("R5 end");

        __println("R end (locals dtor next)");
    }

    // ---- S: THE LAST CHAIN POSITIONS ----
    __println("S: remaining chain positions");
    {
        Acc sa = Acc(10);
        Acc sb = Acc(3);

        // S1/S2: the two remaining loop forms (switch + do-while are in R5).
        __println("S1: for-long body");
        for (int si = 0) (si < 2) {si = si + 1;} {
            Acc sf = sa + sb;
            __println("S1 body sf=" + sf.v_);        // 13, twice
        }
        __println("S2: for-ranged body");
        for (int sj : 0 .. 2) {
            Acc sg = sa + sb;
            __println("S2 body sg=" + sg.v_);        // 13, twice
        }

        // S3: a chain into a HEAP object, and a chain OPERAND that is a heap deref.
        __println("S3: heap target and heap operand");
        Acc^ sh = new Acc(0);
        sh^ = sa + sb;                                // a live target through a deref
        Acc si2 = sh^ + sb;                           // an operand that is a deref
        __println("S3 end sh^=" + sh^.v_ + " si2=" + si2.v_);   // 13, 16
        delete sh;

        // S4: DESTRUCTURE with DECLARING (fresh) class slots -- block P covers EXISTING ones.
        __println("S4: destructure, declaring slots");
        (Acc sp, Acc sq) = (sa + sb, sa + sa);
        __println("S4 end sp=" + sp.v_ + " sq=" + sq.v_);       // 13, 20

        __println("S end (locals dtor next)");
    }

    // ---- T: A DERIVED VALUE BINDING TO A BASE REFERENCE ----
    //
    // The blast radius of the argConvertCost / checkArgAssign / emitCall change that made
    // inherited operators reachable (Q1). Both of these fail SILENTLY if they regress -- the
    // wrong overload, or a static call where a virtual one was meant -- so they are pinned
    // here even though neither is about evaluating an expression.
    __println("T: derived value -> base reference");
    {
        Der td(7);

        // T1: an EXACT derived param still WINS. The upcast is rung 2; exact is rung 0.
        __println("T1: exact beats upcast");
        __println("T1 end pick(td)=" + pick(td));              // 200, NOT 100

        // T2: a base-only overload IS reachable from a bare derived value (no `^` needed).
        __println("T2: base-only reachable");
        __println("T2 end base_only(td)=" + base_only(td));    // 307

        // T3: DYNAMIC DISPATCH survives the upcast -- the address is identical, vptr and all.
        __println("T3: virtual dispatch through the upcast");
        VDer tv(0);
        __println("T3 end ask(tv)=" + ask(tv));                // 2, the override -- NOT 1

        // T4: a hook-less class with a POINTER field -- the move runs the operator and leaves
        //     the source a husk.
        __println("T4: pointer-field move");
        int tslot = 42;
        Ptr tp(^tslot);
        Ptr tq(nullptr);
        tq <-- tp;
        __println("T4 end tq=" + tq.p_^);                      // 42

        __println("T end");
    }

    // ---- U: THE ARITY-1 UNARY ----
    //
    // A unary PRODUCES a class value, so it is a chain node like a binary: the destination is
    // the accumulator wherever it is raw storage (zero temps), and a LIVE target takes exactly
    // one temp, moved in through the class's op<--, dead at the semicolon. A unary at a chain's
    // HEAD collapses into the accumulator exactly as a head construction does. Dispatch is on
    // the OPERAND's class alone -- the old path let the assignment TARGET's type pick the
    // operator, the same precedence inversion that killed the target-keyed binary fuse.
    __println("U: arity-1 unary");
    {
        Neg ua = Neg(5);
        Neg ub = Neg(2);

        // U1: a fresh decl -- uc IS the accumulator. ZERO temps: one ctor, and it is uc's.
        //     This used to cost a second object and a COPY into uc, whose dtor then ran at
        //     the block end rather than here.
        __println("U1: fresh decl     Neg uc = -ua");
        Neg uc = -ua;
        __println("U1 end uc=" + uc.v_);              // -5

        // U2: a LIVE target -- one temp, MOVED in through op<-- (which prints), dead at the
        //     semicolon. The old path COPIED, so the author's op<-- never ran.
        __println("U2: live target    ud = -ua");
        Neg ud = Neg(0);
        ud = -ua;
        __println("U2 end ud=" + ud.v_);              // -5

        // U3: the unary at a chain HEAD -- it writes the accumulator, then `+ ub` fuses onto
        //     it with op+=. Zero temps.
        __println("U3: chain head     Neg ue = -ua + ub");
        Neg ue = -ua + ub;
        __println("U3 end ue=" + ue.v_);              // -3

        // U4: the unary as a NON-head operand -- an ordinary operand, so it materializes into
        //     its own accumulator, dead at the semicolon.
        __println("U4: chain operand  Neg uf = ua + -ub");
        Neg uf = ua + -ub;
        __println("U4 end uf=" + uf.v_);              // 3

        // U5: a CALL ARG -- no destination at all, so a lifted temp, dead at the semicolon.
        __println("U5: call arg       take_neg(-ua)");
        int u5 = take_neg(-ua);
        __println("U5 end u5=" + u5);                 // -5

        // U6: a RETURN rhs -- NRVO makes ug the accumulator: zero temps.
        __println("U6: return rhs     Neg ug = neg_ret(ua)");
        Neg ug = neg_ret(^ua);
        __println("U6 end ug=" + ug.v_);              // -5

        // U7: a unary over a parenthesized SUB-CHAIN. The spine STOPS at the unary, so the
        //     inner chain is an ordinary nested rvalue -- one temp for it, dead at the
        //     semicolon -- while the unary itself still collapses into uh. Walking the spine
        //     through the unary instead would apply it to the accumulator: `acc.op-(acc)`,
        //     reading the accumulator while writing it (the un-fusable rule's self-alias).
        __println("U7: sub-chain      Neg uh = -(ua + ub)");
        Neg uh = -(ua + ub);
        __println("U7 end uh=" + uh.v_);              // -7

        __println("U end (locals dtor next)");
    }

    // ---- V: A TUPLE OF CLASSES ----
    //
    // Every object here PRINTS, so the counts are the assertion.
    __println("V: tuple of classes");
    {
        // V1: a nested tuple built from a LITERAL of constructions costs exactly ONE ctor
        // per object -- each element is constructed IN ITS SLOT. It used to build every
        // element as a temp and copy it into the slot: six ctors for three objects.
        __println("V1: ((Trk,Trk),Trk) vt = ((Trk(1), Trk(2)), Trk(3))");
        ((Trk, Trk), Trk) vt = ((Trk(1), Trk(2)), Trk(3));
        __println("V1 end");

        // V2: a literal of class LVALUES dispatches op= per element, INTO the slot. A
        // whole-value build would have loaded each element and blitted it in, silently
        // skipping the operator -- the transfer invariant. Each slot is CONSTRUCTED first
        // (its ctor prints 0, the field default) and only then copied into -- block W.
        Trk v1 = Trk(1);
        Trk v2 = Trk(2);
        __println("V2: (Trk,Trk) vl = (v1, v2)");
        (Trk, Trk) vl = (v1, v2);
        __println("V2 end vl=" + vl[0].v_ + "," + vl[1].v_);        // 1,2

        // V3: a NESTED destructure, COPY. The source is an lvalue, so it is INDEXED per slot
        // -- `vt[0][0]` -- and the nested slot recurses on that. No sub-tuple temp: three
        // slots, three op= calls. It used to spill an intermediate (Trk,Trk) temp per level.
        __println("V3: ((Trk va, Trk vb), Trk vc) = vt");
        {
            ((Trk va, Trk vb), Trk vc) = vt;
            __println("V3 end " + va.v_ + " " + vb.v_ + " " + vc.v_);   // 1 2 3
        }

        // V4: a NESTED destructure, MOVE. Same indexing, so `<--` reaches every depth -- it
        // was rejected outright ("a nested move/swap destructure is not yet supported")
        // because the source had to be a bare NAME, and `vt[0]` is not one. Each slot's
        // op<-- nulls the REAL leaf: the source is left a husk of zeros.
        __println("V4: ((Trk vd, Trk ve), Trk vf) <-- vt");
        {
            ((Trk vd, Trk ve), Trk vf) <-- vt;
            __println("V4 end " + vd.v_ + " " + ve.v_ + " " + vf.v_);   // 1 2 3
        }
        __println("V4 src vt=" + vt[0][0].v_ + " " + vt[0][1].v_ + " " + vt[1].v_);  // 0 0 0

        // V5: a NESTED destructure, SWAP -- exchanges with the real leaves, so the source
        // ends up holding what the slots had (the slots are fresh, hence zeros).
        ((Trk, Trk), Trk) vs = ((Trk(4), Trk(5)), Trk(6));
        __println("V5: ((Trk vg, Trk vh), Trk vi) <--> vs");
        {
            ((Trk vg, Trk vh), Trk vi) <--> vs;
            __println("V5 end " + vg.v_ + " " + vh.v_ + " " + vi.v_);   // 4 5 6
        }
        __println("V5 src vs=" + vs[0][0].v_ + " " + vs[0][1].v_ + " " + vs[1].v_);  // 0 0 0

        // V6: the same literal of constructions, but into a LIVE tuple. Its slots are
        // already objects, so they cannot be BUILT over (V1's in-place path) -- each
        // element is constructed as a statement-scoped temp and TRANSFERRED in through
        // op=, and the temps die at the semicolon. Only raw storage (a decl, a return
        // slot, a field of something being constructed) takes the build-in-place path.
        __println("V6: vs = ((Trk(7), Trk(8)), Trk(9))   [live target]");
        vs = ((Trk(7), Trk(8)), Trk(9));
        __println("V6 end vs=" + vs[0][0].v_ + " " + vs[0][1].v_ + " " + vs[1].v_);  // 7 8 9

        __println("V end (locals dtor next)");
    }

    // ---- W: THE ORDER OF CONSTRUCTION ----
    //
    // A class can only be COPIED INTO, so it has to EXIST first: alloc, init, ctor, THEN the
    // transfer. Every binding here copies from an LVALUE, and Ord's ctor WRITES its own field
    // (v_ = 99) -- so if the ctor ran after the copy, as it used to, each of these would read
    // back 99 instead of the value it was copied from. Every one is a wrong ANSWER, not an
    // ordering nicety, and none of the printing counters above could see it.
    __println("W: order of construction");
    {
        Ord wa = Ord(1);  wa.v_ = 7;
        Ord wb = Ord(1);  wb.v_ = 8;

        // W1: the scalar copy-init and move-init.
        Ord w1 = wa;
        Ord w2 <-- wb;
        __println("W1: copy-init w1=" + w1.get() + " move-init w2=" + w2.get());   // 7 8

        // W2: a tuple LITERAL of class lvalues -- per SLOT, each slot constructed then
        // copied into. (Block V2 counts the operators; this reads the values back.)
        Ord wc = Ord(1);  wc.v_ = 5;
        (Ord, Ord) w3 = (wa, wc);
        __println("W2: tuple literal w3=" + w3[0].get() + " " + w3[1].get());      // 7 5

        // W3: the WHOLE-value copy of a class-bearing tuple -- one transfer, after the
        // whole aggregate is constructed.
        (Ord, Ord) w4 = w3;
        __println("W3: tuple copy w4=" + w4[0].get() + " " + w4[1].get());         // 7 5

        // W4: an ARRAY of classes from a literal, and a NESTED literal. Same rule at every
        // layer -- the slot exists before anything is copied into it.
        Ord w5[2] = (wa, wc);
        ((Ord, Ord), Ord) w6 = ((wa, wc), wa);
        __println("W4: array w5=" + w5[0].get() + " " + w5[1].get()
                  + " nested w6=" + w6[0][0].get() + " " + w6[0][1].get() + " " + w6[1].get());
        // 7 5 / 7 5 7

        // W5: a MIXED literal -- slot 0 is a CONSTRUCTION and still BUILDS IN PLACE (its ctor
        // writes 99 and nothing copies over it); slot 1 is a copy. The decision is per slot.
        (Ord, Ord) w7 = (Ord(1), wa);
        __println("W5: mixed w7=" + w7[0].get() + " " + w7[1].get());              // 99 7

        // W6: the BUILD-IN-PLACE forms are untouched -- an rvalue source has no object to
        // copy FROM, so it elides and the ctor's own write stands.
        Ord w8 = Ord(3);
        __println("W6: build-in-place w8=" + w8.get());                            // 99

        __println("W end (locals dtor next)");
    }

    // ---- X: EVERY SPILL DIES AT THE SEMICOLON ----
    //
    // A SPILL materializes a source that must be evaluated ONCE but is read MORE than once
    // (indexed per slot, spread per field). It is a TEMPORARY, so it dies at the SEMICOLON --
    // and each of these used to live to the END OF THE BLOCK instead, because classify pushed
    // its declaration into the prelude, where it is just another local of the enclosing scope.
    // Nobody chose that: the lifetime was a side effect of where the decl got parked, and four
    // sites hand-rolled the same twenty lines and answered it differently. They now share one
    // funnel (spillToTemp), which makes the placement -- and so the lifetime -- the one thing
    // the caller has to decide.
    //
    // Each case prints the temp's dtor BEFORE its marker line. A dtor printing after the
    // marker means the temp outlived its statement.
    __println("X: spills die at the semicolon");
    {
        // X1: a CLASS from an rvalue AGGREGATE -- the source tuple is spilled and spread
        // across the fields. SEQ placement: the temp is read by this decl's rhs alone.
        __println("X1: Trkpair xp = mkTrkTup();");
        Trkpair xp = mkTrkTup();
        __println("X1 end (the (Trk,Trk) temp is already dead)");

        // X2: a DESTRUCTURE from an rvalue source -- spilled, then indexed per slot. GROUP
        // placement: the temp is read by several sibling statements, so it and they go in a
        // block, and the DECLARING slots are hoisted out of it (their names outlive it).
        __println("X2: (Trk xa, Trk xb) = mkTrkTup();");
        {
            (Trk xa, Trk xb) = mkTrkTup();
            __println("X2 end xa=" + xa.v_ + " xb=" + xb.v_);   // 1 2
        }

        // X3: a CONSTRUCTION element bound to a LIVE destructure slot -- it cannot be built
        // in the slot (the slot is already an object), so it is materialized and copied in.
        Trk xc = Trk(8);
        Trk xd = Trk(9);
        __println("X3: (xc, xd) = (Trk(80), Trk(90));");
        (xc, xd) = (Trk(80), Trk(90));
        __println("X3 end xc=" + xc.v_ + " xd=" + xd.v_);       // 80 90

        __println("X end (locals dtor next)");
    }

    // ---- Y: THE SOURCE MODEL ----
    //
    // A destructure evaluates its source EXACTLY ONCE and then reads it apart: a tuple LITERAL
    // is taken apart element by element, a re-readable LVALUE is indexed per slot, and anything
    // else -- a call, a chain, a nested rvalue -- SPILLS to a temp that is indexed instead.
    // Block X pins where the spill DIES; this block pins that the right thing was read, once.
    // Val is silent, so these are VALUES, not a print order.
    __println("Y: the source model");
    {
        // Y1: a NESTED destructure from an rvalue. The spill goes in a block, but the nested
        // slots' DECLARATIONS have to escape it -- they outlive it. When they did not, the
        // inner stores were hoisted out ahead of the spill and read an unwritten source.
        ((Val y1a, Val y1b), Val y1c) = mkValNest();
        __println("Y1: nested=" + y1a.get() + " " + y1b.get() + " " + y1c.get());  // 1 2 3

        // Y2: the source is evaluated ONCE, however many slots read it.
        (Val y2a, Val y2b) = valCounted();
        __println("Y2: once calls=" + y_calls + " " + y2a.get() + " " + y2b.get());// 1 4 5

        // Y3: a spilling destructure in a LOOP body. The temp is rebuilt and destroyed on each
        // iteration -- the source is evaluated once PER ITERATION, not once per statement --
        // and Trk's prints show the pairs balance. (Trk here, not Val: this one IS a lifetime.)
        for (i : 0..2) {
            __println("Y3: iter " + i);
            (Trk y3a, Trk y3b) = mkTrkTup();
            __println("Y3: iter " + i + " = " + y3a.v_ + " " + y3b.v_);            // 1 2
        }

        // Y4: a DISCARD slot drops its element's VALUE, not its EFFECTS -- the element is still
        // evaluated (and its temp still destroyed).
        (Val y4, ) = (valBump(1), valBump(10));
        __println("Y4: discard effects=" + y_effects + " y4=" + y4.get());         // 11 1

        // Y5: an ARRAY copy-init from an array LVALUE -- the whole aggregate, element by
        // element, each element constructed before it is copied into.
        Val y5src[2] = (Val(1), Val(2));
        y5src[0].v_ = 5;  y5src[1].v_ = 6;
        Val y5dst[2] = y5src;
        __println("Y5: array=" + y5dst[0].get() + " " + y5dst[1].get());           // 5 6

        // Y6: a copy-init inside a NESTED function.
        yNestedHost();

        __println("Y end (locals dtor next)");
    }

    // ---- Z: AN OPERATION ON AN AGGREGATE OF CLASSES ----
    //
    // A TUPLE DESUGARS TO THE OPERATION BY SLOT, ITERATIVELY AND RECURSIVELY -- and an ARRAY
    // is a homogeneous tuple, so this is every aggregate. `(a, b) + (c, d)` becomes the tuple
    // literal `(a + c, b + d)` in classify, and each element is then an ORDINARY class binary:
    // it dispatches the class's operator, joins the chain machinery, and costs what any other
    // chain costs. Nothing about aggregates survives past classify.
    //
    // NONE of this used to work, and it did not FAIL either -- it emitted `add { i32 } %a, %b`
    // and exited 0. The aggregate walkers lived in CODEGEN and worked in the VALUE domain
    // (extractvalue / insertvalue on SSA registers), where a slot has NO ADDRESS -- and a class
    // operation needs one, for its `^self` receiver and its sret destination. So they could
    // only ever emit a numeric instruction, and widen::commonType SUCCEEDS for two identical
    // class types, so nothing caught it. Both walkers are deleted; codegen now asserts an
    // aggregate operand never reaches it.
    __println("Z: aggregates of classes");
    {
        (Acc, Acc) za = (Acc(1), Acc(2));
        (Acc, Acc) zb = (Acc(10), Acc(20));

        // Z1: tuple + tuple. Acc prints, so the objects are countable: each slot is a chain
        // with its own destination, and its accumulator dies at the semicolon.
        __println("Z1: (Acc,Acc) zc = za + zb;");
        (Acc, Acc) zc = za + zb;
        __println("Z1 end zc=" + zc[0].v_ + " " + zc[1].v_);                    // 11 22

        // Z2: ARRAY + ARRAY -- an array IS a homogeneous tuple, so it is the same road, and
        // it COSTS THE SAME as Z1 (the explode re-forms its result as the ARRAY, so the decl
        // is not a cross-form copy: it used to spill the whole aggregate to a temp -- a ctor
        // and a dtor per slot -- and copy the zero defaults in, only to overwrite them).
        Acc zd[2] = (Acc(1), Acc(2));
        Acc ze[2] = (Acc(10), Acc(20));
        __println("Z2: Acc zf[2] = zd + ze;");
        Acc zf[2] = zd + ze;
        __println("Z2 end zf=" + zf[0].v_ + " " + zf[1].v_);                    // 11 22

        // Z3: the AUG-ASSIGN explodes into per-slot STATEMENTS, not one exploded expression --
        // the operator the author wrote is the COMPOUND one, so each slot must reach its own
        // `op+=`. Rebuilding it out of `op+` would be a different program (and impossible for
        // a class that has only `op+=`). A class `+=` with no matching operator was the
        // ORIGINAL struct-add hole; the aggregate form was that hole one level up.
        __println("Z3: zd += ze;");
        zd += ze;
        __println("Z3 end zd=" + zd[0].v_ + " " + zd[1].v_);                    // 11 22

        // Z4: a SCALAR class operand BROADCASTS into every slot.
        Acc zs = Acc(100);
        __println("Z4: (Acc,Acc) zg = za + zs;");
        (Acc, Acc) zg = za + zs;
        __println("Z4 end zg=" + zg[0].v_ + " " + zg[1].v_);                    // 101 102

        // Z5: NESTED -- a nested aggregate slot re-enters the rewrite when its own element is
        // classified, so the recursion costs nothing and needs no code of its own.
        ((Acc,Acc),Acc) zn = ((Acc(1),Acc(2)),Acc(3));
        __println("Z5: ((Acc,Acc),Acc) zo = zn + zn;");
        ((Acc,Acc),Acc) zo = zn + zn;
        __println("Z5 end zo=" + zo[0][0].v_ + " " + zo[0][1].v_ + " " + zo[1].v_);  // 2 4 6

        // Z6: the SOURCE IS EVALUATED ONCE. Every slot reads the operand, so one that cannot
        // be re-read (a call) SPILLS to a temp that is indexed instead -- the same spill
        // funnel the destructure uses. The (Acc,Acc) temp dies at the SEMICOLON.
        __println("Z6: (Acc,Acc) zp = mkAccPair() + za;");
        (Acc, Acc) zp = mkAccPair() + za;
        __println("Z6 end zp=" + zp[0].v_ + " " + zp[1].v_);                    // 2 4

        __println("Z end (locals dtor next)");
    }

    // ---- Z7: THE ORDER OF CONSTRUCTION, IN A SLOT ----
    //
    // The slot-wise explode created a NEW way to reach the fill-then-construct bug (block W):
    // a class CHAIN landing in a tuple slot. Desugar's tuple-literal distribution can hand a
    // slot to a nested literal or to a construction, but NOT to a chain -- a chain's
    // accumulator home is answered per STATEMENT, and a slot of a literal is not one. So the
    // chain lifted to a temp, the tuple was formed from the temps, and the whole VALUE filled
    // the storage -- after which each slot's ctor ran ON TOP of the result. Ord's ctor WRITES
    // its own field, so this read back 99, not the sum: a wrong ANSWER, which no printing class
    // could show. splitTransferInit now peels a chain in a SLOT exactly as it peels an lvalue:
    // the slot is constructed, then the chain is assigned into it. At the ROOT it must NOT be
    // peeled -- a decl whose whole rhs is a chain IS the accumulator (zero temps), and that was
    // always right, which is what z8 pins.
    {
        Zord z1(0);  z1.v_ = 1;
        Zord z2(0);  z2.v_ = 2;
        (Zord, Zord) zt = (z1, z2);
        (Zord, Zord) zu = (z1, z2);

        (Zord, Zord) zv = zt + zu;
        __println("Z7: aggregate chain zv=" + zv[0].get() + " " + zv[1].get());     // 2 4

        Zord z8 = z1 + z2;
        __println("Z7: scalar chain z8=" + z8.get());                               // 3
    }

    return 0;
}

/* A compound operator on a class that has NO matching one is REJECTED. It used to fall
   through to the numeric path, where commonType SUCCEEDS for two identical class types --
   emitting a struct `add`, which is INVALID IR. The kBinaryExpr arm has always had this
   guard; the aug-assign arm never did. */
//-EXPECT-ERROR: Operator '+=' is not defined on class 'Bare'.
//Bare(int v_) { }
//int neg_aug_no_op() {
//    Bare a(1);
//    Bare b(2);
//    a += b;
//    return a.v_;
//}
