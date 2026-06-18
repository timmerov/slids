/*
test pre/post-increment/decrement.
aka ppid.

expressions are composed of phrases.
pre/post-increment/decrement operations are extracted from their phrase.
pre-increment/decrement is evaluated before the phrase.
post-increment/decrement is evaluated after the phrase.

what is a phrase?

an expression statement is a phrase.
expressions statements may have sub-phrases.

every comma separated slot of a tuple clause is a separate phrase.
phrases are evaluated left to right.
this includes:
parameter to functions and methods,
for initialization tuple clause slot,
tuple literal slot,
tuple destructure slot,
if/while/for condition.

the rhs of && and || operations is a sub-phrase.
this ensures a short-circuited ppid is not evaluated.

Canonical examples:
x = p++^;             // p incremented AFTER x is assigned
x = (++p)^ + (++p)^;  // p incremented twice BEFORE the assignment
x = (cond && b++);    // b incremented iff cond is true
x = foo(a++, ++a);    // foo(1, 3) — args are separate phrases

desugaring does not duplicate ppid evaluation.
side effects are extracted from the lvalue.
p++^ += 1;  -->  p^ = p^ + 1; p++;
++(arr[k++]) = 5;  -->  ++arr[k]; arr[k] = 5; k++;
row[++k] += (10,20)  -->  ++k; row[k][0] += 10; row[k][1] += 20;
*/

/*
claude says:

PPID model: a ++/-- is extracted from its PHRASE — pre bumps at phrase ENTRY,
post at phrase EXIT — and the lowering never duplicates the side effect. The
STATEMENT is the phrase, so `s = s++` stores 5 then bumps to 6, and
`t = t++ + t++` stores 10 then bumps twice to 12. desugar does the work: a use
becomes a kSeqExpr (yielding the pre-bumped / pre-read value) + a kBumpExpr, and a
statement's post-bumps hoist to sibling kExprStmts after the phrase. A SUB-PHRASE
(the rhs of && / ||, each comma slot of a call / tuple / condition) keeps its own
seq, so a short-circuited or per-slot bump fires only when that phrase runs.

covered here:
  - post (a++) vs pre (++b); both-pre in one expr ((++c)+(++c) -> reads see 3);
    both-post ((d++)+(d++) -> reads see 1, bumps after).
  - the statement-as-phrase self cases: s=s++ -> 6, t=t++ + t++ -> 12.
  - bare inc/dec statement (e--); float scalar step (f++ -> 2.0).
  - function-arg phrases: show2(g++, ++g) evaluates each arg as its own phrase,
    left to right (show 1 3).
  - && / || short-circuit: rhs REACHED bumps (ct && h++ -> h=1; of || k++), rhs
    SKIPPED does not (cf && i++ -> unchanged; ot || j++) — the sub-phrase rule.
  - if / while CONDITION as a phrase: the post-bump fires after the condition
    (if (m++ > 0) -> m=1; while (n++ < 3) bumps each pass -> n=4).
  - tuple-literal slots are SEPARATE phrases, left to right (like call args):
    (p++, ++p) -> (1,3); same var repeated ((d2++, d2++) -> (1,2), (++c2, ++c2) ->
    (2,3), three post -> (1,2,3)); different vars + mixed inc/dec + pre/post.
  - destructure rhs slots are separate phrases too: (a,b) = (dp++, ++dp) -> 5,7;
    different vars (ea++, eb--).
  - multiple ppid in ONE phrase: all pres fire at phrase entry, all posts at exit,
    every read sees the between value — ++sa + sa++ -> 4 (sa=3); 2-pre+2-post same
    var -> 12 (sm=5); different vars mixed (va++ + ++vb + vc--) -> 112.
  - a for-RANGE bound is a phrase: the bump fires once at loop setup
    (for (ii : 0..q++) -> q=3, two iterations). lowerForLong lowers the ranged-for
    bound/step as varlist-init phrases.
  - a RETURN value is a phrase: post returns the OLD value (retPost(5) -> 5), pre
    the new (retPre(5) -> 6).
  - a do-while CONDITION (slids `while { body } (cond);`) bumps each pass
    (dw++ < 2 -> dw=3 after 3 passes); a switch SCRUTINEE bumps once (switch (sw++)
    -> sw=2). Method-call args reduce to the call-arg case — a kMethodCallStmt is
    lowered to a kCallStmt before the PPID pass — so they need no separate test.
  - prefix ppid in a short-circuited && rhs is skipped too (pf && ++pi -> pi=0).
  - store / move statements are phrases: a store rhs (stp^ = stx++) and an index
    bump (ar[kk++] = 99 -> store ar[1], then kk++) and a move rhs (mvb <-- mva++)
    all lift the bump after the statement. (Swap-OPERAND ppid x++ <--> y++ is a
    PARSER block — parseIncDecStmt commits to a bare x++; — so the kSwapStmt arm is
    defensive / unreachable today; see the deferred x++-as-lvalue item.)
  - alias transparency: an inc on an alias-typed var (Counter=int32) stays a binary
    OPERAND whose own type drives the common-type rule (no spelling round-trip that
    would clobber 'Counter' to an unknown slid).
  - COMPLEX-LVALUE operands: ++/-- on an array element, a deref, a class field
    (`pt.v_` lowers to the index path), statement + expression, pre + post, inc +
    dec. A hidden `_$lv` reference binds the leaf ADDRESS once (a side-effecting
    index runs a single time, 1D and multi-dim), and the scalar read/bump split is
    kept so post defers to the phrase exit (oo[0]++ + oo[0] -> old+old; two DISTINCT
    elems tw[0]++ + tw[1]++). Element TYPES: int, float (steps by 1), iterator
    (steps by one ELEMENT via GEP). Sub-phrase forms work too: a call arg
    (show2(sp[0]++, ++sp[1])) and a short-circuited && rhs (false && sc[0]++ skips,
    true && sc[1]++ bumps). One PPID path — both grammar shortcuts (the postfix
    `arr[i]++;` aug-assign rewrite and the bare-ident-only prefix) are gone.

negatives: ++ on a const, on bool (operator undefined), on a non-lvalue (5++), on
a function name (main++). Distinct from the supported complex OPERAND above: a ppid
RESULT used as an assignment TARGET (cp++^ = 7 -> "Expected ';'" — parseIncDecStmt
commits to the bare `cp++;`) is the deferred x++-as-lvalue gap, still rejected.

fixed while writing these tests: tuple-literal slots not being separate phrases
(kTupleExpr arm in lowerInPhrase); destructure rhs ppid (kDestructureStmt arm); and
store / move / swap ppid (kStoreStmt/kMoveStmt/kSwapStmt arms) — an audit found the
PPID dispatch silently no-op'd those, so a ppid in them crashed (e.g. p^ = x++).
lowerStatementPPID is now an EXHAUSTIVE switch (assert backstop) so a future stmt
kind can't silently skip lowering. Also: the "operand of '++' must be a variable"
diagnostic now carets the operand, not the operator. The for-range bound was NOT a
separate bug — an earlier crash there was the destructure crash misattributed (the
assert fires at the first surviving inc/dec); lowerForLong already lowers it.

not covered (aspirational):
  - a ppid RESULT used as an assignment / compound-assign TARGET (`p++^ += 1`,
    `++(arr[k++]) = 5`) — the deferred x++-as-lvalue gap, still a parse error.
    (A complex OPERAND — `arr[i]++`, `++p^` — is covered above; what is missing is
    treating the ppid's own result as a writable lvalue.)
*/

int32 show2(int32 a, int32 b) {
    __println("show " + a + " " + b);
    return 0;
}

int32 retPost(int32 v) { return v++; }   // post: returns OLD v (bump embedded, then dead)
int32 retPre(int32 v)  { return ++v; }   // pre: bump first, returns NEW v

Pt(int32 v_) {                           // a one-field class, for field-operand ppid
}

int32 main() {
    int32 a = 1;
    int32 x = a++;            // post: x reads old a, a bumps after
    __println("x= " + x + " a= " + a);

    int32 b = 1;
    int32 y = ++b;            // pre: b bumps first, y reads new b
    __println("y= " + y + " b= " + b);

    int32 c = 1;
    int32 z = (++c) + (++c);  // both pre: c bumps twice up front, reads see 3
    __println("z= " + z + " c= " + c);

    int32 d = 1;
    int32 w = d++ + d++;      // both post: reads see 1, two bumps after
    __println("w= " + w + " d= " + d);

    int32 s = 5;
    s = s++;                  // the statement is the phrase: store 5, bump after -> 6
    __println("s= " + s);

    int32 t = 5;
    t = t++ + t++;            // store 10, two post-bumps after -> 12
    __println("t= " + t);

    int32 e = 5;
    e--;                      // bare inc/dec statement
    __println("e= " + e);

    int32 g = 1;
    show2(g++, ++g);          // each arg is its own phrase -> show 1 3
    __println("g= " + g);

    int32 h = 0;
    bool ct = true;
    bool r1 = (ct && h++);    // rhs reached -> h bumps
    __println("h= " + h);
    __println("r1= " + r1);

    int32 i = 0;
    bool cf = false;
    bool r2 = (cf && i++);    // short-circuited -> i unchanged
    __println("i= " + i);
    __println("r2= " + r2);

    float32 f = 1.0;
    f++;                      // float scalar steps by 1.0
    __println("f= " + f);

    /* an inc on an ALIASED-type variable keeps the alias transparent: the inc
       expression is a binary OPERAND, so its OWN type drives the common-type rule.
       (A spelling round-trip would clobber 'Counter' to an unknown slid, which has
       no common type with int.) */
    alias Counter = int32;
    Counter cnt = 41;
    int32 sum = (cnt++) + 100;            // post-inc value 41 -> 141; cnt becomes 42
    __println("alias inc= " + sum);       // 141
    __println("alias var= " + cnt);       // 42

    /* || rhs is a sub-phrase: short-circuited (lhs true) skips the bump. */
    int32 j = 0;
    bool ot = true;
    bool r3 = (ot || j++);                // skipped -> j unchanged
    __println("j= " + j);                 // 0
    __println("r3= " + r3);               // true
    int32 k = 0;
    bool of = false;
    bool r4 = (of || k++);                // rhs reached -> k bumps
    __println("k= " + k);                 // 1
    __println("r4= " + r4);               // false

    /* an if CONDITION is a phrase: the post-bump fires after the condition. */
    int32 m = 0;
    if (m++ > 0) { __println("if-then"); } else { __println("if-else"); }
    __println("m= " + m);                 // 1 (read 0 -> else, bump after)

    /* a while CONDITION is a phrase, re-evaluated each pass (post-bump each time). */
    int32 n = 0;
    while (n++ < 3) { }
    __println("n= " + n);                 // 4

    /* tuple-literal slots are SEPARATE phrases, evaluated left to right. */
    int32 p = 1;
    (int32, int32) t1 = (p++, ++p);       // post then pre, same var -> (1, 3), p=3
    __println("t1= " + t1[0] + " " + t1[1] + " p= " + p);

    int32 c2 = 1;
    (int32, int32) t2 = (++c2, ++c2);     // two pre, same var -> (2, 3), c2=3
    __println("t2= " + t2[0] + " " + t2[1] + " c2= " + c2);

    int32 d2 = 1;
    (int32, int32) t3 = (d2++, d2++);     // two post, same var -> (1, 2), d2=3
    __println("t3= " + t3[0] + " " + t3[1] + " d2= " + d2);

    int32 p3 = 1;
    (int32, int32, int32) t4 = (p3++, p3++, p3++);   // three post, same var -> (1, 2, 3), p3=4
    __println("t4= " + t4[0] + " " + t4[1] + " " + t4[2] + " p3= " + p3);

    int32 a2 = 10;
    int32 b2 = 20;
    int32 c4 = 30;
    int32 d4 = 40;
    (int32, int32, int32, int32) t5 = (a2++, ++b2, c4--, --d4);   // different vars, mixed
    __println("t5= " + t5[0] + " " + t5[1] + " " + t5[2] + " " + t5[3]);   // 10 21 30 39
    __println("t5vars= " + a2 + " " + b2 + " " + c4 + " " + d4);           // 11 21 29 39

    /* destructure rhs slots are separate phrases too, left to right. */
    int32 dp = 5;
    int32 da2;
    int32 db2;
    (da2, db2) = (dp++, ++dp);            // da2=5 (dp=6), db2=7 (dp=7)
    __println("da2= " + da2 + " db2= " + db2 + " dp= " + dp);   // 5 7 7

    int32 ea = 1;
    int32 eb = 10;
    int32 dx;
    int32 dy;
    (dx, dy) = (ea++, eb--);              // different vars: dx=1 (ea=2), dy=10 (eb=9)
    __println("dx= " + dx + " dy= " + dy + " ea= " + ea + " eb= " + eb);   // 1 10 2 9

    /* multiple ppid in ONE phrase: all pres fire at phrase entry, all posts at
       phrase exit, every read sees the between value. */
    int32 sa = 1;
    int32 v1 = ++sa + sa++;               // 1 pre + 1 post, same var: pre sa=2, reads see 2 -> 4, post sa=3
    __println("v1= " + v1 + " sa= " + sa);            // 4 3

    int32 sm = 1;
    int32 v2 = ++sm + sm++ + ++sm + sm++; // 2 pre + 2 post, same var: pres sm=3, 4 reads see 3 -> 12, posts sm=5
    __println("v2= " + v2 + " sm= " + sm);            // 12 5

    int32 va = 1;
    int32 vb = 10;
    int32 vc = 100;
    int32 v3 = va++ + ++vb + vc--;        // different vars, mixed: pre vb=11; reads 1+11+100=112; posts va=2, vc=99
    __println("v3= " + v3);                           // 112
    __println("v3vars= " + va + " " + vb + " " + vc); // 2 11 99

    /* a for-RANGE bound is a phrase: the bump fires once, at loop setup. */
    int32 q = 2;
    int32 qn = 0;
    for (int32 ii : 0..q++) { qn = qn + 1; }
    __println("qn= " + qn + " q= " + q);

    /* range START and STEP also carry ppid — each bump fires once at setup. */
    int32 ra = 1;
    int32 rb = 2;
    int32 rsum = 0;
    for (int32 jj : ra++ .. 10 + rb++) { rsum = rsum + jj; }   // start 1 (ra=2), step +2 (rb=3)
    __println("rsum= " + rsum + " ra= " + ra + " rb= " + rb);  // 25 2 3

    /* a return value is a phrase: post returns the OLD value (read before bump). */
    int32 rp1 = retPost(5);
    int32 rp2 = retPre(5);
    __println("rp1= " + rp1 + " rp2= " + rp2);   // 5 6

    /* a do-while CONDITION is a phrase, re-tested after each pass (post-bump each).
       slids spells do-while as `while { body } (cond);` — body runs first. */
    int32 dw = 0;
    int32 dn = 0;
    while { dn = dn + 1; } (dw++ < 2);
    __println("dn= " + dn + " dw= " + dw);       // 3 3

    /* prefix ppid in a short-circuited && rhs is skipped too. */
    int32 pi = 0;
    bool pf = false;
    bool pr = (pf && ++pi);
    __println("pi= " + pi + " pr= " + pr);       // 0 false

    /* a switch scrutinee is a phrase: the bump fires once as it is evaluated. */
    int32 sw = 1;
    switch (sw++) {
        case 1: { __println("sw case 1"); break; }
        default: { __println("sw default"); }
    }
    __println("sw= " + sw);                       // 2

    /* store / move / swap statements are phrases too: a ppid operand lifts off and
       the post fires after the statement. */
    int32 stx = 5;
    int32 sty = 0;
    int32^ stp = ^sty;
    stp^ = stx++;                                 // store old stx into sty, then stx bumps
    __println("sty= " + sty + " stx= " + stx);    // 5 6

    int32 ar[3];
    ar[0] = 0;
    ar[1] = 0;
    ar[2] = 0;
    int32 kk = 1;
    ar[kk++] = 99;                                // index bump lifts: store ar[1], then kk++
    __println("ar1= " + ar[1] + " kk= " + kk);    // 99 2

    int32 mva = 5;
    int32 mvb = 0;
    mvb <-- mva++;                                // move old mva into mvb, then mva bumps
    __println("mvb= " + mvb + " mva= " + mva);    // 5 6

    /* COMPLEX-LVALUE ppid: ++/-- on an array element or a deref, in statement AND
       expression position, prefix AND postfix. The leaf address is bound ONCE (a
       side-effecting index evaluates a single time) and the scalar read/bump split
       is preserved (post defers to the phrase exit). A class FIELD reduces to this
       same path — `b.f` lowers to an index — so it needs no separate test. */
    int32 cl[4] = (10, 20, 30, 40);
    cl[0]++;                                      // statement post -> 11
    ++cl[1];                                      // statement pre  -> 21
    cl[2]--;                                      // statement post-dec -> 29
    __println("cl= " + cl[0] + " " + cl[1] + " " + cl[2]);          // 11 21 29
    int32 clp = cl[3]++;                          // expr post: reads 40, then cl[3]=41
    int32 clq = ++cl[3];                          // expr pre: cl[3]=42, reads 42
    __println("clp= " + clp + " clq= " + clq + " cl3= " + cl[3]);   // 40 42 42

    int32 dv = 5;
    int32^ dpp = ^dv;
    dpp^++;                                       // statement post on a deref -> dv=6
    int32 dr = dpp^++;                            // expr post on a deref: reads 6, dv=7
    __println("dv= " + dv + " dr= " + dr);        // 7 6

    /* address-once: a side-effecting index is evaluated a single time, for a 1D
       and a multi-dim element. */
    int32 ao[3] = (100, 200, 300);
    int32 aok = 0;
    ao[aok++]++;                                  // ao[0] bumps; aok read once -> ao[0]=101, aok=1
    __println("ao0= " + ao[0] + " aok= " + aok); // 101 1

    int32 md[2][3];
    int32 mi = 0;
    while (mi < 6) { md[mi/3][mi%3] = 0; mi = mi + 1; }
    int32 mk = 0;
    md[mk++][1]++;                                // 2D elem; outer index once -> md[0][1]=1, mk=1
    __println("md01= " + md[0][1] + " mk= " + mk);   // 1 1

    /* a complex operand inside a SUB-PHRASE (a call arg): each arg is its own
       phrase, the bump fires at that phrase's exit. */
    int32 sp[2] = (7, 8);
    show2(sp[0]++, ++sp[1]);                      // show 7 9 ; sp -> 8 9
    __println("sp= " + sp[0] + " " + sp[1]);      // 8 9

    /* short-circuit on a complex operand: the && rhs bumps only when reached. */
    int32 sc[2] = (3, 4);
    bool scz = (false && (sc[0]++ > 0));          // skipped -> sc[0] unchanged
    bool scw = (true && (sc[1]++ > 0));           // reached -> sc[1] bumps
    __println("sc= " + sc[0] + " " + sc[1]);      // 3 5
    __println("scz= " + scz + " scw= " + scw);    // false true

    /* post defers to phrase exit, so a re-read in the same phrase sees the OLD
       value — old+old, then the bump (consistent with the scalar `a++ + a`). */
    int32 oo[2] = (7, 0);
    int32 ooq = oo[0]++ + oo[0];                  // 7 + 7 = 14, then oo[0]=8
    __println("ooq= " + ooq + " oo0= " + oo[0]);  // 14 8

    /* a non-int (float) element steps by one; post reads the old value. */
    float64 fl[2] = (1.5, 2.5);
    fl[0]++;                                      // float element post -> 2.5
    float64 flr = fl[1]--;                        // post-dec: reads 2.5, fl[1]=1.5
    __println("fl= " + fl[0] + " " + fl[1] + " flr= " + flr);   // 2.5 1.5 2.5

    /* decrement through a deref. */
    int32 dd = 9;
    int32^ ddp = ^dd;
    ddp^--;                                       // -> dd=8
    __println("dd= " + dd);                       // 8

    /* two DISTINCT complex elements in one phrase: both posts defer to phrase exit. */
    int32 tw[2] = (5, 50);
    int32 twq = tw[0]++ + tw[1]++;                // 5 + 50 = 55, then tw -> 6, 51
    __println("twq= " + twq + " tw= " + tw[0] + " " + tw[1]);   // 55 6 51

    /* an ITERATOR element steps by one ELEMENT (pointer arithmetic), not by 1. */
    int32 idata[4] = (10, 20, 30, 40);
    int32[] its[2];
    its[0] = ^idata[0];
    its[1] = ^idata[2];
    its[0]++;                                     // step element 0: now points at idata[1]
    int32 ia = its[0]^;                           // 20
    int32 ib = (its[1]++)^;                       // post: reads idata[2]=30, then steps
    int32 ic = its[1]^;                           // 40
    __println("ia= " + ia + " ib= " + ib + " ic= " + ic);      // 20 30 40

    /* a class FIELD operand — `pt.v_` lowers to an index, the same complex path. */
    Pt pt = 10;
    pt.v_++;                                      // field post -> 11
    int32 ptpre = ++pt.v_;                        // field pre  -> 12
    __println("ptv= " + pt.v_ + " ptpre= " + ptpre);   // 12 12

    /* swap-OPERAND ppid (`x++ <--> y++`) is blocked at the PARSER — parseIncDecStmt
       commits to a bare `x++;` (the deferred x++-as-lvalue gap) — so the kSwapStmt
       PPID arm is defensive / unreachable today, not exercised here. */

    //-EXPECT-ERROR: Constant 'K' cannot be incremented
    //const int32 K = 5;
    //K++;

    //-EXPECT-ERROR: Operator '++' is not defined on type 'bool'
    //bool bb = true;
    //bb++;

    //-EXPECT-ERROR: The operand of '++' must be a variable
    //int32 lv = 5++;

    //-EXPECT-ERROR: is a function, not a variable
    //main++;

    /* a ppid on a complex (deref) lvalue is not supported: the inc commits to a
       bare `p++;` statement, so the deref/store after it is a parse error. */
    //-EXPECT-ERROR: Expected ';'
    //int32 cx = 5;
    //int^ cp = ^cx;
    //cp++^ = 7;
    //__println("cx= " + cx);

    return 0;
}
