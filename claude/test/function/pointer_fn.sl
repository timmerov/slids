/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
test pointers to functions.

    bool less(int a, int b) {
        return (a >= b);
    }

explicit type declaration:
types must match exactly - allowing for aliases.
char does not match uint8.
a function is syntactically an object.
a pointer to a function follows the syntax for references to objects.

    bool fn(int,int)^ = ^less;

inferred type:
only allowed when the function has no overloads.
pointers to overloaded functions cannot be inferred.
they must use explicit types.

    fn = ^less;  // bool(int,int)^

the parameter list for function pointers follows the same assumed
const pointer rules that apply to function parameters.

    void ptr_fn(int^)^ = ^some_ptr_fn; // void((const int)^)^

function pointers may not have default parameter values.
there's no way to spell such a type.

    void not_valid(int, int=0)^; // compile error

function pointers may be nullptr.

    void fn()^ = nullptr;

pointers to functions may be const.
the parameters to a const function pointer may be mutable.

    const void const_fn(int)^ = ^some_fn;
    const (const char)[] const_gn(int)^ = ^some_gn;
    const void hybrid_fn(mutable int[])^ = ^some_hybrid;

syntax for arrays of function pointers:

    bool(int,int)^ fn_array[4];

calling the pointed-to function:

    b = fn^(1, 2);

passing the function pointer as parameter:

    void sort(mutable int arr[2], bool less_fn(int, int)^) {
        if (less_fn^(arr[1], arr[0])) {
            arr[0] <--> arr[1];
        }
    }

    sort(container, ^less);
    sort(container, fn);

function pointers are semantically references.
they may be used with == != ! && || ^^ operations.

function pointers may be cast to a generic pointer.
and vice versa.
this is allowed but use with caution.

    alias VoidFn = void(void^)^;
    alias TypeFn = void(Type^)^;
    VoidFn vfn;
    TypeFn tfn;
    void^ vp = <void^> vfn;
    int8^ gp = <int8^> tfn;
    vfn = <VoidFn> gp;
    tfn = <TypeFn> vp;

    void const_fn(Type^)^ = ^some_const_fn;
    mut_fn = <void(mutable Type^)^> <void^> const_fn;

notes:

nested functions may be pointed to if they do not capture
any local variables.

pointers to template functions are currently not allowed.
deferred.

you may create a pointer to an imported C function or alias.

pointers to methods is tbd.
and won't be covered in this file.
*/

/*
claude says:

- the type is a new form, a function SIGNATURE `R(P...)` (widen kFunction), only
  ever behind a `^`. its params are stored MUNGED (widen::mungeParam, the same rule
  as a definition's params), so `int(int^)^` IS `int((const int)^)^`; a param the
  munge leaves alone spells `mutable`. a const return parenthesizes:
  `(const int)(int)^`.
- grammar: a `(...)` group right before `^` / `^^` in a type position is a
  signature (fnSigClose). the named form `bool fn(int,int)^` moves the signature
  after the name. a postfix `^` directly followed by `(` is a call through the
  pointer (kIndirectCallExpr) — so `a ^ (b)` is no longer XOR; write `(b) ^ a`.
- `^fn`: resolve admits a function name (not a template, not a method). classify
  picks the overload: a function-pointer target picks the exact signature; with no
  such target only a lone function infers. a capturing nested function is rejected.
- equality: deepStrip of a signature keeps const (aliases aside), so the munged
  const is part of the type — `int(mutable int^)^` and `int(int^)^` differ.
- `^fn` is constant data: a global, a field default, a param default may use it.
- lowering: `^fn` -> the function's symbol; `fp^(args)` -> an ast call flagged
  `indirect` whose callee value rides params[0]. mangling: Itanium `F<ret><params>E`
  with const kept.
*/

import string;
alias slids;

bool less(int a, int b) {
    return (a >= b);
}

bool greater(int a, int b) {
    return (a < b);
}

int add(int a, int b) {
    return a + b;
}

int peek(int^ p) {
    return p^;
}

void bump(mutable int^ p) {
    p^ += 1;
}

uint8 u8id(uint8 x) {
    return x;
}

(const char)[] name_of(int which) {
    if (which == 0) {
        return "zero";
    }
    return "other";
}

/* overloads: the declared pointer type picks one. */
int twice(int x) {
    return 2 * x;
}

float64 twice(float64 x) {
    return 2.0 * x;
}

/* a template function: pointers to it are deferred (negative below). */
T ident<T>(T x) {
    return x;
}

void sort(mutable int arr[2], bool less_fn(int, int)^) {
    if (less_fn^(arr[1], arr[0])) {
        arr[0] <--> arr[1];
    }
}

int apply1(int f(int)^, int x) {
    return f^(x);
}

/* a function's address is constant data: a parameter default, a global. */
int apply2(int x, int y, int f(int, int)^ = ^add) {
    return f^(x, y);
}

bool g_cmp(int, int)^ = ^greater;

/* a function returning a function pointer, spelled with the nameless type. */
bool(int, int)^ pick(bool want_less) {
    if (want_less) {
        return ^less;
    }
    return ^greater;
}

Counter(int n_ = 0) {
}

void count_up(mutable Counter^ c) {
    c^.n_ += 1;
}

int count_of(Counter^ c) {
    return c^.n_;
}

/* a class field holding a function pointer, called from a method. */
Holder(int(int, int)^ op_ = nullptr) {
    int apply(int a, int b) {
        return op_^(a, b);
    }
}

/* a field default may name a function. */
Adder(int(int, int)^ op_ = ^add) {
}

/* an imported C function and an alias of it. */
c_lib {
    int abs(int x) = import;
    alias my_abs = abs;
}

alias VoidFn = void(void^)^;

int32 main() {
    /* explicit type declaration. */
    bool fn(int,int)^ = ^less;
    println(String + "fn^(1, 2)= " + fn^(1, 2));                     // fn^(1, 2)= false
    println(String + "fn^(2, 1)= " + fn^(2, 1));                     // fn^(2, 1)= true
    println(String + "##type(fn)= " + ##type(fn));                   // ##type(fn)= bool(int, int)^

    /* inferred type; reassignment. */
    fn2 = ^less;
    println(String + "##type(fn2)= " + ##type(fn2));                 // ##type(fn2)= bool(int, int)^
    fn2 = ^greater;
    println(String + "fn2^(1, 2)= " + fn2^(1, 2));                   // fn2^(1, 2)= true

    /* the parameter list follows the assumed-const pointer rules. */
    int v = 41;
    int ptr_fn(int^)^ = ^peek;
    println(String + "##type(ptr_fn)= " + ##type(ptr_fn));           // ##type(ptr_fn)= int((const int)^)^
    println(String + "ptr_fn^(^v)= " + ptr_fn^(^v));                 // ptr_fn^(^v)= 41
    void mut_fn(mutable int^)^ = ^bump;
    println(String + "##type(mut_fn)= " + ##type(mut_fn));           // ##type(mut_fn)= void(mutable int^)^
    mut_fn^(^v);
    println(String + "v= " + v);                                     // v= 42

    /* nullptr, comparisons, conditions. */
    void none_fn()^ = nullptr;
    println(String + "!none_fn= " + !none_fn);                       // !none_fn= true
    println(String + "!fn= " + !fn);                                 // !fn= false
    println(String + "none_fn == nullptr= " + (none_fn == nullptr)); // none_fn == nullptr= true
    println(String + "fn == fn2= " + (fn == fn2));                   // fn == fn2= false
    println(String + "fn != fn2= " + (fn != fn2));                   // fn != fn2= true
    println(String + "fn == ^less= " + (fn == ^less));               // fn == ^less= true
    println(String + "fn && none_fn= " + (fn && none_fn));           // fn && none_fn= false
    println(String + "fn || none_fn= " + (fn || none_fn));           // fn || none_fn= true
    println(String + "fn ^^ none_fn= " + (fn ^^ none_fn));           // fn ^^ none_fn= true
    if (fn) {
        println("fn is set");                                        // fn is set
    }
    if (!none_fn) {
        println("none_fn is null");                                  // none_fn is null
    }

    /* const function pointers; the parameters may still be mutable. */
    const bool const_fn(int,int)^ = ^less;
    println(String + "##type(const_fn)= " + ##type(const_fn));       // ##type(const_fn)= const bool(int, int)^
    println(String + "const_fn^(3, 3)= " + const_fn^(3, 3));         // const_fn^(3, 3)= true
    const (const char)[] const_gn(int)^ = ^name_of;
    println(String + "const_gn^(0)= " + const_gn^(0));               // const_gn^(0)= zero
    println(String + "const_gn^(1)= " + const_gn^(1));               // const_gn^(1)= other
    const void hybrid_fn(mutable int^)^ = ^bump;
    hybrid_fn^(^v);
    println(String + "v= " + v);                                     // v= 43

    /* arrays of function pointers. */
    bool(int,int)^ fn_array[2];
    fn_array[0] = ^less;
    fn_array[1] = ^greater;
    for (i : 0..2) {
        println(String + "fn_array[" + i + "]^(1, 2)= " + fn_array[i]^(1, 2));
    }                                                                // fn_array[0]^(1, 2)= false
                                                                     // fn_array[1]^(1, 2)= true

    /* passing a function pointer as a parameter. */
    int pair[2] = (1, 2);
    sort(pair, ^less);
    println(String + "pair= " + pair[0] + "," + pair[1]);            // pair= 2,1
    sort(pair, fn2);
    println(String + "pair= " + pair[0] + "," + pair[1]);            // pair= 1,2
    println(String + "apply1(^twice, 4)= " + apply1(^twice, 4));     // apply1(^twice, 4)= 8

    /* returning a function pointer; calling the result. */
    println(String + "pick(true)^(3, 4)= " + pick(true)^(3, 4));     // pick(true)^(3, 4)= false
    println(String + "pick(false)^(3, 4)= " + pick(false)^(3, 4));   // pick(false)^(3, 4)= true

    /* an overloaded function needs an explicit type. */
    int tw_i(int)^ = ^twice;
    float64 tw_f(float64)^ = ^twice;
    println(String + "tw_i^(3)= " + tw_i^(3));                       // tw_i^(3)= 6
    println(String + "tw_f^(1.5)= " + tw_f^(1.5));                   // tw_f^(1.5)= 3
    println(String + "ident(7)= " + ident(7));                       // ident(7)= 7

    /* class parameters. */
    Counter c;
    void cup(mutable Counter^)^ = ^count_up;
    cup^(^c);
    cup^(c);
    int cof(Counter^)^ = ^count_of;
    println(String + "##type(cof)= " + ##type(cof));                 // ##type(cof)= int((const Counter)^)^
    println(String + "cof^(c)= " + cof^(c));                         // cof^(c)= 2

    /* a function pointer field. */
    Holder h;
    h.op_ = ^add;
    println(String + "h.op_^(2, 3)= " + h.op_^(2, 3));               // h.op_^(2, 3)= 5
    println(String + "h.apply(4, 5)= " + h.apply(4, 5));             // h.apply(4, 5)= 9
    Adder ad;
    println(String + "ad.op_^(6, 7)= " + ad.op_^(6, 7));             // ad.op_^(6, 7)= 13
    println(String + "apply2(2, 3)= " + apply2(2, 3));               // apply2(2, 3)= 5
    println(String + "g_cmp^(1, 2)= " + g_cmp^(1, 2));               // g_cmp^(1, 2)= true

    /* a nested function that captures nothing. */
    int square(int x) {
        return x * x;
    }
    int sq(int)^ = ^square;
    println(String + "sq^(5)= " + sq^(5));                           // sq^(5)= 25

    /* an imported C function and an alias of it. */
    int af(int)^ = ^c_lib:abs;
    int af2(int)^ = ^c_lib:my_abs;
    println(String + "af^(-3)= " + af^(-3));                         // af^(-3)= 3
    println(String + "af == af2= " + (af == af2));                   // af == af2= true

    /* casts to and from generic pointers. */
    VoidFn vfn = nullptr;
    println(String + "!vfn= " + !vfn);                               // !vfn= true
    void^ vp = <void^> fn;
    void^ vp2 = fn;
    println(String + "vp == vp2= " + (vp == vp2));                   // vp == vp2= true
    fn3 = <bool(int,int)^> vp;
    println(String + "fn3^(2, 1)= " + fn3^(2, 1));                   // fn3^(2, 1)= true
    int8^ gp = <int8^> fn;
    bool fn4(int,int)^ = <bool(int,int)^> gp;
    println(String + "fn4 == fn= " + (fn4 == fn));                   // fn4 == fn= true
    int const_peek(int^)^ = ^peek;
    int mut_peek(mutable int^)^ = <int(mutable int^)^> <void^> const_peek;
    println(String + "mut_peek^(^v)= " + mut_peek^(^v));             // mut_peek^(^v)= 43

    /* a call statement discards the result. */
    fn_array[0]^(1, 2);
    mut_fn^(^v);
    println(String + "v= " + v);                                     // v= 44

/* a function pointer type may not carry default values. */
//-EXPECT-ERROR: Function pointers may not have default parameter values.
//    void not_valid(int, int=0)^ = nullptr;

/* a pointer to an overloaded function cannot be inferred. */
//-EXPECT-ERROR: Cannot infer a pointer to overloaded function 'twice'
//    tw = ^twice;
//    println(String + !tw);

/* the pointer must be dereferenced to call it. */
//-EXPECT-ERROR: is a function pointer; call it as 'fn^(args)'
//    bool b1 = fn(1, 2);

/* a dereferenced function pointer is only callable. */
//-EXPECT-ERROR: A dereferenced function pointer must be called
//    b2 = fn^;
//    println(String + b2);

/* a nested function that captures a local cannot be pointed to. */
//-EXPECT-ERROR: captures local variables
//    int base = 10;
//    int add_base(int x) {
//        return x + base;
//    }
//    int ab(int)^ = ^add_base;
//    println(String + ab^(1));

/* pointers to template functions are deferred. */
//-EXPECT-ERROR: Pointers to template functions are not supported
//    int tf(int)^ = ^ident;

/* types must match exactly. */
//-EXPECT-ERROR: Cannot implicitly cast 'bool(int, int)^' to 'bool(int)^'
//    bool wrong(int)^ = ^less;
//    println(String + !wrong);

/* char does not match uint8. */
//-EXPECT-ERROR: Cannot implicitly cast 'uint8(uint8)^' to 'char(char)^'
//    char cf(char)^ = ^u8id;
//    println(String + !cf);

/* the munged const is part of the type. */
//-EXPECT-ERROR: Cannot implicitly cast 'int((const int)^)^' to 'int(mutable int^)^'
//    int mp(mutable int^)^ = ^peek;
//    println(String + !mp);

/* a const function pointer cannot be reassigned. */
//-EXPECT-ERROR: Cannot write to a const value
//    const_fn = ^greater;

/* the argument count must match. */
//-EXPECT-ERROR: Wrong number of arguments
//    fn^(1);

/* the argument types must convert. */
//-EXPECT-ERROR: Cannot assign 'const char[2]' to 'int'.
//    fn^(1, "x");

/* only == != ! && || ^^ apply. */
//-EXPECT-ERROR: is not defined on function pointers
//    bool lt = fn < fn2;
//    println(String + lt);

/* no pointer arithmetic. */
//-EXPECT-ERROR: Arithmetic is not allowed
//    fn5 = fn + 1;
//    println(String + !fn5);

/* only a function pointer can be called. */
//-EXPECT-ERROR: is not a function pointer
//    int k = 5;
//    k^(1);

/* a class value is not a parameter type. */
//-EXPECT-ERROR: A non-primitive parameter must be a pointer
//    void bad(Counter)^ = nullptr;

/* a generic pointer needs an explicit cast to become a function pointer. */
//-EXPECT-ERROR: Cannot implicitly cast 'void^' to 'bool(int, int)^'
//    bool f6(int,int)^ = vp;
//    println(String + !f6);

    return 0;
}
