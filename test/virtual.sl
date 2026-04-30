/*
virtual classes
*/

/*
pure virtual class.
the compiler auto-generates an empty ctor and an empty virtual dtor for every
virtual class. the `virtual` keyword on `~` is optional in a virtual class.
*/
PureVirtual(
    int x_ = 0
) {
    /* explicitly declared dtor must be virtual. */
    _() {}
    virtual ~() { __println("PureVirtual:vdtor"); }

    /*
    delete syntax indicates function does not actually exist.
    parameters and a non-void return are exercised in the impls below.
    */
    virtual void hello(int n) = delete;
    virtual int  goodbye() = delete;
}

/*
base class derived from pure virtual class.
explicit ctor and virtual dtor (dtor body prints to verify destruction).
*/
PureVirtual : BaseVirtual(
    int y_ = 0
) {
    /* explicitly declared dtor must be virtual. */
    _() {}
    virtual ~() { __println("BaseVirtual:vdtor"); }

    /*
    compile error. the virtual keyword is required to override.
    cannot shadow a virtual method with a non-virtual one.
    */
    //void hello(int n);

    /*
    compile errors: signatures don't match the base slot.
    */
    //virtual int hello(int n);
    //virtual void hello();

    virtual void hello(int n) {
        __println("Base: hello n=" + n);
    }

    virtual void catch() {
        __println("Base: catch");
    }

    /* virtual method with a non-void return — exercised through value, ptr, and self. */
    virtual int sum() {
        return x_ + y_;
    }

    /* self-call: dispatch through vtable so derived overrides win at runtime. */
    virtual void intro() {
        self.catch();
        self.hello(99);
    }
}

/*
reopen virtual class.
*/
BaseVirtual {
    virtual int goodbye() {
        __println("Base: goodbye");
        return -1;
    }

    /*
    compile error: new virtual methods may only be added when the class is
    declared, not in a reopen.
    */
    //virtual void release() {}
}

/*
class derived from base class.
override catch (used for ptr+self virtual dispatch tests).
override hello with an explicit call to the base impl (ancestor-method call).
*/
BaseVirtual : DerivedVirtual(
    int z_ = 0
) {
    /* explicitly declared dtor must be virtual. */
    _() {}
    virtual ~() { __println("DerivedVirtual:vdtor"); }

    virtual void catch() {
        __println("Derived: catch");
    }

    virtual void hello(int n) {
        BaseVirtual:hello(n);
        __println("Derived: also hello n=" + n);
    }
}

/*
auto-generated virtual dtor in the middle of a chain.
ChainBase has explicit virtual dtor with print.
ChainMiddle has no explicit dtor — the compiler auto-generates one that
chains through to ChainBase's dtor at scope/heap exit.
ChainLeaf has explicit virtual dtor with print.
*/
ChainBase(
    int b_ = 0
) {
    /*
    compile error: ctor and dtor must be declared together
    or auto-generated together.
    it is a compile error to have one but not the other.
    comment out this line to test.
    */
    _() {}

    /* explicitly declared dtor must be virtual. */
    virtual ~() { __println("ChainBase:vdtor"); }
    virtual int tag() { return b_; }
}

/* middle class — no dtor declared. */
ChainBase : ChainMiddle(
    int m_ = 0
) {
    /* auto-generated dtor is virtual. */
    virtual int tag() { return m_; }
}

ChainMiddle : ChainLeaf(
    int l_ = 0
) {
    /* explicitly declared dtor must be virtual. */
    _() {}
    virtual ~() { __println("ChainLeaf:vdtor"); }
    virtual int tag() { return l_; }
}

/*
non-virtual class
*/
Plain(
    int a_
) {
    _() {}
    ~() {}
}

/*
compile error: ancestors of a virtual class must have a virtual dtor.
*/
/*
Plain : FailedVirtual() {
    virtual void greet() { __println("non-committal grunting noise."); }
}
*/

int32 main() {
    /*
    compile error: cannot instantiate a pure virtual class.
    */
    //PureVirtual pure;

    {
        __println("---- existing chain (stack) ----");

        /* init check: explicit field values flowing through inheritance. */
        BaseVirtual base(7, 11);
        __println("base.sum=" + base.sum());      /* virtual returning int */
        base.hello(1);
        base.intro();

        DerivedVirtual derived(2, 3, 5);
        __println("derived.sum=" + derived.sum());
        derived.hello(2);
        derived.intro();

        /* virtual dispatch through base/pure pointers. */
        BaseVirtual^ base_ptr = ^derived;
        base_ptr^.catch();
        __println("base_ptr^.sum=" + base_ptr^.sum());

        PureVirtual^ pure_ptr = ^derived;
        pure_ptr^.goodbye();
    }

    {
        /*
        heap allocation + delete: chain dtors run in reverse.
        expected order: DerivedVirtual:vdtor, BaseVirtual:vdtor.
        */
        __println("---- heap (new/delete) ----");
        DerivedVirtual^ heap = new DerivedVirtual(10, 20, 30);
        heap^.hello(7);
        delete heap;
    }

    {
        /*
        auto-gen middle vdtor: instantiate ChainLeaf; expect ChainLeaf:vdtor
        then ChainBase:vdtor at scope exit. the middle class's auto-generated
        empty dtor is the only thing chaining leaf back to base.
        */
        __println("---- middle auto-gen ----");
        ChainLeaf leaf(100, 200, 300);
        __println("leaf.tag=" + leaf.tag());
    }

    /* compile-error catalog (each verified one-by-one). */
        __println("---- compile errors ----");
    __println("1: Not allowed: non-virtual cannot shadow inherited virtual method");
    __println("2: Not allowed: virtual override return type must match base");
    __println("3: Not allowed: virtual method signature must match the inherited slot");
    __println("4: Not allowed: new virtual methods may not be added in a reopen");
    __println("5: Not allowed: all ancestors of a virtual class must have a virtual destructor");
    __println("6: Not allowed: cannot instantiate a pure virtual class");
    __println("7: Not allowed: ctor and dtor must be declared together or both auto-generated");

    return 0;
}
