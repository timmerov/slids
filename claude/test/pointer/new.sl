/*
test new and delete.

new returns a reference or an iterator.
delete sets the pointer to nullptr.

    int^ ref = new int;
    int[] iter = new int[4];
    delete ref;
    delete iter;

placement new instantiates the object at the given buffer-class pointer.
buffer-class pointers are void^, int8^, uint8^.

    intptr sz = 2 * sizeof(int);
    void^ rawp = new int8[sz];

    int[] intp = new(rawp) int[2];

    delete rawp;
*/

/*
claude says:

- a kNewExpr (a primary). grammar: `new`, an optional `(addr)` placement prefix,
  the element type, an optional `[n]` size. children[0] = the array-size expr (or
  null = single), [1] = the placement-address expr (or null = heap). yields T^
  (single) or T[] (array). the `new T(value)` initializer form needs tuples — not
  landed. a kDeleteStmt holds the pointer EXPRESSION in children[0].
- the element type is the ONE parseType, so it is ANY variable type — a pointer,
  iterator, tuple, grouped `(const int)^`, alias, or multi-dim array.
- placement vs a `(`-led ELEMENT type after `new (`: newParenStartsPlacement()
  decides by the token AFTER the balanced `(...)` — a type-start (primitive / ident
  / `::` / `const` / `(`) means placement (a placement addr is always followed by an
  element type); anything else (`^ ^^ [] [N] ; , ) { EOF`) means the `(...)` IS the
  element type (tuple `(int,int)`, grouped `(const int)^`). corner: `new (Foo)(3)`
  reads as placement, not `new Foo(3)`.
- MULTI-DIM `new T[n][d]...`: the first `[n]` is the runtime count, the further `[d]`
  dims are the element's trailing dims (n copies of T[d]...); a non-literal element
  dim is rejected (no dim folding in `new`).
- classify: a heap element must be statically sized (widen::typeByteSize >= 0 —
  Phase 4 primitives; a slid -> "Cannot allocate"); an array size must be
  integer-class; a placement address must be a buffer-class pointer (void^ /
  int8^ / uint8^, the same set as casts). delete's operand is ANY pointer
  EXPRESSION (classify: a pointer type); an LVALUE (variable / field / element /
  slot / deref) is nulled back, an RVALUE (call return / op result) is freed only.
- codegen: `new T` -> malloc(sizeof(T)); `new T[n]` -> malloc(n * sizeof(T));
  placement -> the address itself (no allocation — for primitives nothing is
  constructed). delete -> load the pointer, free(it), store null back. malloc /
  free are declared in the module preamble. No destructors run — those land with
  classes (Phase 5).
*/

alias Big = int64;

Simple(int x_ = -2) {
    _() {
        __println("Simple:ctor: " + x_);
    }
    ~() {
        __println("Simple:dtor: " + x_);
    }
}

// a TRIVIAL class (no ctor/dtor) with two field defaults — exercises field-init
// without the cookie/hook machinery, and multi-field construction.
Plain(int a_ = 9, int b_ = 8) {
}

// returns a heap pointer — delete of the RETURN VALUE is an rvalue (freed, nothing
// to null).
int^ allocInt() { return new int; }

int32 main() {
    /* heap single: new T -> T^. */
    int^ ref = new int;
    ref^ = 42;
    __println("single= " + ref^);                    // 42
    delete ref;
    __println("freed single= " + (ref == nullptr));  // true

    /* heap array: new T[n] -> T[] (n is a runtime expression). */
    int[] iter = new int[4];
    iter[0] = 7;
    iter[3] = 9;
    __println("array[0]= " + iter[0]);               // 7
    __println("array[3]= " + iter[3]);               // 9
    delete iter;
    __println("freed array= " + (iter == nullptr));  // true

    /* placement: a raw buffer-class buffer, then build an int[2] inside it. */
    intptr sz = 2 * sizeof(int);
    void^ rawp = new int8[sz];
    int[] intp = new(rawp) int[2];
    intp[0] = 100;
    intp[1] = 200;
    __println("placement[0]= " + intp[0]);           // 100
    __println("placement[1]= " + intp[1]);           // 200
    delete rawp;

    /* a non-4-byte element: the allocation size multiplies by the width. */
    int64[] wide = new int64[3];
    wide[2] = 7;
    __println("wide[2]= " + wide[2]);                // 7
    delete wide;

    /* placement single: new(addr) T (no [n]) -> T^. */
    int8[] one = new int8[sizeof(int)];
    int^ slot = new(one) int;
    slot^ = 5;
    __println("placement single= " + slot^);         // 5
    delete one;

    /* the new result obeys the implicit cast rules — a single object strips to
       void^ / intptr, same as any pointer. */
    void^ vp = new int;
    __println("to void^= " + (vp != nullptr));       // true
    delete vp;
    intptr ip = new int;
    __println("to intptr= " + (ip != 0));            // true
    int^ ipp = <int^> ip;                            // explicit intptr -> pointer
    delete ipp;

    /* new of an alias type resolves the element (Big = int64). a `Big^` lhs is
       not yet declarable, so the result is held in an int64^. */
    int64^ ab = new Big;
    ab^ = 11;
    __println("alias elem= " + ab^);                 // 11
    delete ab;

    /* new/delete a single object. */
    __println("ctor 1 after");
    simple_ref = new Simple(1);
    delete simple_ref;
    __println("dtor 1 before");

    /* new/delete an array of objects. */
    __println("ctor -2 -2 -2 after");
    simple_arr = new Simple[3];
    simple_arr[0].x_ = 10;
    simple_arr[1].x_ = 11;
    simple_arr[2].x_ = 12;
    delete simple_arr;
    __println("dtor 12 11 10 before");

    /* placment new a single object and direct call the dtor. */
    __println("ctor 20 after");
    raw_ptr = new int8[sizeof(Simple)];
    simple_place = new(raw_ptr) Simple(20);
    simple_place^.~();
    delete raw_ptr;
    __println("dtor 20 before");

    /* a TRIVIAL class: field-init only (no ctor/dtor output), plain free. */
    pl = new Plain(1, 2);                              // multi-field construct
    __println("plain single: " + pl^.a_ + " " + pl^.b_);   // 1 2
    delete pl;
    pls = new Plain[2];                                // array: field defaults, no cookie
    __println("plain array: " + pls[0].a_ + " " + pls[1].b_); // 9 8
    delete pls;

    /* a no-args single new: default-construct. */
    __println("default ctor after");
    sd = new Simple;
    delete sd;
    __println("default dtor before");                 // ctor/dtor -2

    /* field WRITES through different lvalue bases. */
    Plain pv;
    pv.a_ = 7;                                         // plain class value: cls.field
    __println("pv.a= " + pv.a_);                      // 7
    sp = new Simple(5);
    sp^.x_ = 99;                                       // pointer deref: ptr^.field
    delete sp;                                         // dtor 99

    /* null single-delete is a safe no-op (not a crash). */
    Simple^ snull = nullptr;
    delete snull;
    __println("survived null delete");

    /* new T[0] — a zero-count array allocates, constructs nothing, frees. */
    z = new Simple[0];
    delete z;
    __println("empty array ok");

    /* delete takes ANY pointer expression, not just a variable: a NON-variable
       lvalue (array element / tuple slot) is freed AND nulled back; an rvalue (a
       call return) is freed with nothing to null. */
    {
        int^ ea[2] = (new int, new int);
        delete ea[0];                                     // array element lvalue
        __println("ea[0] null= " + (ea[0] == nullptr));   // true
        delete ea[1];
        (int^, int^) et = (new int, new int);
        delete et[0];                                     // tuple slot lvalue
        __println("et[0] null= " + (et[0] == nullptr));   // true
        delete et[1];
        delete allocInt();                                // rvalue (fn return)
        /* a DEREF lvalue: pp^ is a pointer; delete frees it + nulls pp^. (`int^ ^`
           spells the pointer-to-pointer type — `int^^` mis-lexes as the `^^` operator;
           see todo.) */
        int^ rr = new int;
        int^ ^ pp = ^rr;
        delete pp^;
        __println("deref delete, rr null= " + (rr == nullptr));   // true
        __println("non-variable delete ok");
    }

    /* --- the element type may be ANY variable type, not just a primitive or a
       class: a pointer, an array of pointers, a tuple, a grouped type, multi-dim. --- */

    /* a POINTER element: new T^ heap-allocates a pointer. (`int^ ^` spells the
       pointer-to-pointer result.) */
    int pe_v = 5;
    int^ ^ pe = new int^;
    pe^ = ^pe_v;
    __println("ptr elem= " + pe^^);                  // 5
    delete pe;

    /* an ARRAY of pointers: new T^[n] -> T^[]. */
    int pa_a = 1;
    int pa_b = 2;
    int^[] parr = new int^[2];
    parr[0] = ^pa_a;
    parr[1] = ^pa_b;
    int pa_sum = parr[0]^ + parr[1]^;
    __println("ptr array= " + pa_sum);               // 3
    delete parr;

    /* a TUPLE element: new (T1,T2) -> (T1,T2)^ (disambiguated from placement). */
    (int,int)^ tup = new (int,int);
    tup^ = (10, 20);
    (int tu0, int tu1) = tup^;
    int tu_sum = tu0 + tu1;
    __println("tuple single= " + tu_sum);            // 30
    delete tup;

    /* a padded TUPLE array: (int8,int) lays out as {i8, pad, i32} — the alloc size
       is the LLVM-default struct size, so element addressing lands on the right
       stride. */
    (int8,int)[] tarr = new (int8,int)[3];
    tarr[0] = (1, 100);
    tarr[2] = (3, 300);
    (int8 tp_a, int tp_b) = tarr[2];
    int tp_sum = tp_a + tp_b;
    __println("tuple array= " + tp_sum);             // 303
    delete tarr;

    /* a GROUPED type: new (const int)^ -> a pointer to const int. */
    int cp_v = 7;
    (const int)^ ^ cpp = new (const int)^;
    cpp^ = ^cp_v;
    int cp_r = cpp^^;
    __println("const ptr= " + cp_r);                 // 7
    delete cpp;

    /* MULTI-DIM: new T[n][d]... — the FIRST dim is the runtime count, the rest are
       the element's dims (n copies of int[2][2]). */
    md = new int[2][2][2];
    md[0][0][0] = 1;
    md[1][1][1] = 8;
    int md_sum = md[0][0][0] + md[1][1][1];
    __println("multidim= " + md_sum);                // 9
    delete md;

    /* compile errors — each uncommented in isolation by the negative runner. */

    //-EXPECT-ERROR: Cannot allocate 'void'
    //void^ nvp = new void;
    //__println("x= " + (nvp == nullptr));

    //-EXPECT-ERROR: A placement address must be a buffer-class pointer
    //int yy = 0;
    //int^ ypp = ^yy;
    //int[] qp = new(ypp) int[2];
    //qp[0] = 1;
    //__println("x= " + qp[0]);

    //-EXPECT-ERROR: Cannot delete a non-pointer value of type 'int32'
    //int32 nint = 0;
    //delete nint;

    /* deleting a deref whose pointee is NOT a pointer is a non-pointer delete (delete
       now takes any pointer EXPRESSION, so the restriction is the type, not the form). */
    //-EXPECT-ERROR: Cannot delete a non-pointer value of type 'int'
    //int^ dp = new int;
    //delete dp^;

    /* a non-integer array size (bool is integer-class and would be accepted). */
    //-EXPECT-ERROR: An array size must be an integer
    //float32 fsz = 2.0;
    //int[] fp = new int[fsz];
    //fp[0] = 1;
    //__println("x= " + fp[0]);

    /* an unknown element type. */
    //-EXPECT-ERROR: Unknown type 'Bogus'
    //int^ bp = new Bogus;
    //bp^ = 1;
    //__println("x= " + bp^);

    /* void has no size in the array form either. */
    //-EXPECT-ERROR: Cannot allocate 'void'
    //void^ wp = new void[4];
    //__println("x= " + (wp == nullptr));

    /* constructor args belong to a class, not a primitive. */
    //-EXPECT-ERROR: Only a class takes constructor arguments
    //int^ cp = new int(5);
    //__println("x= " + cp^);

    /* an array allocation default-constructs its elements — no ctor args. */
    //-EXPECT-ERROR: An array allocation cannot take constructor arguments
    //ap = new Simple[2](1);
    //delete ap;

    /* an explicit destructor call needs a class receiver. */
    //-EXPECT-ERROR: A destructor call '.~()' requires a class object
    //int32 ni = 0;
    //ni.~();

    /* a TUPLE element must be statically sized: a slot that is a class (runtime-sized
       via __$sizeof) leaves the whole tuple unsized, so it is not allocatable. */
    //-EXPECT-ERROR: Cannot allocate '(Simple, int)'
    //ts = new (Simple, int);
    //delete ts;

    /* only the LEADING count dim may be dynamic; a trailing ELEMENT dim is a type dim
       and must be a plain literal (a runtime / const-expression dim is not folded). */
    //-EXPECT-ERROR: A 'new' element array dimension must be a constant
    //int edn = 2;
    //ed = new int[3][edn];
    //delete ed;

    return 0;
}
