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

    /* compile error: cannot instantiate a namespace. */
    //-EXPECT-ERROR: 'Space' is a namespace, not a type.
    //Space space;

    return 0;
}
