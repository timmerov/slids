/*
test namespaces.

namespaces may be opened in any scope.
they may be re-opened in any scope.
even different scopes.
things defined in a namespace inherit the lifetime of the scope.

there is a default global namespace named :.

bare names may be used without qualifiers within the scope.
qualifiers are required to access things outside of the current scope.

namespaces may not be instantiated.
*/

/* open in global scope. */
Space {
    const int kOne = 1;
    int foo() {
        return kOne;
    }

    /* nested space. */
    Nested {
        const int kFour = 4;
    }
}

/* reopen space */
Space {
    int bar() {
        return 100 + foo();
    }
}

/* inline syntax */
const int Space:kSix = 6;

int scoped() {
    /* reopen in local scope. */
    Space {
        const int kTwo = 2;
    }
    return Space:kTwo;
}

int not_qualified() {
    /* inline reopen nested space. */
    const int Space:Nested:kSeven = 7;

    /* qualifier not needed. */
    alias Space;
    int m = kSix;

    /*
    qualifier not needed.
    note: this also works:
        alias Nested;
    because the Space qualifier has been aliased to current scope.
    */
    alias Space:Nested;
    int n = kSeven;

    return m + n;
}

/* resolve shadowed definitions. */
const int kBest = 100;
Local {
    const int kBest = 200;

    int local_best() {
        return kBest;
    }

    int global_best() {
        return ::kBest;
    }
}

Global {
    const int kEight = 8;
}
alias Global;

/* --- a class is a first-class namespace member. forward references span file
   scope and namespace scope (their names register together, so either may name
   the other before it is defined). --- */

/* a file-scope class whose by-value field is a NAMESPACE class defined below. */
Crate(Space:Gadget g_) {
    int total() {
        return g_.tick() + 1000;
    }
}

Space {
    /* a namespace class (Space:Gadget) whose by-value field is a FILE-scope class
       defined below — the reverse forward reference. */
    Gadget(Cog c_) {
        int tick() {
            return c_.teeth() * 10;
        }
    }
}

/* the file-scope class the namespace class above refers to. */
Cog(int teeth_) {
    int teeth() {
        return teeth_;
    }
}

/* a namespace class with lifecycle hooks (ctor/dtor), whose method reaches a
   namespace sibling const BARE (kGain) — no qualifier, like a free function in
   the namespace. */
Space {
    const int kGain = 3;
    Meter(int v_) {
        _() { __println("Meter:ctor " + v_); }
        ~() { __println("Meter:dtor " + v_); }
        int amp() { return v_ * kGain; }
    }
}

/* contained scope so the dtor fires at this return, keeping output ordered. */
int meter_demo() {
    Space:Meter mtr(4);
    return mtr.amp();
}

/* a class in a NESTED namespace — reached A:B:Knob. */
A {
    B {
        Knob(int t_) {
            int turn() { return t_ + 1; }
        }
    }
}

/* an EMPTY namespace class (no fields → no-self ABI), constructed and called. */
Space {
    Tag() {
        int answer() { return 42; }
    }
}

/* a namespace class with lifecycle hooks used as a BY-VALUE field of a file-scope
   class: the container inherits the ctor/dtor need transitively (Wrapper has no
   hooks of its own). */
Space {
    Tracked(int id_) {
        _() { __println("Tracked:ctor " + id_); }
        ~() { __println("Tracked:dtor " + id_); }
        int id() { return id_; }
    }
}
Wrapper(Space:Tracked t_) {
    int wid() { return t_.id(); }
}
int wrap_demo() {
    Wrapper w(77);
    return w.wid();
}

/* a namespace class reached through the heap, an explicit reference param, and a
   by-value return. */
Space {
    Cell(int v_ = 9) {
        int g() { return v_; }
    }
}
void show_cell(Space:Cell^ c) { __println("cell = " + c^.g()); }
Space:Cell make_cell(int n) {
    Space:Cell c(n);
    return c;
}

/* a class defined in a LOCALLY-opened namespace (a reopen inside a function). */
int local_ns_class() {
    Depot {
        Crate(int n_) {
            int size() { return n_; }
        }
    }
    Depot:Crate c(42);
    return c.size();
}

/* a namespace member ALIAS (the uniform vocabulary — alias is a member kind here
   just as in a class body), and a namespace function whose SIGNATURE names a class
   defined LATER (Span) — a regression for the forward-ref fix: member signature
   types resolve after every name exists, so they may name any class regardless of
   order. */
Space {
    alias Count = int;
    const Count kCountBase = 12;
    Count counted() { return kCountBase; }
    int sized(Span^ s) { return s^.len(); }
}
Span(int len_) {
    int len() { return len_; }
}

int32 main() {

    int x = Space:bar();
    __println("x = " + x);

    int y = Space:kOne;
    __println("y = " + y);

    int w = scoped();
    __println("w = " + w);

    /* open space in local scope. */
    SubSpace {
        const int kThree = 3;
        int box() {
            return kThree;
        }

        /* nested space with same name */
        Nested {
            const int kFive = 5;
        }
    }
    int z = SubSpace:box();
    __println("z = " + z);

    int u = Space:Nested:kFour;
    __println("u = " + u);

    int v = SubSpace:Nested:kFive;
    __println("v = " + v);

    int mn = not_qualified();
    __println("mn = " + mn);

    int j = Local:local_best();
    __println("j = " + j);

    int k = Local:global_best();
    __println("k = " + k);

    /* qualifier not needed. */
    int h = kEight;
    __println("h = " + h);

    /* a class defined in a namespace — constructed and method-called like any
       class (Space:Gadget). */
    Space:Gadget g(7);
    __println("g = " + g.tick());

    /* a class whose fields forward-reference across file/namespace scope. */
    Crate cr(5);
    __println("cr = " + cr.total());

    /* a namespace class with ctor/dtor; the method reaches kGain bare. The
       ctor/dtor prints bracket the value inside meter_demo. */
    int amp = meter_demo();
    __println("amp = " + amp);

    /* a class in a nested namespace. */
    A:B:Knob kb(9);
    __println("knob = " + kb.turn());

    /* a namespace member alias used as a qualified type, + a namespace function
       returning it (Count = int). */
    Space:Count cn = 3;
    __println("cnt = " + Space:counted() + " cn = " + cn);

    /* a namespace function whose param is a class defined later (forward-ref). */
    Span sp(8);
    __println("sized = " + Space:sized(sp));

    /* an empty namespace class. */
    Space:Tag tg;
    __println("tag = " + tg.answer());

    /* transitive lifecycle: Wrapper's dtor runs because its field's class has one.
       ctor/dtor prints bracket the value inside wrap_demo. */
    int wr = wrap_demo();
    __println("wrap = " + wr);

    /* a namespace class on the heap. */
    Space:Cell^ hp = new Space:Cell(13);
    __println("heap = " + hp^.g());
    delete hp;

    /* an array of a namespace class (default field value). */
    Space:Cell arr[2];
    __println("arr = " + arr[0].g());

    /* a namespace class as a function return + reference param. */
    Space:Cell mc = make_cell(21);
    show_cell(mc);

    /* a class in a locally-opened namespace. */
    __println("lnc = " + local_ns_class());

    /* compile error: need qualifier */
    //-EXPECT-ERROR: 'foo' needs a namespace qualifier
    //int e1 = foo();
    //-EXPECT-ERROR: 'kOne' needs a namespace qualifier
    //int e2 = kOne;
    //-EXPECT-ERROR: 'kThree' needs a namespace qualifier
    //int e3 = kThree;

    /* compile error: not visible from this scope. */
    //-EXPECT-ERROR: 'kTwo' is not visible from this scope
    //int e4 = Space:kTwo;

    /* compile error: wrong qualifiers */
    //-EXPECT-ERROR: 'SubSpace:Nested' has no member 'kFour'
    //int e5 = SubSpace:Nested:kFour;
    //-EXPECT-ERROR: 'Space:Nested' has no member 'kFive'
    //int e6 = Space:Nested:kFive;

    /* compile error: cannot instantiate a namespace (even one holding a class). */
    //-EXPECT-ERROR: 'Space' is a namespace, not a type.
    //Space space;

    return 0;
}

/* compile error: ctor/dtor are method-shaped — illegal in a namespace body. */
//-EXPECT-ERROR: A constructor or destructor may only appear in a class body.
//NsHook { ~() { } }

/* compile error: a namespace body holds definitions, never a naked statement —
   a non-definition item is read as a (malformed) function. */
//-EXPECT-ERROR: Expected function name.
//NsNaked { __println("naked"); }

/* compile error: a field-bearing duplicate class within a namespace — a re-open
   cannot add fields. */
//-EXPECT-ERROR: Duplicate definition of class 'Dup'; a re-open cannot add fields
//Space { Dup(int a_){} Dup(int b_){} }

/* compile error: a class name colliding with a sibling const in a namespace. */
//-EXPECT-ERROR: Duplicate declaration of 'Clash'.
//Space { const int Clash = 1; Clash(int a_){} }

/* compile error: a namespace class field naming an unknown type. */
//-EXPECT-ERROR: Unknown type 'Nope'.
//Space { BadField(Nope n_){} }

/* compile error: a duplicate namespace member carries a 'first declared here' note
   pointing at the earlier declaration (the runner matches the note's text). */
//-EXPECT-ERROR: first declared here
//Space { const int kDupMember = 1; const int kDupMember = 2; }
