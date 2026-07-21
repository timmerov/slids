/*
test comparison expressions.

    == != >= <= > <

widen rules apply when comparing numbers.
implicit type casts apply when comparing pointers.
*/

/*
claude says:

a comparison converts its operands first, like every other binary operator. for
NUMBERS that is the ordinary widening; for POINTERS it used to be the one place that
demanded the two sides already have IDENTICAL types.

the pointer conversions are not new — they are the ones the language already performs
at a call, an assignment, and a return. one set of rules decides what a pointer
implicitly becomes (classify's ptrImplicitOk / ptrBaseUpcastOk, shared with overload
ranking), so a comparison and a call cannot drift apart on the answer:

  nullptr -> any pointer
  any pointer -> intptr           the one bridge between pointers and integers
  iterator -> reference           of the same pointee
  derived -> base                 the base is the slot-0 sub-object: same address

DIRECTION IS NOT A CHOICE. pointer -> intptr is implicit while intptr -> pointer needs
an explicit cast, so a mixed pair always settles on intptr. a derived/base pair settles
on the base. that settled type is what BOTH operands convert to, so `(p == q)` and
`(q == p)` agree.

WHICH OPERATORS. a reference has no sequence position, so a pair involving one admits
only == and != — unchanged, and ordering an iterator against a reference is still an
error because the pair settles on the reference. iterators and intptr admit all six.

POINTERS ARE SIGNED. ordering uses the signed predicates, the same ones an intptr gets.
that is observable: a pointer built from a negative intptr orders BELOW one built from
a positive one. ordered unsigned, the same two would compare one way against each other
and the opposite way against an intptr holding the same bits.
*/

/* a base and a derived, to compare pointers across an inheritance edge. */
Base(int b_ = 1) {
    int bget() { return b_; }
}

Base : Derived(int d_ = 2) {
    int dget() { return d_; }
}

/* unrelated to both, for the negatives: being a class is not what makes a pair
   compatible — inheritance is. */
Other(int o_ = 3) { }

int32 main() {

    /* ---- numbers: the ordinary widening applies ---- */
    int8  n8  = 4;
    int64 n64 = 4;
    __println("num eq  = " + (n8 == n64));
    __println("num lt  = " + (n8 <  n64));
    __println("num lit = " + (n8 == 4));
    __println("num uns = " + (n8 <  100000));

    /* ---- baseline: identical pointee ---- */
    int arr[4] = (10, 20, 30, 40);
    int[] p0 = ^arr[0];
    int[] p2 = ^arr[2];
    __println("it eq   = " + (p0 == p2));
    __println("it ne   = " + (p0 != p2));
    __println("it lt   = " + (p0 <  p2));
    __println("it le   = " + (p0 <= p2));
    __println("it gt   = " + (p0 >  p2));
    __println("it ge   = " + (p0 >= p2));

    /* a reference pair admits == and != only. */
    int x = 5;
    int y = 6;
    int^ rx  = ^x;
    int^ ry  = ^y;
    int^ rx2 = ^x;
    __println("ref eq  = " + (rx == rx2));
    __println("ref ne  = " + (rx != ry));

    /* ---- nullptr against a pointer, both orders ---- */
    int^ rnull = nullptr;
    __println("null l  = " + (nullptr == rnull));
    __println("null r  = " + (rnull == nullptr));
    __println("null ne = " + (rx != nullptr));

    /* ---- reference vs iterator: the iterator demotes to the reference ---- */
    /* ra and p0 address the same object, so they compare equal. */
    int^ ra = ^arr[0];
    __println("mix eq  = " + (ra == p0));
    __println("mix eq' = " + (p0 == ra));
    __println("mix ne  = " + (ra != p2));
    __println("mix ne' = " + (p2 != ra));

    /* ---- pointer vs intptr: the pointer converts, the pair settles on intptr ---- */
    intptr i0 = <intptr> p0;
    intptr i2 = <intptr> p2;
    __println("ip eq   = " + (p0 == i0));
    __println("ip eq'  = " + (i0 == p0));
    __println("ip ne   = " + (p0 != i2));
    /* ordering is fine: an iterator and an integer, no reference in the pair. */
    __println("ip lt   = " + (p0 <  i2));
    __println("ip le   = " + (p0 <= i2));
    __println("ip gt   = " + (i2 >  p0));
    __println("ip ge   = " + (i2 >= p0));
    /* a reference against an intptr still admits == and != only. */
    __println("ip refeq= " + (rx == <intptr> rx));

    /* ---- derived vs base: the derived converts to the base ---- */
    Derived d;
    Base b;
    Derived^ rd  = ^d;
    Base^    rb  = ^b;
    /* the base sub-object sits at offset 0, so a derived address IS its base address. */
    Base^    rdb = ^d;
    __println("cls eq  = " + (rb == rd));
    __println("cls eq' = " + (rd == rb));
    __println("cls same= " + (rd == rdb));
    __println("cls ne  = " + (rd != rb));

    /* the same edge through ITERATORS, which admit all six. */
    Derived darr[2];
    Derived[] pd0 = ^darr[0];
    Derived[] pd1 = ^darr[1];
    Base[]    pb0 = ^darr[0];
    __println("cit eq  = " + (pb0 == pd0));
    __println("cit lt  = " + (pb0 <  pd1));
    __println("cit le  = " + (pb0 <= pd1));
    __println("cit gt  = " + (pd1 >  pb0));
    __println("cit ge  = " + (pd1 >= pb0));

    /* ---- pointers order SIGNED ---- */
    /* built from raw values and never dereferenced: -1 must order BELOW +1. an
       unsigned ordering reads the same bits as the largest address and answers false. */
    intptr neg = -1;
    intptr pos = 1;
    int[] ineg = <int[]> neg;
    int[] ipos = <int[]> pos;
    __println("sgn lt  = " + (ineg <  ipos));
    __println("sgn ge  = " + (ipos >= ineg));

    return 0;
}

/* an unrelated pointee is still rejected — the rule is compatibility, not identity. */
//-EXPECT-ERROR: Pointer comparison requires the same pointee type.
//bool neg_unrelated() {
//    int x = 0;
//    char c = 'a';
//    int^ ri = ^x;
//    char^ rc = ^c;
//    return (ri == rc);
//}

/* two unrelated CLASSES are unrelated pointees like any other. */
//-EXPECT-ERROR: Pointer comparison requires the same pointee type.
//bool neg_unrelated_class() {
//    Derived d;
//    Other o;
//    Derived^ rd = ^d;
//    Other^ ro = ^o;
//    return (rd == ro);
//}

/* intptr is the ONLY integer that bridges to a pointer; a sized int does not. */
//-EXPECT-ERROR: Pointer comparison requires the same pointee type.
//bool neg_plain_int() {
//    int x = 0;
//    int^ r = ^x;
//    int32 n = 4;
//    return (r == n);
//}

/* ordering a reference is rejected however the pair was reached. */
//-EXPECT-ERROR: References support only '==' and '!=' comparison.
//bool neg_ref_order() {
//    int x = 0;
//    int y = 0;
//    int^ a = ^x;
//    int^ b = ^y;
//    return (a < b);
//}

/* an iterator ordered against a reference is the same rejection: the pair settles on
   the reference, which has no sequence position. */
//-EXPECT-ERROR: References support only '==' and '!=' comparison.
//bool neg_mixed_order() {
//    int arr[2] = (1, 2);
//    int[] it = ^arr[0];
//    int^ r = ^arr[1];
//    return (it < r);
//}
