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
Simple {
    void goodbye();
}

/*
two valid syntaxes for after-the-fact definitions.
*/
void foo() {
    __println("foo");
}
Simple {
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

//-EXPECT-ERROR: 'greet()' is = default in 'Derived'
// Derived {
//     void greet() {
//         __println("Derived:greet()");
//     }
// }

//-EXPECT-ERROR: 'speak()' is = delete in 'Derived'
// Derived {
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

    return 0;
}
