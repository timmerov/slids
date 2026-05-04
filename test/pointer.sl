/*
test pointer math.
*/

int32 main() {

    int array[10] = (0, 1, 2, 3, 4, 5, 6, 7, 8, 9);

    /* check pointers. */

    int[] ptr = ^array[0];
    __println("^ptr<0>=" + ptr^);

    ++ptr;
    __println("^ptr<1>=" + ptr^);

    ptr++;
    __println("^ptr<2>=" + ptr^);

    --ptr;
    __println("^ptr<1>=" + ptr^);

    ptr--;
    __println("^ptr<0>=" + ptr^);

    ptr += 5;
    __println("^ptr<5>=" + ptr^);

    ptr = ptr + 2;
    __println("^ptr<7>=" + ptr^);

    ptr -= 5;
    __println("^ptr<2>=" + ptr^);

    ptr = ptr - 2;
    __println("^ptr<0>=" + ptr^);

    other = ptr + 8;
    __println("^other<8>=" + other^);

    intptr diff = other - ptr;
    __println("diff<8>=" + diff);

    if (ptr == other) {
        __println("ptr==other<false>=true");
    } else {
        __println("ptr==other<false>=false");
    }

    if (ptr != other) {
        __println("ptr!=other<true>=true");
    } else {
        __println("ptr!=other<true>=false");
    }

    if (ptr < other) {
        __println("ptr<other<true>=true");
    } else {
        __println("ptr<other<true>=false");
    }

    if (ptr <= other) {
        __println("ptr<=other<true>=true");
    } else {
        __println("ptr<=other<true>=false");
    }

    if (ptr > other) {
        __println("ptr>other<false>=true");
    } else {
        __println("ptr>other<false>=false");
    }

    if (ptr >= other) {
        __println("ptr>=other<false>=true");
    } else {
        __println("ptr>=other<false>=false");
    }

    /*
    these should be compile errors.
    pointer math only applies to pointers
    of the same type.
    */
    char[] not_int_ptr = nullptr;
    //-EXPECT-ERROR-DEFERRED: char[] - int[] subtraction not focused as a typed-pointer-mismatch error
    //diff = not_int_ptr - ptr;
    //-EXPECT-ERROR: pointer comparison requires same pointee type
    //bool cond = (not_int_ptr == ptr);
    //-EXPECT-ERROR: pointer comparison requires same pointee type
    //bool cond = (not_int_ptr != ptr);
    //-EXPECT-ERROR: pointer comparison requires same pointee type
    //bool cond = (not_int_ptr < ptr);
    //-EXPECT-ERROR: pointer comparison requires same pointee type
    //bool cond = (not_int_ptr <= ptr);
    //-EXPECT-ERROR: pointer comparison requires same pointee type
    //bool cond = (not_int_ptr > ptr);
    //-EXPECT-ERROR: pointer comparison requires same pointee type
    //bool cond = (not_int_ptr >= ptr);

    //-EXPECT-ERROR: pointer + pointer is not allowed
    //ptr += ptr;
    //-EXPECT-ERROR-DEFERRED: ptr -= ptr not focused as a typed-pointer error
    //ptr -= ptr;
    //-EXPECT-ERROR-DEFERRED: ptr *= ptr not focused as a typed-pointer error
    //ptr *= ptr;
    //-EXPECT-ERROR-DEFERRED: ptr /= ptr not focused as a typed-pointer error
    //ptr /= ptr;
    //-EXPECT-ERROR-DEFERRED: ptr %= ptr not focused as a typed-pointer error
    //ptr %= ptr;

    void^ void_ptr = other;
    __println("void^ = int^ pass");

    other = <int[]> void_ptr;
    __println("int^ = int[] void^ pass");

    //-EXPECT-ERROR-DEFERRED: cross-type pointer assign char[] → int[] not focused as a typed-pointer error
    //ptr = not_int_ptr;

    /* reference tests. ^ is a reference: no arithmetic. */
    int^ ref = ^array[3];
    __println("ref^<3>=" + ref^);

    ref^ = 99;
    __println("ref^<99>=" + ref^);

    ref^ = 3;
    __println("ref^<3>=" + ref^);

    ref = ^array[5];
    __println("ref^<5>=" + ref^);

    int^ ref2 = ^array[7];

    if (ref == ref2) {
        __println("ref==ref2<false>=true");
    } else {
        __println("ref==ref2<false>=false");
    }

    if (ref != ref2) {
        __println("ref!=ref2<true>=true");
    } else {
        __println("ref!=ref2<true>=false");
    }

    int^ null_ref = nullptr;
    if (null_ref == nullptr) {
        __println("null_ref==nullptr<true>=true");
    } else {
        __println("null_ref==nullptr<true>=false");
    }

    if (null_ref != nullptr) {
        __println("null_ref!=nullptr<false>=true");
    } else {
        __println("null_ref!=nullptr<false>=false");
    }

    //-EXPECT-ERROR: arithmetic on references is not allowed
    //++ref;
    //-EXPECT-ERROR: arithmetic on references is not allowed
    //--ref;
    //-EXPECT-ERROR-DEFERRED: ref += 1 — not focused as a reference-arithmetic error
    //ref += 1;
    //-EXPECT-ERROR-DEFERRED: ref -= 1 — not focused as a reference-arithmetic error
    //ref -= 1;
    //-EXPECT-ERROR-DEFERRED: ref = ref + 1 — not focused as a reference-arithmetic error
    //ref = ref + 1;
    //-EXPECT-ERROR-DEFERRED: ref = ref - 1 — not focused as a reference-arithmetic error
    //ref = ref - 1;
    //-EXPECT-ERROR-DEFERRED: ref - ref2 — not focused as a reference-arithmetic error
    //ref = ref - ref2;
    //-EXPECT-ERROR: references only support '==' and '!='
    //bool rcond = (ref < ref2);
    //-EXPECT-ERROR: references only support '==' and '!='
    //bool rcond = (ref <= ref2);
    //-EXPECT-ERROR: references only support '==' and '!='
    //bool rcond = (ref > ref2);
    //-EXPECT-ERROR: references only support '==' and '!='
    //bool rcond = (ref >= ref2);

    /* hybrid pointer/reference tests. pointer demotes to reference. */
    ref = ptr;
    __println("ref^<0>=" + ref^);

    if (ref == ptr) {
        __println("ref==ptr<true>=true");
    } else {
        __println("ref==ptr<true>=false");
    }

    if (ref != ptr) {
        __println("ref!=ptr<false>=true");
    } else {
        __println("ref!=ptr<false>=false");
    }

    if (ref == ref2) {
        __println("ref==ref2<false>=true");
    } else {
        __println("ref==ref2<false>=false");
    }

    if (ref != ref2) {
        __println("ref!=ref2<true>=true");
    } else {
        __println("ref!=ref2<true>=false");
    }

    //-EXPECT-ERROR: references cannot promote to iterators
    //ptr = ref;
    //-EXPECT-ERROR-DEFERRED: ptr < ref — pointer/reference cross-comparison not focused yet
    //bool hcond = (ptr < ref);
    //-EXPECT-ERROR-DEFERRED: ptr <= ref — pointer/reference cross-comparison not focused yet
    //bool hcond = (ptr <= ref);
    //-EXPECT-ERROR-DEFERRED: ptr > ref — pointer/reference cross-comparison not focused yet
    //bool hcond = (ptr > ref);
    //-EXPECT-ERROR-DEFERRED: ptr >= ref — pointer/reference cross-comparison not focused yet
    //bool hcond = (ptr >= ref);
    //-EXPECT-ERROR-DEFERRED: ref < ptr — pointer/reference cross-comparison not focused yet
    //bool hcond = (ref < ptr);
    //-EXPECT-ERROR-DEFERRED: ref <= ptr — pointer/reference cross-comparison not focused yet
    //bool hcond = (ref <= ptr);
    //-EXPECT-ERROR-DEFERRED: ref > ptr — pointer/reference cross-comparison not focused yet
    //bool hcond = (ref > ptr);
    //-EXPECT-ERROR-DEFERRED: ref >= ptr — pointer/reference cross-comparison not focused yet
    //bool hcond = (ref >= ptr);
    //-EXPECT-ERROR-DEFERRED: ptr += ref — pointer/reference cross-arith not focused yet
    //ptr += ref;
    //-EXPECT-ERROR-DEFERRED: ptr -= ref — pointer/reference cross-arith not focused yet
    //ptr -= ref;
    //-EXPECT-ERROR-DEFERRED: ptr = ptr + ref — pointer/reference cross-arith not focused yet
    //ptr = ptr + ref;
    //-EXPECT-ERROR-DEFERRED: ptr = ptr - ref — pointer/reference cross-arith not focused yet
    //ptr = ptr - ref;

    return 0;
}
