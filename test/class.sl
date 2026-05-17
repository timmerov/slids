/*
test adding methods to classes after the fact.
also test forward declarations.

for default and delete:
the signature of the derived method must match
the signature of the base method exactly.
compile error if there is no such method in the
base class.
the exception is the very weird case where the
author is adding a pure virtual method to a
derived class.

long ancestries.
deleted and default methods may not be defined
for any descendant class.


*/

Simple(
    int x_
) {
    _() {
        __println("Simple:ctor");
    }
    ~() {
        __println("Simple:dtor");
    }
}

/*
two valid syntaxes for forward declarations.
*/
void foo();
void Simple:hello();
Simple() {
    void goodbye();
}

/*
two valid syntaxes for after-the-fact definitions.
*/
void foo() {
    __println("foo");
}
Simple() {
    void hello() {
        __println("Hello, World!");
    }
}
void Simple:goodbye() {
    __println("Goodbye, World!");
}

/*
base class and inherited class with deleted methods.
*/
Base(int x_) {
    _() {
        __println("Base:ctor");
    }
    virtual ~() {
        __println("Base:dtor");
    }

    void greet() {
        __println("Base:greet");
    }

    void speak() {
        __println("Base:speak");
    }

    virtual void process() {
        __println("Base:process");
    }

    virtual void dwim() {
        __println("Base:dwim");
    }
}
Base: Derived() {
    _() {
        __println("Derived:ctor");
    }
    virtual ~() {
        __println("Derived:dtor");
    }

    /*
    keep for documentation purposes.
    functionally a no-op.
    cannot be defined in the derived class.
    cannot shadow the base method.
    */
    void greet() = default;

    /*
    speak cannot be called from a Derived object.
    cannot be defined in the derived class.
    */
    void speak() = delete;

    /*
    keep for documentation purposes.
    functionally a no-op.
    cannot be defined in the derived class.
    */
    virtual void process() = default;

    /*
    dwim cannot be called from a Derived object.
    cannot be defined in the derived class.
    */
    virtual void dwim() = delete;
}

// negatives — bodies disabled with `//` so the file compiles. the runner
// strips the `//` prefixes for the lone live case driven by each marker.

//-EXPECT-ERROR: 'Simple' is a class, not a namespace. Reopen with 'Simple()'.
// Simple {
//     void error() {
//         __println("Simple is a class, not a namespace.");
//     }
// }

//-EXPECT-ERROR: 'Space' is a namespace, not a class. Reopen with 'Space', no parentheses.
// Space {
//     void namespace() { }
// }
// Space() { }

//-EXPECT-ERROR: 'greet()' is = default in 'Derived'
// Derived() {
//     void greet() {
//         __println("Derived:greet()");
//     }
// }

//-EXPECT-ERROR: 'speak()' is = delete in 'Derived'
// Derived() {
//     void speak() {
//         __println("Derived:speak()");
//     }
// }

//-EXPECT-ERROR: 'process()' is = default in 'Derived'
// void Derived:process() {
//     __println("Derived:process()");
// }

//-EXPECT-ERROR: 'dwim()' is = delete in 'Derived'
// void Derived:dwim() {
//     __println("Derived:dwim()");
// }

InferFieldTypes(
    x_ = 42,
    pi_ = 3.14
) {
    void print() {
        __println("typeof(x)=" + ##type(x_));
        __println("typeof(pi)=" + ##type(pi_));
    }
}

//-EXPECT-ERROR: Field 'x_' has no type and no initializer
// NoInit(x_) { }

//-EXPECT-ERROR: Expression is not allowed in a constant initializer
// NonConstFn(x_ = foo()) { }

//-EXPECT-ERROR: Expression is not allowed in a constant initializer
// NonConstClass(x_ = Simple()) { }

//-EXPECT-ERROR: references unknown name 'unknown_name'
// UnknownName(x_ = unknown_name) { }

//-EXPECT-ERROR: Expression is not allowed in a constant initializer
// NullDefault(x_ = nullptr) { }


/* hoisted classes. */
BaseHoist(int x_ = 1) {
    LowHoist(int y_ = 2) {
        MidHoist(int z_ = 4) {
            HighHoist(int w_ = 8) {
                TopHoist(int v_ = 16) {
                }
            }
        }
    }
}

/* class in function */
void classInFunction() {
    InFunc(int x_ = 88) {
        Hoisted(int y_ = 66) {
        }
    }
    InFunc:Hoisted cls;
    __println("foo:cls.y_ + " + cls.y_);

    /* derived classes in function. */
    BaseHoist : DerivedHoist(int u_ = 32) {
    }
    InFunc : DerivedIF(int p_ = 64) {
    }

    /* class in nested function. */
    void nestedFunc() {
        DerivedHoist dh;
        DerivedIF dif;
        __println("dh.u_ = " + dh.u_);
        __println("dif.p_ = " + dif.p_);
    }
    nestedFunc();
}

/* class in a template function. each instantiation gets its own copy of */
/* the local class; a template-dependent field type is substituted per */
/* instantiation. */
T templateLocal<T>(T seed) {
    Holder(T item_) {
    }
    Holder h;
    h.item_ = seed;
    return h.item_;
}

/* template-independent local class inside a template function. */
int templateLocalIndep<T>(T ignored) {
    Tally(int n_ = 9) {
    }
    Tally t;
    return t.n_;
}

/* local classes inside the methods of a template class. the local class */
/* may reference the template class's type parameter (Echo), or be */
/* template-independent (Tag). */
Boxed<T>(T value_) {
    T unwrap() {
        Echo(T copy_) {
        }
        Echo e;
        e.copy_ = value_;
        return e.copy_;
    }
    int tag() {
        Tag(int id_ = 5) {
        }
        Tag g;
        return g.id_;
    }
}

/*
*/
int32 main() {
    Simple simple;
    simple.hello();
    foo();
    simple.goodbye();

    {
        Base base;
        Derived derived;
        base.greet();
        base.speak();
        base.process();
        derived.greet();
        derived.process();
        //-EXPECT-ERROR: call to deleted method 'speak()'
        // derived.speak();
    }

    InferFieldTypes ift;
    ift.print();

    BaseHoist:LowHoist:MidHoist:HighHoist:TopHoist hoist;
    __println("TopHoist: hoist.v_ = " + hoist.v_);

    classInFunction();

    __println("templateLocal<int> = " + templateLocal<int>(42));
    __println("templateLocal<float> = " + templateLocal<float>(3.5));
    __println("templateLocalIndep<int> = " + templateLocalIndep<int>(0));

    Boxed<int> bi(7);
    __println("Boxed<int>.unwrap = " + bi.unwrap());
    __println("Boxed<int>.tag = " + bi.tag());
    Boxed<float> bf(2.5);
    __println("Boxed<float>.unwrap = " + bf.unwrap());

    /* compile errors: a local class is unreachable outside its block — */
    /* not by bare name, and not via the enclosing function (a function */
    /* is not a namespace). */
    //-EXPECT-ERROR: Unknown type 'InFunc'
    // InFunc na1;
    //-EXPECT-ERROR: Unknown type 'classInFunction.InFunc'
    // classInFunction:InFunc na2;

    return 0;
}
