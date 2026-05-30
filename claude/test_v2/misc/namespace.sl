/*
test namespaces.

namespaces may be opened in any scope.
they may be re-opened in any scope.
even different scopes.
things defined in a namespace inherit the lifetime of the scope.
*/

/*
/* open in global scope. */
Space {
    const int kOne = 1;
    int foo() {
        return kOne;
    }
}

/* reopen space */
Space {
    int bar() {
        return foo();
    }
}

int scoped() {
    /* reopen in local scope. */
    Space {
        const int kTwo = 2;
    }
    return Space:kTwo;
}
*/

int32 main() {
/*
    int x = Space:bar();
    __println("x = " + x);

    int y = Space:kOne;
    __println("y = " + y);

    /* open space in local scope. */
    SubSpace {
        const int kThree = 3;
        int box() {
            return kThree;
        }
    }
    int z = SubSpace:box();
    __println("z = " + z);

    /* compile error: need qualifier */
    int e1 = foo();
    int e2 = kOne;
    int e3 = kThree;
    /* compile error: dropped out of scope. */
    int e4 = Space:kTwo;
*/
    return 0;
}
