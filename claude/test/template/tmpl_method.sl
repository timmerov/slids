/*
test method templates.

definition:

    return-type metod-name < template-list > ( parameter-list ) { body }

template-list is a comma separate list of identifiers used as types.
the return type maybe a type from the template-list.
parameter-list is a normal comma separated list of parameters.
the parameters may use types from the template-list.

usage:

    function-name < type-list > ( argument-list )
    function-name ( argument-list )

the type-list may be inferred from the argument-list.

special handling of template type parameters.
a template method needs to be able to handle primitive and not-primitive types.
a convention of convenience applies here: a parameter of template type is
passed by value if the template type is a primitive.
otherwise it's passed by reference to const.
this convention applies to all template methods declared in all class flavors
including: plain, hoisted, derived, virtual, template, etc.

    void template_method<T>(T arg);

    template_method<int>(42);  -->
        void template_method(int arg);

    Class(int a_) { }
    Class obj(37);
    template_method<Class>(obj);  -->
        void template_method(Class^ arg);

    alias Tuple = (int,int);
    Tuple t = (1,2);
    template_method<Tuple>(t);  -->
        void template_method(Tuple^ arg);

the template body must rewrite every usage of the argument when it is converted
to a reference.

    void template_method<T>(T arg) {
        __println(arg);
    }

transforms - when T is not-primitive - to:

    void template_method<T>(T^ arg) {
        __println(arg^);
    }
*/

/*
claude says:

a template method rides the function-template machinery whole: the same
pristine body + pattern signature (behind the implicit receiver slot) + scope
snapshot, the same classify-time instantiation, memoized per binding — one
instance serves every object. binding: an explicit type-list, or unification
of the USER arguments against the patterns past the receiver; the retargeted
call then rides the ordinary method path (defaults, conversions, coercion).
the body is a method body: bare fields, self, and sibling calls resolve
normally.

a template method owns its name within its class (no overload set, either
direction); virtual is rejected (instances are unbounded, vtable slots are
not); the out-of-line form (`T Class:m<T>`) landed with cross-TU templates —
it relocates into the class like any external member, adding (or, given a
header declaration, defining) a member template.
*/

/* the workhorse: explicit + inferred calls, field read/write, memoization. */
Jar(int n_ = 0) {
    T scaled<T>(T v) { return v * n_; }
    void grow<T>(T by) { n_ = n_ + by; }
    /* a template method calling a bare SIBLING template, a plain sibling, a
       FREE template, and the explicit `self.` form with its OWN T as the arg. */
    int half(int v) { return v / 2; }
    T mix<T>(T v) {
        return free_id(half(scaled(v)) + self.scaled<T>(1));
    }
    /* T in a composite position, plus a template-typed default. */
    T sum2<T>(T[] p, T extra = 3) { return p[0] + p[1] + extra; }
    /* T in no parameter: explicit-only. */
    T fresh<T>() { T r = 9; return r; }
    /* T bound to a CLASS through references; the instance returns the class by
       value (sret through an instance method). */
    T comb<T>(T^ a, T^ b) { return a^ + b^; }
    /* an alias-template use in a template method's signature. */
    T viaRf<T>(Rf<T> p) { return p^; }
    /* the convention of convenience: a bare-S param binding a CLASS — the
       instance's param is `(const S)^`; uses auto-deref. */
    S same<S>(S s) { return s; }
    int peek<S>(S s) { return s.x_ + n_; }
    /* method self-recursion: the memo is seeded before the body resolves. */
    T fact<T>(T n) {
        if (n <= 1) { return n; }
        return n * fact(n - 1);
    }
}

T free_id<T>(T v) { return v; }

/* the out-of-line member-template form: adds `extra` to Jar. */
T Jar:extra<T>(T v) { return v + v; }

alias Rf<T> = T^;

/* a class with an operator, for the class-bound T; and the same template-method
   NAME as Jar's — name ownership is per class. */
Vec(int x_ = 0) {
    op+(Vec^ p, Vec^ q) { x_ = p^.x_ + q^.x_; }
    T scaled<T>(T v) { return v + x_; }
}

/* shadowing across the chain is normal (collision is same-class only): a
   derived TEMPLATE shadows a base PLAIN method, and a derived PLAIN method
   shadows a base TEMPLATE. */
Aa(int a_ = 0) {
    int m(int v) { return v + 1; }
    T t<T>(T v) { return v + 2; }
}
Aa : Bb(int b_ = 0) {
    T m<T>(T v) { return v + 10; }
    int t(int v) { return v + 20; }
}

/* the static-bypass spelling pins the BASE's template through a derived shadow
   (inferred and explicit); bare resolution finds the shadow. */
Vv(int v_ = 0) {
    T plus<T>(T v) { return v + 100; }
}
Vv : Ww(int w_ = 0) {
    T plus<T>(T v) { return v + 1; }
    int useBase(int v) { return Vv:plus(v); }
    int useBaseX(int v) { return Vv:plus<int>(v); }
    int useOwn(int v) { return plus(v); }
}

/* a template method beside VIRTUALS: the vtable is undisturbed (dispatch still
   picks the derived id) and the template dispatches statically. */
Shape(int s_ = 0) {
    virtual int id() { return 1; }
    T tag<T>(T v) { return v + s_; }
    /* the convention in a VIRTUAL class: S binds a class. */
    S ident<S>(S s) { return s; }
}
Shape : Circle(int r_ = 0) {
    virtual int id() { return 2; }
}

/* a template method in a HOISTED class. */
Outer(int o_ = 0) {
    Inner(int i_ = 0) {
        T dub<T>(T v) { return v + v; }
    }
}

/* a base's template method reached through a derived receiver. `echoK` pins
   the convention through the DERIVED receiver (S binds a class). */
Counter(int c_ = 0) {
    T plus<T>(T v) { return v + c_; }
    S echoK<S>(S s) { return s; }
}
Counter : Kid(int k_ = 0) {
}

/* a template method may not share its name with a plain method... */
//-EXPECT-ERROR: may not share its name
//Bad1(int b_ = 0) {
//    int m(int v) { return v; }
//    T m<T>(T v) { return v; }
//}

/* ...in either direction. */
//-EXPECT-ERROR: may not share its name
//Bad2(int b_ = 0) {
//    T m<T>(T v) { return v; }
//    int m(int v) { return v; }
//}

/* a template method may not be virtual. */
//-EXPECT-ERROR: may not be virtual
//Bad3(int b_ = 0) {
//    virtual T m<T>(T v) { return v; }
//}

/* a constructor cannot be a template — `_` has no name position for a
   template-list, so the hook parse demands its parens. */
//-EXPECT-ERROR: Expected '('
//BadCtor(int b_ = 0) {
//    _<T>() { b_ = 1; }
//}

/* nor can a destructor. */
//-EXPECT-ERROR: Expected '('
//BadDtor(int b_ = 0) {
//    ~<T>() { b_ = 0; }
//}

int32 main() {

    Jar j(4);
    int e1 = j.scaled<int>(5); __println("e1 = " + e1);
    int e2 = j.scaled(6); __println("e2 = " + e2);

    /* the instance writes the field; later calls see it. */
    j.grow(10);
    int e3 = j.scaled(1); __println("e3 = " + e3);

    /* one instance serves every object. */
    Jar k(100);
    int e4 = k.scaled(2); __println("e4 = " + e4);

    /* sibling / self / free composition inside a template method. */
    int e5 = j.mix(2); __println("e5 = " + e5);

    /* composite T + the template-typed default: omitted, then supplied. */
    int arr[2] = (5, 6);
    int e6 = j.sum2(arr); __println("e6 = " + e6);
    int e7 = j.sum2(arr, 10); __println("e7 = " + e7);

    /* a method-call expression inside a larger expression. */
    int e8 = j.scaled(2) + k.scaled(1); __println("e8 = " + e8);

    /* the derived receiver reaches the base's template method. */
    Kid kid;
    int e9 = kid.plus(7); __println("e9 = " + e9);
    int e10 = kid.plus<int>(8); __println("e10 = " + e10);

    /* explicit-only: T appears in no parameter. */
    int e11 = j.fresh<int>(); __println("e11 = " + e11);

    /* a STATEMENT-position call with an explicit type-list. */
    j.grow<int8>(2);
    int f1 = j.scaled(1); __println("f1 = " + f1);

    /* a construction-temp receiver. */
    int ct = Jar(7).scaled(2); __println("ct = " + ct);

    /* a pointer receiver. */
    Jar^ jp = ^j;
    int pr = jp^.scaled(2); __println("pr = " + pr);

    /* T bound to a class; the instance method returns the class by value. */
    Vec v1(3);
    Vec v2(4);
    Vec v3 = j.comb(^v1, ^v2);
    __println("v3 = " + v3.x_);

    /* the same template-method name in an unrelated class. */
    int vs = v1.scaled(1); __println("vs = " + vs);

    /* an alias-template use in the signature. */
    int aa2 = 42;
    int vr = j.viaRf(^aa2); __println("vr = " + vr);

    /* the hoisted class's template method. */
    Outer:Inner oi;
    int hz = oi.dub(21); __println("hz = " + hz);

    /* method self-recursion. */
    int fc = j.fact(5); __println("fc = " + fc);

    /* THE CONVENTION OF CONVENIENCE on a method template: S binds a class
       through the by-value spelling — the instance takes `(const Vec)^`
       behind it, and the body stays generic. */
    Vec vcs = j.same(v1); __println("cs = " + vcs.x_);
    int cp = j.peek(v1); __println("cp = " + cp);

    /* the out-of-line member template. */
    int ol = j.extra(4); __println("ol = " + ol);

    /* derived TEMPLATE shadows base PLAIN; derived PLAIN shadows base TEMPLATE;
       the base's own versions stay reachable on a base object. */
    Bb bb;
    __println("s1 = " + bb.m(1));
    __println("s2 = " + bb.t(1));
    Aa aax;
    __println("s3 = " + aax.m(1));
    __println("s4 = " + aax.t(1));

    /* the bypass spelling pins the base's template through the shadow. */
    Ww w;
    __println("b1 = " + w.useBase(5));
    __println("b2 = " + w.useBaseX(5));
    __println("b3 = " + w.useOwn(5));

    /* beside virtuals: dispatch is undisturbed, the template is static. */
    Circle c;
    __println("ci = " + c.tag(5));
    Shape^ sp = ^c;
    __println("vd = " + sp^.id());
    __println("st = " + sp^.tag(3));

    /* the convention across the FLAVORS (class bindings everywhere): the
       hoisted class's dub (Vec+Vec through op+), the base's echoK through the
       derived receiver, and the virtual class's ident. */
    Vec cv(5);
    Vec hd = oi.dub(cv); __println("f1 = " + hd.x_);
    Vec kd = kid.echoK(cv); __println("f2 = " + kd.x_);
    Vec vd2 = c.ident(cv); __println("f3 = " + vd2.x_);

    /* explicit type arguments on a plain method. */
    //-EXPECT-ERROR: is not a template method
    //int np = j.half<int>(4); __println("np = " + np);

    /* the wrong number of explicit type arguments. */
    //-EXPECT-ERROR: Wrong number of template arguments
    //int wa = j.scaled<int, int>(1); __println("wa = " + wa);

    /* conflicting bindings across the user arguments. */
    //-EXPECT-ERROR: Conflicting bindings for template parameter
    //int8 c8 = 1;
    //int cb = j.sum2(arr, c8); __println("cb = " + cb);

    /* nothing binds T. */
    //-EXPECT-ERROR: Cannot infer template parameter
    //int nb = j.fresh(); __println("nb = " + nb);

    /* an empty type-list is no template call — the gate falls to a comparison. */
    //-EXPECT-ERROR: Expected expression
    //int em = j.scaled<>(1); __println("em = " + em);

    return 0;
}
