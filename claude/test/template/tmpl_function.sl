/*
test function templates.

definition:

    return-type function-name < template-list > ( parameter-list ) { body }

template-list is a comma separate list of identifiers used as types.
the return type maybe a type from the template-list.
parameter-list is a normal comma separated list of parameters.
the parameters may use types from the template-list.

usage:

    function-name < type-list > ( argument-list )
    function-name ( argument-list )

the type-list may be inferred from the argument-list.
*/

/*
claude says:

the template body is held unresolved until a call binds the type-list; each
distinct binding is cloned, run through the normal pipeline in the template's
own scope, and memoized (one instance per binding). instance symbols carry the
itanium I..E type-arg encoding.

inference's one job is finding the T bindings: T binds an argument's type
exactly (a literal binds the type a typeless `x = literal` would; conflicting
bindings error). the NOT-template parts of a pattern match by the normal
parameter rules — a t-free subtree imposes no constraint at inference, and on
the way to a T the normal shape conversions apply (array decay, rvalue
materialization into a reference). after binding (inferred or explicit) the
call is an ordinary single-candidate call: widening / implicit casts / class
coercion apply as spec.

a template owns its name: colliding with any same-name function or template in
the same scope is a compile error (overload participation deferred). templates
declare anywhere a function does — file scope, namespaces, blocks.

deferred: nested template types (the >> closer split), overloads, methods,
classes, cross-tu. alias templates landed: see tmpl_alias.sl.
*/

T add<T>(T a, T b) {
    return a + b;
}

/* two type parameters; mixed arithmetic inside the instance widens as usual. */
T addu<T, U>(T a, U b) {
    return a + b;
}

/* T in a composite position: pointer to T. */
void bump<T>(T^ p) {
    p^ = p^ + 1;
}

/* T in a composite position: iterator over T. an array argument reaches it via
   the normal decay, explicit or inferred. */
T sum3<T>(T[] p) {
    return p[0] + p[1] + p[2];
}

/* a template calling a template: instantiating triple<int> discovers add<int>. */
T triple<T>(T a) {
    return add(a, add(a, a));
}

/* T appears in no parameter: only callable with an explicit type-list. */
T make<T>() {
    T r = 77;
    return r;
}

/* a template may not share its name with a plain function. */
//-EXPECT-ERROR: may not share its name
//int clash(int a) { return a; }
//T clash<T>(T v) { return v; }

/* two templates may not share a name (overloads deferred). */
//-EXPECT-ERROR: may not share its name
//T dup<T>(T v) { return v; }
//U dup<U>(U v) { return v; }

/* a mixed signature: T infers from the first argument; the concrete int64
   parameter is a not-template part — its argument converts normally. */
void zork<T>(T a, int64 b) {
    __println(a + " " + b);
}

/* T inferred THROUGH a tuple-shaped reference parameter: the `#` tuple is an
   rvalue (materializes into the ref), its char[] slots are the not-template
   parts (normal matching), and T binds through the value-pointer slot. */
void dump<T>( (char[], char[], char[], char[], T^)^ tuple) {
    __println(tuple^[0] + ":" + tuple^[1] + ": "
        + tuple^[2] + " " + tuple^[3] + " = " + tuple^[4]^);
}

/* a NAMESPACE-scope template: registered in the namespace frame, pattern
   resolved in the types phase, callable qualified (explicit or inferred). */
Space {
    T twice<T>(T v) { return v + v; }
}

/* T bound to a CLASS through reference parameters: the body dispatches the
   class operator and the result returns by value. (Binding a class through a
   BY-VALUE `T` errors like any class value parameter — negative below.) */
Pair(int x_ = 0, int y_ = 0) {
    op+(Pair^ p, Pair^ q) { x_ = p^.x_ + q^.x_; y_ = p^.y_ + q^.y_; }
}
T addc<T>(T^ a, T^ b) {
    return a^ + b^;
}

/* a template declared inside a METHOD body: its instances are nested functions
   in the method's scope, so the body may even read the enclosing object's
   FIELD through the ordinary field machinery. (In the class BODY itself a
   template is a template METHOD — rejected at parse, tmpl_method's item.) */
Tally(int c_ = 0) {
    int bump(int by) {
        T scale<T>(T v) { return v * 2; }
        T plus_c<T>(T v) { return v + c_; }
        c_ = plus_c(scale(by));
        return c_;
    }
}

/* self-recursion: the instance is memoized BEFORE its body resolves, so the
   recursive call inside lands on the very instance being built. */
T fact<T>(T n) {
    if (n <= 1) { return n; }
    return n * fact(n - 1);
}

/* a parameter default inside a template; an omitted argument fills it per
   instance. */
T pad<T>(T a, T b = 3) {
    return a + b;
}

/* the identity shape: T binds a pointer type, or a (substituted) const. */
T same<T>(T v) { return v; }

/* one argument binding T twice inside a composite: consistent bindings only. */
T sumpair<T>( (T, T)^ p ) { return p^[0] + p^[1]; }

/* T as the element of a sized-array parameter (the size rides the name). */
T sum3v<T>(T a[3]) { return a[0] + a[1] + a[2]; }

/* the body may ask compile-time questions of T. */
void meta<T>(T v) {
    __println("ty = " + ##type(v) + " sz = " + sizeof(T) + " v = " + v);
}

/* an uninstantiated template's body is never checked: nobody calls this, so it
   only has to PARSE (see todo — unchecked uninstantiated bodies). */
void nonsense<T>(T v) {
    v = totally + undefined * names;
}

/* aliases in an explicit type-list canonicalize: add<Integer> IS add<int>. */
alias Integer = int;

/* a plain function, for the type-args-on-a-non-template negative. */
int plain(int v) { return v; }

/* a file-scope function a block-scope template may SHADOW (collision is
   same-scope only; normal lexical shadowing across scopes). */
int shade(int v) { return v; }

/* duplicate type-parameter names. */
//-EXPECT-ERROR: Duplicate type-parameter name
//T bad2<T, T>(T v) { return v; }

/* a template's body IS the template — no forward declaration. */
//-EXPECT-ERROR: A template function must have a body
//T decl_only<T>(T v);

/* a template may not take a class's name either. */
//-EXPECT-ERROR: Duplicate declaration
//Klass(int k_ = 0) { }
//T Klass<T>(T v) { return v; }

/* a runaway recursive instantiation: every call needs a NEW binding (each level
   wraps another `^`), so instantiation never converges — the depth cap reports
   it. (`mutable` keeps the munge from const-qualifying each level differently,
   which would stop the chain early with a cast error instead.) */
//-EXPECT-ERROR: instantiation depth limit
//void deeper<T>(mutable T^ p) { deeper(^p); }
//void start_deeper() { int seed = 1; deeper(^seed); __println("seed = " + seed); }

int32 main() {

    int x = add<int>(1, 2); __println("x = " + x);
    float f = add<float>(1.9, 2.8); __println("f = " + f);

    /* inferred: a literal binds the type a typeless declaration would give. */
    int xi = add(3, 4); __println("xi = " + xi);
    float fi = add(1.5, 2.5); __println("fi = " + fi);

    /* inferred from typed lvalues; same instance as add<int> above (memoized). */
    int a = 10;
    int b = 20;
    int c = add(a, b); __println("c = " + c);

    /* explicit type-list; the argument widens into the bound parameter. */
    int8 i8 = 3;
    int w = add<int>(i8, 4); __println("w = " + w);

    /* two type parameters, both inferred. */
    int u = addu(a, i8); __println("u = " + u);

    /* pointer position: inferred and explicit. */
    bump(^a); __println("a = " + a);
    bump<int>(^b); __println("b = " + b);

    /* iterator position via array decay: explicit and inferred. */
    int arr[3] = (1, 2, 3);
    int s3 = sum3<int>(arr); __println("s3 = " + s3);
    int s4 = sum3(arr); __println("s4 = " + s4);

    /* a template instantiating another template. */
    int t = triple(5); __println("t = " + t);

    /* explicit-only: T appears in no parameter. */
    int m = make<int>(); __println("m = " + m);

    /* a template declared in a block, like any nested function. */
    T sq<T>(T v) { return v * v; }
    int s = sq(6); __println("s = " + s);
    int e = sq<int>(7); __println("e = " + e);

    int za = 62;
    zork("za = ", za);

    /* mixed signature: T from the first argument, the second widens normally. */
    zork(a, i8);

    int64 answer = 42;
    dump(#answer);

    /* namespace-scope template, inferred and explicit. */
    int nt = Space:twice(21); __println("nt = " + nt);
    int ne = Space:twice<int>(2); __println("ne = " + ne);

    /* T bound to a class; the instance runs the class operator. */
    Pair p1(3, 4);
    Pair p2(10, 20);
    Pair p3 = addc(^p1, ^p2);
    __println("p3 = " + p3.x_ + " " + p3.y_);

    /* templates inside a method body: memoized across calls, field readable. */
    Tally ty(10);
    __println("ty1 = " + ty.bump(3));
    __println("ty2 = " + ty.bump(4));

    /* self-recursion within one instance. */
    int fa = fact(5); __println("fa = " + fa);

    /* a template parameter default: omitted, then supplied. */
    int pd = pad(7); __println("pd = " + pd);
    int pe = pad(7, 8); __println("pe = " + pe);

    /* T bound to a pointer type; a substituted-const argument. */
    int^ pi = ^a;
    int^ qi = same(pi); __println("qi = " + qi^);
    const int ci = 5;
    int cv = same(ci); __println("cv = " + cv);

    /* one argument binding T twice, consistently. */
    int sp2 = sumpair((4, 5)); __println("sp2 = " + sp2);

    /* T as the element of a sized-array parameter. */
    int s5 = sum3v(arr); __println("s5 = " + s5);

    /* compile-time questions of T inside the body. */
    meta(za);

    /* an alias type argument canonicalizes to its underlying instance. */
    int ai = add<Integer>(21, 21); __println("ai = " + ai);

    /* an explicit iterator type argument. */
    zork<char[]>("hey", 9);

    /* a block template shadows the same-name file-scope function. */
    T shade<T>(T v) { return v + 100; }
    int sh = shade(1); __println("sh = " + sh);

    /* the gate rule's comparison side: parenthesized, `<` is a comparison. */
    bool lt = (a < b); __println("lt = " + lt);

    /* a block template CAPTURING an enclosing local: the instance is a nested
       function, so the capture machinery carries `base` in by reference. */
    int base = 1000;
    T offset<T>(T v) { return v + base; }
    int of = offset(7); __println("of = " + of);

    /* conflicting exact bindings for T. */
    //-EXPECT-ERROR: Conflicting bindings for template parameter
    //int32 n32 = 1;
    //int8 n8 = 2;
    //int bad = add(n32, n8); __println("bad = " + bad);

    /* nothing binds T. */
    //-EXPECT-ERROR: Cannot infer template parameter
    //int nope = make(); __println("nope = " + nope);

    /* more explicit type arguments than template parameters. */
    //-EXPECT-ERROR: Wrong number of template arguments
    //int extra = add<int, float>(1, 2); __println("extra = " + extra);

    /* too FEW explicit type arguments. */
    //-EXPECT-ERROR: Wrong number of template arguments
    //int few = addu<int>(1, 2); __println("few = " + few);

    /* an unknown type in an explicit type-list. */
    //-EXPECT-ERROR: Unknown type
    //int unk = add<Bogus>(1, 2); __println("unk = " + unk);

    /* type arguments on a non-template function. */
    //-EXPECT-ERROR: is not a template function
    //int np = plain<int>(1); __println("np = " + np);

    /* a structurally unmatchable argument: a pointer against the tuple shape. */
    //-EXPECT-ERROR: does not match the template pattern
    //dump(^answer);

    /* a conflict reached through a composite: both tuple slots bind T. */
    //-EXPECT-ERROR: Conflicting bindings for template parameter
    //int spx = sumpair((1, 2.5)); __println("spx = " + spx);

    /* the gate rule: bare `a < b > (c)` IS a template-call shape — the
       comparison reading requires parentheses. */
    //-EXPECT-ERROR: not a function
    //int amb = a < b > (0); __println("amb = " + amb);

    /* T may not bind a class through a BY-VALUE parameter: the instance obeys
       the normal rule that a class parameter must be a reference. */
    //-EXPECT-ERROR: must be a pointer
    //Pair px = add(p1, p2); __println("px = " + px.x_);

    return 0;
}
