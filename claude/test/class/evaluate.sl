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
iteration, no growth), 6 a pointer-valued rhs (the guard's kPointer arm), 7 the
guard-negative boundary (a CLASS-valued rhs leaks its temp -- pinned as the
current deferred behavior), 8 aug-assign (lowers to assign, so covered too).

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

deliberately NOT correct yet (still-broken / deferred; case 7 pins the first):
  - a var-decl / return rhs whose VALUE is a CLASS built in place
    (Class y = make(Class(80));) still leaks its arg temp -- the seq wrap is
    scalar-only (a class value takes the in-place sret path).
  - a bare construction discard (Class(5);) escapes to enclosing scope.
  - ping-pong: a class with a 2-arg op+ but NO op+= can't fuse (acc.op+(acc,c) would
    read acc while writing it), so each such step starts a fresh buffer -- one temp per
    un-fusable operand. Two alternating buffers would need none.
  - the move into a live target is the DEFAULT move; a user op<-- is not dispatched.
  - an arity-1 unary (Class r = -a) still takes the old construct-temp-then-op path.
  - tuple-construct assignment (((a,b),c) = (...)) -- value-correct but
    spews redundant temps; not clean.
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

// A GLOBAL as a chain destination (a live target -- one temp, moved in).
global Acc gacc = Acc(100);

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

// A chain in a RETURN rhs: the accumulator is the returned local, which NRVO aliases
// to the sret slot -- so the caller's variable IS the accumulator and no temp exists.
Str chain_ret(Str^ x, Str^ y) {
    return Str + x^ + y^;
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

    // 7: guard-negative BOUNDARY -- a CLASS-valued rhs (built in place) does NOT
    //    seq-wrap its arg temp, so the temp keeps ENCLOSING-scope lifetime: its
    //    dtor (81) runs at the BLOCK end, AFTER "7 end", together with y (181, in
    //    reverse). Pins the current deferred behavior; a future class-rhs fix will
    //    move dtor 81 up to the statement.
    __println("7: class-valued rhs (temp leaks to enclosing scope)");
    {
        Class y = make(Class(80));
        __println("7 end y=" + y.a_);              // dtor 81 does NOT run yet
    }                                              // block end: dtor 181, dtor 81

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
    // Values are right; the SHAPE is pinned as-is, and it is NOT minimal. What actually
    // happens: a destructure whose source is not a bare name is SPILLED once into a `_$dsrc`
    // tuple temp (classify, so a side-effecting source is not re-evaluated per slot), and
    // each slot is then a plain `pc = _$dsrc[0]` copy through op=. So the chains are NOT the
    // direct source of any assignment -- they sit inside the spill's tuple literal, take the
    // no-destination path (one accumulator each), and are copied twice: once into the spill
    // slot, once into the target. Four extra objects for two chains, where two would do.
    //
    // Both costs are pre-existing and out of this landing's scope: for a tuple LITERAL the
    // spill is pure overhead (each element is read exactly once), and the accumulators keep
    // ENCLOSING-scope lifetime for the same reason case 7 does -- the statement-scoping seq
    // wrap is scalar-only, and the spill's value is a class. A future fix assigns a literal
    // source per slot without spilling, which would make each slot a live-target chain (one
    // accumulator, moved in, dead at the semicolon) and land Acc:op<-- here.
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
