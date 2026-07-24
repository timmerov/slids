/*
test class templates.

definition:

    class-name < template-list > ( field-list ) { body }

template-list is a comma separate list of identifiers used as types.
field-list is a normal comma separated list of fields.
the fields may use types from the template-list.

usage:

    class-name < type-list > variable-name

type-list is required.
template classes may be declared anywhere a class may be declared.
template class objects may be instantiated anywhere a non-template class
object may be instantiated.
template class objects may be initialized every way a non-template class
object may be initialized.
*/

/*
claude says:

a class template is a PATTERN (pristine body + scope snapshot, the
fn-template machinery); the instance is minted at RESOLVE, where the first
type position names `Vec<int32>` — the same arm that expands alias
templates. instantiation runs the four class phases (NAME -> TYPES ->
cycle+needs -> BODY, the registerLocalClasses shape) over a clone with each
T bound and the template's own name bound to the instance, memoized on the
canonicalized type-list, so every later `Vec<int32>` — through any alias
spelling — is the same interned class. by the end of resolve the instance
is an ordinary class: every init form, the transfer invariant, operators,
virtuals, inheritance (both directions), for-class, arrays, and `new` ride
existing machinery; ##type reports the use as written.

the type-list is required (no inference from construction arguments). a
template owns its name (no overloads, no re-open); incomplete (`...`) and
external members (`T Vec:m()`) are rejected this landing; template methods
inside a template class landed later (tmpl_nested.sl); nested uses
(`Pair<Vec<int>>`) stay with the `>>` umbrella todo.
*/

/* the workhorse: fields and methods on T, a user binary operator. */
Vec<T>(T x_ = 0, T y_ = 0) {
    T sum() { return x_ + y_; }
    T addx(T v) { x_ = x_ + v; return x_; }
    op+(Vec^ p, Vec^ q) { x_ = p^.x_ + q^.x_; y_ = p^.y_ + q^.y_; }
}

/* two type parameters. */
Pair<K, V>(K k_ = 0, V v_ = 0) {
    K key() { return k_; }
    V val() { return v_; }
}

/* the convention-of-convenience fixture: a class with the operators Vec's
   T-typed methods dispatch, so Vec<CPoint>'s `T addx(T v)` — a bare-T method
   param binding a CLASS — takes `(const CPoint)^` behind the value spelling. */
CPoint(int c_ = 0) {
    op=(CPoint^ r) { c_ = r^.c_; }
    op+(CPoint^ p, CPoint^ q) { c_ = p^.c_ + q^.c_; }
}

/* lifecycle hooks in a template: ctor/dtor run per instance object. */
Trace<T>(T t_ = 0) {
    _() { __println("ctor " + t_); }
    ~() { __println("dtor " + t_); }
    T get() { return t_; }
}

/* a hooked instance as a plain class's FIELD — needs propagate transitively.
   (The field is REQUIRED: an in-place construction is the one legal class-field
   value, per construct.sl.) */
Hold(Trace<int> f_) { }

/* deriving FROM an instance: the instance is the base, methods reach it bare. */
Vec<int> : Der(int extra_ = 0) {
    int total() { return sum() + extra_; }
}

/* a template WITH a (plain) base. */
Base0(int b0_ = 1) {
    int bump() { return b0_ + 1; }
}
Base0 : TDer<T>(T t_ = 0) {
    T mix() { return t_ + bump(); }
    /* the self spellings and the base bypass, in one body: `self.field`, the
       explicit `Base0:` pin, and the template's own bare name as a type. */
    T mix2() { TDer^ sp = ^self; return self.t_ + Base0:bump() + sp^.t_; }
}

/* a virtual template class, and a plain class deriving from its instance. */
VB<T>(T z_ = 3) {
    virtual T tagv() { return z_; }
}
VB<int> : VK(int y_ = 0) {
    virtual int tagv() { return 100 + y_; }
}

/* a container instance drives for-class (op[]/size). */
Trio<T>(T a_ = 0, T b_ = 0, T c_ = 0) {
    int size() { return 3; }
    T^ op[](int i) {
        if (i == 0) { return ^a_; }
        if (i == 1) { return ^b_; }
        return ^c_;
    }
}

/* a template declared in a namespace. */
Space {
    Boxed<T>(T b_ = 0) {
        T get() { return b_ + 1; }
    }
}

/* a PLAIN class embedding an instance by value (field + transitive layout),
   and doubling as the not-a-template negative below. */
Wrap(Vec<int> in_ = Vec<int>()) {
    int wsum() { return in_.sum(); }
}

/* an instance type in a file-scope function signature. */
int useVec(Vec<int>^ p) { return p^.sum(); }

/* composition with function templates: an instance rides inference... */
T thru<T>(T v) { return v; }

/* ...and a class template used INSIDE a function template's body. */
T boxsum<T>(T v) {
    Vec<T> w(v, v);
    return w.sum();
}

alias Integer = int;

/* an instance-typed GLOBAL (registry-constructed static storage). */
global Vec<int> gvec;

/* composite positions for T: a pointer field, a tuple field (with a tuple
   default), a method-local array. */
Com<T>(T a_ = 5, T^ ptr_ = nullptr, (T, T) tp_ = (1, 2)) {
    T tsum() { return tp_[0] + tp_[1]; }
    T locsum(T x) { T loc[2]; loc[0] = x; loc[1] = a_; return loc[0] + loc[1]; }
}

/* USER transfer operators in a template — the transfer invariant must call
   these (skewed on purpose so a silent blit would show). */
Us<T>(T u_ = 0) {
    op=(Us^ s) { u_ = s^.u_ + 1; }
    op<--(mutable Us^ s) { u_ = s^.u_ + 100; s^.u_ = 0; }
}

/* a plain class WITH HOOKS, for binding K to a class type. */
Data(int d_ = 3) {
    _() { __println("data ctor " + d_); }
    ~() { __println("data dtor " + d_); }
    int dv() { return d_; }
}

/* an INSTANCE as a type argument, smuggled through an alias (the nested
   `Pair<Vec<int>>` spelling stays deferred with the `>>` split). */
alias VI = Vec<int>;

/* the full member vocabulary in a template body: const, alias, enum, a nested
   (hoisted) class, and a method calling a FREE function template. */
Kit<T>(T k_ = 2) {
    const int kBase = 40;
    alias Elem = T;
    enum int E ( eOne = 1, eTwo );
    Sub(int s_ = 6) {
        int sv() { return s_; }
    }
    T total() { return k_ + kBase + E:eOne; }
    Elem dbl(Elem x) { return x + x; }
    int subv() { Sub s(7); return s.sv(); }
    T viaFree(T x) { return thru(x) + k_; }
}

/* for-class, the begin/end/next (by value) flavor. */
Cnt<T>(T n_ = 3) {
    T begin() { return 0; }
    T end() { return n_; }
    T next(T i) { return i + 1; }
}

/* a required (no-default) field, bound to a POINTER type argument. */
Cell<T>(T c_) {
    T take() { return c_; }
}

/* a self-referential field: `Node<T>` inside its own body rides the memo. */
Node<T>(T val_ = 0, Node<T>^ next_ = nullptr) {
    T tail2() {
        if (next_ == nullptr) { return val_; }
        return next_^.val_;
    }
}

/* an empty template class takes the one-byte minimum size. */
Emp<T>() { }

/* same-name block templates in DIFFERENT functions: distinct classes with
   identical instance spellings — the def_id keeps their symbols apart. */
void fnA() {
    Loc2<T>(T q_ = 0) {
        T id() { return q_ + 1; }
    }
    Loc2<int> l(5);
    __println("fa = " + l.id());
}

void fnB() {
    Loc2<T>(T q_ = 0) {
        T id() { return q_ + 2; }
    }
    Loc2<int> l(5);
    __println("fb = " + l.id());
}

/* a template owns its name: no plain-class duplicate, no re-open. */
//-EXPECT-ERROR: owns its name
//Vec(int dup_ = 0) { }

int32 main() {

    /* construction with arguments; methods read and write T fields. */
    Vec<int> a(3, 4);
    int e1 = a.sum(); __println("e1 = " + e1);
    int e2 = a.addx(10); __println("e2 = " + e2);

    /* copy init; the copies are independent. */
    Vec<int> b = a;
    int e3 = b.sum(); __println("e3 = " + e3);
    b.addx(1);
    int e4 = a.sum(); __println("e4 = " + e4);
    int e5 = b.sum(); __println("e5 = " + e5);

    /* a second binding of the same template is a distinct class. */
    Vec<int8> c8(2, 3);
    int8 e7 = c8.sum(); __println("e7 = " + e7);

    /* the user operator on instances. */
    Vec<int> s = a + b;
    int e8 = s.sum(); __println("e8 = " + e8);

    /* THE CONVENTION OF CONVENIENCE, uniform: a CLASS type argument — the
       flavor's T-typed method params pass by reference-to-const behind the
       value spelling; sum/addx dispatch CPoint's operators through it. */
    Vec<CPoint> vcp(CPoint(3), CPoint(4));
    CPoint cc1 = vcp.sum(); __println("cc1 = " + cc1.c_);
    CPoint cc2 = vcp.addx(CPoint(10)); __println("cc2 = " + cc2.c_);

    /* an alias argument names the SAME instance; ##type reports as written. */
    Vec<Integer> d2 = a;
    int e9 = d2.sum(); __println("e9 = " + e9);
    intptr szd = sizeof(Vec<Integer>) - sizeof(Vec<int>);
    __println("szd = " + szd);
    __println("t1: " + ##type(a));
    __println("t2: " + ##type(d2));

    /* two type parameters. */
    Pair<int, int8> p1(400, 5);
    int e10 = p1.key(); __println("e10 = " + e10);
    int8 e11 = p1.val(); __println("e11 = " + e11);

    /* default construction, an empty leading slot, a nameless temp. */
    Vec<int> z0;
    int e12 = z0.sum(); __println("e12 = " + e12);
    Vec<int> es(,9);
    int e13 = es.sum(); __println("e13 = " + e13);
    int nt = Vec<int>(20, 1).sum(); __println("nt = " + nt);

    /* heap and array instantiation. */
    Vec<int>^ np = new Vec<int>;
    np^.addx(3);
    int e14 = np^.sum(); __println("e14 = " + e14);
    Vec<int> va[2];
    va[0].addx(5);
    va[1].addx(6);
    int e15 = va[0].sum() + va[1].sum(); __println("e15 = " + e15);

    /* ctor/dtor hooks fire per object, at block scope. */
    {
        Trace<int> tr(5);
        __println("in block");
    }

    /* deriving from an instance; the upcast reads the base. */
    Der dd(1, 2, 3);
    int e16 = dd.total(); __println("e16 = " + e16);
    Vec<int>^ bp = ^dd;
    int e17 = bp^.sum(); __println("e17 = " + e17);

    /* a template with a base. */
    TDer<int> td(4, 10);
    int e18 = td.mix(); __println("e18 = " + e18);

    /* virtual dispatch through an instance-typed base pointer. */
    VK vk(3, 7);
    VB<int>^ vp = ^vk;
    int e19 = vp^.tagv(); __println("e19 = " + e19);
    VB<int> vb0(9);
    int e20 = vb0.tagv(); __println("e20 = " + e20);

    /* a namespace-declared template, used qualified. */
    Space:Boxed<int> sb(41);
    int e21 = sb.get(); __println("e21 = " + e21);

    /* a block-scope template. */
    Loc<T>(T q_ = 0) {
        T dq() { return q_ * 2; }
    }
    Loc<int> lc(21);
    int e22 = lc.dq(); __println("e22 = " + e22);

    /* for-class over an instance. */
    Trio<int> t3(5, 6, 7);
    int acc = 0;
    for (int x : t3) {
        acc = acc + x;
    }
    __println("acc = " + acc);

    /* an instance through a function template's inference... */
    Vec<int>^ tp = thru(^a);
    int e23 = tp^.sum(); __println("e23 = " + e23);

    /* ...a class template instantiated from a function template's body — a
       binding that exists nowhere else (int16 first appears here)... */
    int e24 = boxsum(5); __println("e24 = " + e24);
    int16 s16 = 6;
    int16 e25 = boxsum(s16); __println("e25 = " + e25);

    /* ...an instance as a plain class's field, and in a signature. */
    Wrap ww(Vec<int>(2, 3));
    int e26 = ww.wsum(); __println("e26 = " + e26);
    int e27 = useVec(^b); __println("e27 = " + e27);

    /* the global instance: registry-constructed, read and written. */
    int g1 = gvec.sum(); __println("g1 = " + g1);
    gvec.addx(5);
    int g2 = gvec.sum(); __println("g2 = " + g2);

    /* the synthesized default move and swap between instances. */
    Vec<int> m1(30, 40);
    Vec<int> m2(1, 2);
    m2 <-- m1;
    __println("mv = " + m2.sum() + " " + m1.sum());
    Vec<int> sw1(7, 8);
    Vec<int> sw2(1, 1);
    sw1 <--> sw2;
    __println("sw = " + sw1.sum() + " " + sw2.sum());

    /* the USER transfer operators are called — the +1/+100 skews prove no
       blit; the move empties its source. */
    Us<int> ua(5);
    Us<int> ub = ua;
    __println("u1 = " + ua.u_ + " " + ub.u_);
    Us<int> uc;
    uc <-- ua;
    __println("u2 = " + uc.u_ + " " + ua.u_);

    /* composite fields: the tuple default, a pointer field, a T-local array. */
    Com<int> cm;
    int c1 = cm.tsum(); __println("c1 = " + c1);
    int x9 = 11;
    Com<int> cm2(1, ^x9, (4, 6));
    int c2 = cm2.ptr_^ + cm2.tsum(); __println("c2 = " + c2);
    int c3 = cm.locsum(30); __println("c3 = " + c3);

    /* K bound to a HOOKED class: the field constructs (field-list fill from
       the scalar default) and destroys exactly once. */
    {
        Pair<Data, int> pd;
        __println("pd = " + pd.k_.dv() + " " + pd.v_);
    }

    /* an instance as a type argument, via the alias. */
    Pair<VI, int8> pv(Vec<int>(2, 3), 7);
    int k2 = pv.k_.sum(); __println("k2 = " + k2);
    int8 k3 = pv.v_; __println("k3 = " + k3);

    /* destructor balance: a copy (ctor on defaults, transfer fills, two
       dtors)... */
    {
        Trace<int> tc1(1);
        Trace<int> tc2 = tc1;
        __println("copied " + tc2.get());
    }
    /* ...an array (two ctors, two dtors, reverse order)... */
    {
        Trace<int8> ta[2];
        __println("array");
    }
    /* ...a hooked instance field inside a plain class (in-place construction,
       the one legal class-field value)... */
    {
        Hold hd(Trace<int>(7));
        __println("held");
    }
    /* ...and an expression temp, dead at its semicolon. */
    int tg = Trace<int>(9).get(); __println("tg = " + tg);

    /* the member vocabulary: const + enum, nested class, member alias, and a
       free-template call from an instance method. */
    Kit<int> kt;
    int c5 = kt.total(); __println("c5 = " + c5);
    int c6 = kt.subv(); __println("c6 = " + c6);
    int c7 = kt.dbl(21); __println("c7 = " + c7);
    int cf = kt.viaFree(5); __println("cf = " + cf);

    /* for-class via begin/end/next. */
    Cnt<int> ct(4);
    int acc2 = 0;
    for (int i : ct) { acc2 = acc2 + i; }
    __println("acc2 = " + acc2);

    /* a pointer type argument. */
    int pv9 = 42;
    Cell<int^> cp(^pv9);
    int c9 = cp.take()^; __println("c9 = " + c9);

    /* the self-referential field, linked and terminal. */
    Node<int> n2(20);
    Node<int> n1(10, ^n2);
    int c10 = n1.tail2(); __println("c10 = " + c10);
    int c11 = n2.tail2(); __println("c11 = " + c11);

    /* the empty instance is one byte. */
    Emp<int> e0;
    intptr c12 = sizeof(e0); __println("c12 = " + c12);

    /* self spellings + the base bypass (on the object from e18). */
    int c13 = td.mix2(); __println("c13 = " + c13);

    /* the same-named block templates, each in its own function. */
    fnA();
    fnB();

    /* a tuple with an instance slot, indexed and destructured. */
    Vec<int> tv(13, 4);
    (Vec<int>, int) tup = (tv, 5);
    int c14 = tup[0].sum() + tup[1]; __println("c14 = " + c14);
    (Vec<int> dva, int dvb) = tup;
    int c15 = dva.sum() + dvb; __println("c15 = " + c15);

    /* the type-list is required. */
    //-EXPECT-ERROR: requires a type-argument list
    //Vec miss;

    /* ...in construction position too. */
    //-EXPECT-ERROR: requires a type-argument list
    //int q1 = Vec(1, 2).sum(); __println("q1 = " + q1);

    /* ...and in sizeof. */
    //-EXPECT-ERROR: requires a type-argument list
    //int q2 = sizeof(Vec); __println("q2 = " + q2);

    /* the wrong number of template arguments. */
    //-EXPECT-ERROR: Wrong number of template arguments
    //Vec<int, int> wa; int q3 = wa.sum(); __println("q3 = " + q3);

    /* an unknown argument type. */
    //-EXPECT-ERROR: Unknown type
    //Vec<Bogus> bg; int q4 = bg.sum(); __println("q4 = " + q4);

    /* a type-list on a class that is not a template. */
    //-EXPECT-ERROR: is not a template
    //Wrap<int> nw; int q5 = nw.wsum(); __println("q5 = " + q5);

    /* an empty type-list is no type at all — the decl gate rejects it. */
    //-EXPECT-ERROR: Expected
    //Vec<> ev; int q6 = ev.sum(); __println("q6 = " + q6);

    /* a void type argument fails the field default's fit-check. */
    //-EXPECT-ERROR: does not fit
    //Vec<void> vv; int q7 = vv.sum(); __println("q7 = " + q7);

    /* an abstract instance cannot be constructed. */
    //-EXPECT-ERROR: abstract
    //Abs<T>(T a_ = 0) {
    //    virtual T m() = delete;
    //}
    //Abs<int> ab;
    //int q8 = ab.m(); __println("q8 = " + q8);

    /* a self-instantiating FIELD chain nests to the depth limit... */
    //-EXPECT-ERROR: depth limit
    //Bad<T>(Bad<T^>^ b_ = nullptr) { }
    //Bad<int> bx;
    //Bad<int>^ bpn = ^bx;
    //__println("p = " + (bpn == nullptr));

    /* ...and a METHOD-BODY chain reaches the drain flat — the total cap. */
    //-EXPECT-ERROR: depth limit
    //Way<T>(T v_ = 0) {
    //    void go() { Way<T^> w; w.go(); }
    //}
    //Way<int> wy;
    //wy.go();

    /* an instance cannot pass by value — the general class rule. */
    //-EXPECT-ERROR: must be a pointer
    //void bv(Vec<int> v) { __println("s = " + v.sum()); }
    //bv(tv);

    /* a function template is not a type. */
    //-EXPECT-ERROR: is not a template
    //thru<int> tx = 5; __println("tx = " + tx);

    return 0;
}
