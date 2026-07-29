/*
test taking the address of values.

    ^ value

the address operator binds loosely.
value can be a wide variety of forms that have storage.
*/

/*
claude says:

- `^value` yields a POINTER to the operand's storage, so the operand must HAVE
  storage — it must be an lvalue. the result CATEGORY is operand-driven: an
  INDEXED element yields an ITERATOR (`T[]`, a position in a sequence), and every
  other form yields a REFERENCE (`T^`, one object). the whole array yields a
  reference to the array (`T[N]^`), which is exactly why `^arr` needs no cast.
- the operand is a CHAIN: a ROOT, then any run of `.field` and `[index]` steps in
  any order. resolve walks the chain down to its root and requires storage THERE,
  since that is what the address is computed from. two roots have it:
    * a NAMED VARIABLE — a local, a parameter, the receiver, or a global.
    * a DEREF (`p^`) — its storage is the pointee, which the pointer already
      reaches, so there is an address to hand back.
  an rvalue root has no storage: a call result, a literal, and an arithmetic
  result live in no addressable slot. a name that is not storage at all — a
  function (code), a `const` (substituted at compile time) — is rejected with its
  own message, since the operand IS a variable spelling but names the wrong thing.
- THE DEREF ROOT LANDED 2026-07-29. Before it, everything reached through a
  pointer (`^p^`, `^p^.f`, `^p^[i]`) was refused in resolve even though the rest
  of the pipeline already handled it: classify's address-of is operand-driven
  (it wraps whatever the operand inferred) and codegen's emitElementAddr has
  always accepted a deref as a chain base. resolve was the only stage refusing.
  This is also what `#x` needs, since `#x` desugars to a 5-tuple whose last slot
  is `^x` — see test/misc/macros.sl.
- `^X^` CANCELS. The address of a deref IS the pointer value, so `^p^` collapses
  to `p` in classify and emits no instruction. (That rule predates the deref
  root, but nothing spelled in SOURCE could reach it — resolve rejected the
  spelling first. It existed for the derefs classify synthesizes itself, e.g. the
  class-index sugar that rewrites `obj[i]` to `(obj.op[](i))^`.)
- ALIASING is a property of the ROOT, not of the chain. Taking the address of a
  NAMED variable aliases that variable: it counts as a write (so reading through
  the alias is not a use-before-init) and as a read (so it is not swept unused).
  A DEREF root aliases nothing new — the pointee was already reachable through
  the pointer — so there the pointer is merely read.
*/

import string;

Point(int x = 1, int y = 2) { }
Nest(Point p, int tag = 9) { }

int five() {
    return 5;
}

int g_val = 40;
const int kSize = 3;

int32 main() {

    /* ── a NAMED VARIABLE root ── */
    int n = 7;
    int^ rn = ^n;
    println(String + "local = " + rn^);                    // 7

    /* the reference IS that storage — a write through it lands on n. */
    rn^ = 8;
    println(String + "wrote = " + n);                      // 8

    /* a global has static storage, so it is addressable too. */
    int^ rg = ^g_val;
    println(String + "global = " + rg^);                   // 40

    /* two addresses of one variable are one pointer. */
    println(String + "same = " + (^n == ^n));              // true

    /* ── chain steps over a named root: [index] and .field ── */
    int arr[3];
    arr[0] = 10;
    arr[1] = 11;
    arr[2] = 12;

    /* an INDEXED element yields an ITERATOR. */
    int[] ie = ^arr[1];
    println(String + "elem = " + ie^);                     // 11

    /* the WHOLE array yields a reference to the array — no cast needed. */
    int[3]^ wa = ^arr;
    println(String + "whole = " + wa^[2]);                 // 12

    /* a field yields a reference. */
    Point pt;
    int^ rf = ^pt.y;
    println(String + "field = " + rf^);                    // 2

    /* a nested field, and a field of an array element. */
    Nest nst;
    int^ rnf = ^nst.p.x;
    println(String + "nested field = " + rnf^);            // 1
    Point pts[2];
    int^ rea = ^pts[1].y;
    println(String + "field of elem = " + rea^);           // 2

    /* ── a DEREF root (landed 2026-07-29) ── */
    Point^ pp = ^pt;
    int^ rd = ^pp^.y;
    println(String + "deref field = " + rd^);              // 2

    /* still the same storage: writing through it lands on pt. */
    rd^ = 20;
    println(String + "deref wrote = " + pt.y);             // 20

    /* `^X^` CANCELS — the address of a deref is the pointer itself. */
    Point^ pq = ^pp^;
    println(String + "cancel = " + (pq == pp));            // true

    /* a deref root under an INDEX step -> an iterator. */
    int[3]^ ap = ^arr;
    int[] rdi = ^ap^[2];
    println(String + "deref elem = " + rdi^);              // 12

    /* a deref root under TWO field steps. */
    Nest^ np = ^nst;
    int^ rdn = ^np^.p.y;
    println(String + "deref nested = " + rdn^);            // 2

    /* a deref root whose pointer came from an array element. */
    Point^ pep = ^pts[0];
    int^ rmix = ^pep^.x;
    println(String + "deref of elem field = " + rmix^);    // 1

    return 0;
}

/*
negatives — the operand must have storage.
*/

/* a call RESULT is an rvalue: it lives in no addressable slot. */
//-EXPECT-ERROR: The operand of '^' must be a variable or array element.
//void neg_call() {
//    int^ r = ^five();
//    println(String + r^);
//}

/* an arithmetic result is an rvalue for the same reason. */
//-EXPECT-ERROR: The operand of '^' must be a variable or array element.
//void neg_arith() {
//    int a = 1;
//    int b = 2;
//    int^ r = ^(a + b);
//    println(String + r^);
//}

/* a literal has no storage. */
//-EXPECT-ERROR: The operand of '^' must be a variable or array element.
//void neg_literal() {
//    int^ r = ^42;
//    println(String + r^);
//}

/* a FUNCTION name is code, not storage. The operand IS a variable spelling, so
   the chain walk accepts it and the entry-kind check rejects it by name. */
//-EXPECT-ERROR: Cannot take the address of 'five'
//void neg_function() {
//    int^ r = ^five;
//    println(String + r^);
//}

/* a `const` is substituted at compile time — there is no runtime slot to point at. */
//-EXPECT-ERROR: Cannot take the address of 'kSize'
//void neg_const() {
//    int^ r = ^kSize;
//    println(String + r^);
//}
