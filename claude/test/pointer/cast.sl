/*
test casting of pointers.

the term cast is exclusive to pointers.
casting a pointer reinterprets what is at the address.
it does not change the object.

iterators and references are pointers.

    Type^ tgt = <Type^> src;
    Type[] tgt = <Type[]> src;

assume these named references have these types.

    void^ voidp;
    int8^ int8p;
    uint8^ uint8p;
    Type^ ref;
    Type[] iter;
    OtherType^ other;
    const Type^ constp;
    Type^ mutp;
    intptr num;
    Type arr[5];

implicit casts are the widening rules of pointers.
you may lose information.
but you may not add information.

nullptr has flex type.
any type pointer can be set to nullptr.
a pointer of type void^ may be set to any type pointer.

    ref = nullptr;
    iter = nullptr;
    voidp = ref;
    voidp = iter;

iterators can be demoted to references of the same type.

    iter = nullptr;
    ref = iter;

mutable pointers can become const pointers.

    constp = mutp;

arrays may be demoted to iterators or references of the same type.
in this case, array means the address of the stored data.
so it's like a pointer.

    iter = arr;
    ref = arr;

implicit casts may be made explicit.

    ref = <Type^> nullptr;
    iter = <Type[]> nullptr;
    ref = <Type^> iter;
    voidp = <void^> ref;
    voidp = <void^> iter;
    constp = <const Type^> mutp;
    iter = <Type[]> arr;
    ref = <Type^> arr;

you may cast a buffer-class pointer to a pointer of any type.
you may cast a pointer of any type to a buffer-class pointer.
you may not cast a non-buffer-class pointer directly to a different
non-buffer-class pointer.
buffer-class pointers are void^, int8^, uint8^.

all other casts are explicit.
they add information.

    ref = <Type^> voidp;
    ref = <Type^> int8p;
    ref = <Type^> uint8p;

casts may be chained.
this is how you indirectly cast a non-buffer-class pointer to a
different non-buffer-class pointer.

    ref = <Type^> <void^> other;
    ref = <Type^> <int8^> other;
    ref = <Type^> <uint8^> other;

a cast may directly add or remove const.

    constp = <const Type^> mutp;
    mutp = <Type^> constp;

<const> and <mutable> may be used to add or remove const from
pointers of the same type.

    constp = <const> mutp;
    mutp = <mutable> constp;

pointers may be implicitly cast to integers of type intptr.
integers of type intptr may be explicitly cast to pointers.
no other integer type may be cast to or from pointers.

    num = ref;
    num = iter;
    num = <intptr> ref;
    num = <intptr> iter;
    ref = <Type^> num;
    iter = <Type[]> num;

pointers of type void must be references.
void has no stride.
it cannot be an iterator.
void[] is a compile error.

you may not cast a pointer to an array.
in this case, array means the stored data - not the address of the
stored data.
Type[N] is not a pointer type. it cannot be used in a cast expression.
Type[N]^ is a pointer type. it may be used in a cast expression.
Type[N][] is a pointer type. it may be used in a cast expression.
no cast is needed for the reference because that is exactly the
type of ^arr.
an explicity cast is need to reinterpret a reference to an array to
an iterator to an array.

    Type[5]^ arr_ref = ^arr;
    Type[5][] arr_iter = <Type[5][]> ^arr;

notes:
pointers to class hierarchies will be written when the feature lands.
see class/inheritance.sl.
*/

/*
claude says:

- `<Type^> operand` is a pointer reinterpret cast: a prefix unary (precedence
  3), so chained casts `<A^> <void^> x` nest right-to-left. it parses wherever
  an expression may begin; a leading `<` is unambiguous, since binary `<` only
  appears between operands. the address is unchanged — under opaque `ptr` a
  pointer-to-pointer cast emits no instruction; only the `intptr` boundary emits
  a `ptrtoint` / `inttoptr`.
- the rules are enforced in classify. an IMPLICIT cast (a bare assignment) may
  only strip information: nullptr -> any pointer, any pointer -> `void^` or
  `intptr`, and an iterator demoted to a reference of the same pointee. an
  EXPLICIT cast additionally bridges through a buffer-class pointer (`void^`,
  `int8^`, `uint8^`) or `intptr`, and reinterprets an iterator <-> a reference
  of the same pointee. two unrelated non-buffer pointers may not cast directly —
  chain through `void^`. only `intptr` bridges pointers and integers.
- an ARRAY decays to a pointer (it is storage, addressed as `^arr[0]`): a bare
  array implicitly casts to the ELEMENT pointer `Type[]` / `Type^` (size dropped),
  and `<Type[]> arr` / `<Type^> arr` is the explicit form. classify rewrites the
  array to `^arr[0]` (an address-of), so codegen needs no special case. The
  WHOLE-array ref `Type[N]^` is NOT implicit from a bare array — write `^arr`
  (address-of, exactly its type; `int[5]^ r = arr` errors). As a function ARGUMENT
  the whole-array ref is a convenience (`fn(arr)` -> `fn(^arr)`); an array arg that
  matches BOTH an element-pointer and a whole-array-ref param is Ambiguous (both
  cost-1 decays — disambiguate with `^arr[0]` / `^arr`). `Type[N]` itself is not a
  pointer type, so it is never a cast target/endpoint.
- `void` has no stride: a void pointer must be a reference (`void^`). `void[]`
  (a void iterator/array) is a compile error.
- DEFERRED to phase 6 (const correctness): `const Type^`, `<const>`, `<mutable>`,
  and the mutable<->const cast lines above. const is not yet a tracked pointer
  qualifier, so those rules and their test cases are held until const lands.
*/

int32 main() {
    int32 a = 99;
    int32^ ref = ^a;

    /* nullptr flexes into any pointer type (implicit). */
    int32^ rnull = nullptr;
    void^  vnull = nullptr;
    __println("rnull is null= " + (rnull == nullptr));   // true
    __println("vnull is null= " + (vnull == nullptr));   // true

    /* any pointer -> void^ (implicit strip); void^ -> typed (explicit add). */
    void^ vp = ref;
    int32^ back = <int32^> vp;
    __println("void round-trip= " + back^);              // 99

    /* pointer <-> intptr. ptr->intptr implicit; intptr->ptr explicit. */
    intptr num = ref;
    int32^ fromnum = <int32^> num;
    __println("intptr round-trip= " + fromnum^);         // 99

    /* the explicit form of the implicit ptr->intptr. */
    intptr num2 = <intptr> ref;
    __println("same address= " + (num == num2));         // true

    /* iterator -> reference of the same pointee (implicit demote). */
    int32 buf[4];
    buf[0] = 7;
    int32[] it = ^buf[0];
    int32^ eref = it;
    __println("demote= " + eref^);                       // 7

    /* reference -> iterator of the same pointee (explicit add). */
    int32[] it2 = <int32[]> eref;
    __println("promote= " + it2^);                       // 7

    /* a const-EXPRESSION dim in a CAST target type — a pointer to a tuple whose
       slot is `int[kCast]` (`int32[N]^` array-then-`^` isn't a type; a tuple slot
       is how a const dim rides a pointer target). Folded + baked onto the cast
       node; the address round-trips. */
    const int kCast = 3;
    void^ avp = ^a;
    void^ aback = <(int32[kCast], int32)^> avp;          // target slot has a const dim
    __println("castN= " + (avp == aback));               // true

    /* buffer-class bridges any two pointers (explicit both ways). */
    int16 h = 1234;
    int16^ hp = ^h;
    int8^  raw = <int8^> hp;
    int16^ hp2 = <int16^> raw;
    __println("buffer round-trip= " + hp2^);             // 1234

    /* two unrelated non-buffer pointers reinterpret by chaining via void^. */
    int32^ cp = <int32^> <void^> ref;
    __println("chain= " + cp^);                          // 99

    /* a store into a pointer-typed slot obeys the same rules: an element of a
       references array, or a deref. */
    int32^ refs[2];
    refs[0] = ref;
    refs[1] = ^a;
    __println("ref array= " + refs[1]^);                 // 99

    /* iterators are pointers too — they strip implicitly, same as references. */
    buf[1] = 8;
    int32[] it1   = ^buf[1];
    void^   viter = it1;                 // iterator -> void^ (implicit strip)
    intptr  inum  = it1;                 // iterator -> intptr (implicit strip)
    int32[] inull = nullptr;             // nullptr -> iterator (implicit)
    __println("iter is null= " + (inull == nullptr));    // true

    /* explicit iterator casts: <intptr> iter, intptr -> iter, iter -> ref. */
    intptr  xnum  = <intptr> it1;
    int32[] fromi = <int32[]> xnum;      // intptr -> iterator (explicit)
    int32^  iref  = <int32^> it1;        // iterator -> reference (explicit)
    int32[] backit = <int32[]> viter;    // void^ -> iterator (buffer -> any)
    __println("iter explicit= " + fromi^);               // 8
    __println("iter->ref= " + iref^);                    // 8
    __println("void iter round-trip= " + backit^);       // 8
    __println("iter addr= " + (inum == xnum));           // true

    /* uint8^ is a buffer-class pointer, like int8^. */
    uint8^ up    = <uint8^> ref;         // any -> uint8 buffer
    int32^ fromu = <int32^> up;          // uint8 buffer -> any
    __println("uint8 round-trip= " + fromu^);            // 99

    /* bridge two UNRELATED non-buffer pointers (int16^ <-> int32^) by chaining
       through a buffer-class type. the reinterpreted value is meaningless, so
       verify the ADDRESS survives the round trip. */
    int32^ bvoid = <int32^> <void^> hp;  // via void^
    int32^ bu8   = <int32^> <uint8^> hp; // via uint8^
    intptr ha    = <intptr> hp;
    __println("bridge void= " + (<intptr> bvoid == ha)); // true
    __println("bridge uint8= " + (<intptr> bu8 == ha));  // true

    /* a cast in a larger expression (operand of ==), and a cast of a complex
       operand (an `^arr[i]` iterator, not a bare variable). */
    __println("expr cast= " + (<intptr> ref == num));    // true
    void^   cv = <void^> ^buf[1];
    int32[] ci = <int32[]> cv;
    __println("complex operand= " + ci^);                // 8

    /* a cast TO an ALIASED pointer type keeps the alias transparent: deref the
       cast result directly, so the cast expression's OWN type drives the load. (A
       spelling round-trip would clobber 'Pint' to an unknown slid and mis-lower
       the pointee.) */
    alias Pint = int32;
    int32 av = 77;
    int32^ ap = ^av;
    __println("alias cast= " + (<Pint^> ap)^);           // 77

    /* an ARRAY decays to a pointer — it is storage, addressed as ^arr[0]. A bare
       array implicitly casts to the ELEMENT pointer (size dropped); the explicit
       <Type[]> / <Type^> form casts the same address; the WHOLE-array ref keeps the
       size and is spelled ^arr (address-of, no cast). */
    int32 darr[5] = (10, 20, 30, 40, 50);
    int32[] dq = darr;                                   // implicit -> ^darr[0]
    int32^  dp = darr;                                   // implicit element-0 ref
    __println("decay iter= " + dq^ + " " + dq[4]);       // 10 50
    __println("decay ref= " + dp^);                      // 10
    int32[] dqx = <int32[]> darr;                        // explicit element decay
    int32^  dpx = <int32^> darr;
    __println("decay cast= " + dqx^ + " " + dpx^);       // 10 10
    int32[5]^ dwhole = ^darr;                            // whole-array ref: ^arr, no cast
    __println("decay whole= " + dwhole^[0] + " " + dwhole^[4]);   // 10 50

    /* compile errors — each uncommented in isolation by the negative runner. */

    //-EXPECT-ERROR: Cannot implicitly cast 'void^' to 'int32^'
    //ref = vp;

    //-EXPECT-ERROR: Cannot implicitly cast 'intptr' to 'int32^'
    //ref = num;

    //-EXPECT-ERROR: Cannot implicitly cast 'int32^' to 'int16^'
    //hp = ref;

    //-EXPECT-ERROR: Cannot implicitly cast 'int32^' to 'int32[]'
    //it = ref;

    //-EXPECT-ERROR: reinterpret indirectly through 'void^'
    //cp = <int32^> hp;

    //-EXPECT-ERROR: only 'intptr' may be cast to or from a pointer
    //ref = <int32^> a;

    /* the TARGET branch of the same rule: a non-intptr integer target. */
    //-EXPECT-ERROR: only 'intptr' may be cast to or from a pointer
    //num = <int16> ref;

    /* a dereference yields a value, not a pointer (a complex operand). */
    //-EXPECT-ERROR: only 'intptr' may be cast to or from a pointer
    //num = <int32^> ref^;

    /* an ARRAY decays to a pointer, so it IS a valid cast operand now: the element
       decay `<int32^> buf` == `<int32^> ^buf[0]` (verified above via dqx / dpx).
       The WHOLE-array ref, though, is NOT implicit from a bare array — spell `^arr`;
       a bare array to `int32[5]^` is a compile error. */
    //-EXPECT-ERROR: Cannot assign 'int32[5]' to 'int32[5]^'
    //int32[5]^ badwhole = darr;
    //__println("bad= " + badwhole^[0]);

    //-EXPECT-ERROR: A void pointer must be a reference 'void^'
    //void[] bad = nullptr;

    /* void as an array (not just an iterator) is rejected the same way. */
    //-EXPECT-ERROR: A void pointer must be a reference 'void^'
    //void bad2[3];

    /* a store into a pointer slot is checked too (deref and index). */
    //-EXPECT-ERROR: Cannot implicitly cast 'int16^' to 'int32^'
    //refs[0] = hp;

    //-EXPECT-ERROR: Cannot implicitly cast 'int16^' to 'int32'
    //ref^ = hp;

    return 0;
}
