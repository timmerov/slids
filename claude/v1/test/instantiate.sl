/*
class instantiation as a statement.

`Type;`, `Type();`, and `Type(args);` construct an unnamed instance.
A bare `Type;` at file scope is an unnamed global: constructed eagerly at
main's `global;`, destroyed at the close of that block. Inside a code block
it is an unnamed local — scope lifetime, LIFO with named locals, unwound on
early returns. An unnamed instance of a type with no constructor or
destructor is dead code — a compile error at either scope.
*/

Tag(int id_ = 0) {
    _() {
        __println("Tag:ctor " + id_);
    }
    ~() {
        __println("Tag:dtor " + id_);
    }
}

/* a nested class — instantiated as the statement `Outer:Inner(n)`. */
Outer(int x_ = 0) {
    Inner(int n_ = 0) {
        _() {
            __println("Inner:ctor " + n_);
        }
        ~() {
            __println("Inner:dtor " + n_);
        }
    }
}

/* a class for the unnamed-global test below. */
Beacon(int n_ = 0) {
    _() {
        __println("Beacon:ctor");
    }
    ~() {
        __println("Beacon:dtor");
    }
}

/* an inert class: no constructor, no destructor, no field needing one.
   An unnamed instance of it would have no effect — a compile error. */
Inert(int x_ = 0) {
}

/*
bare file-scope `Name;` — an unnamed global. No name to drive lazy
construction, so it is built eagerly at main's `global;` and destroyed at
the close of that block. Two of them — two ctors, two dtors.
*/
Beacon;
Beacon;

//-EXPECT-ERROR: has no constructor or destructor
//Inert;

/* early return: the instantiation's dtor must still run on the way out. */
void earlyReturn(bool stop) {
    Tag(100);
    if (stop) {
        return;
    }
    __println("earlyReturn: past the if");
}

int32 main() {
    /* unnamed globals construct here, at `global;`. */
    global;

    /* the three forms — all construct, all scope-lifetime. */
    Tag;            /* bare — zero-arg */
    Tag();          /* zero-arg */
    Tag(2);         /* one arg */

    /* interleaved with a named local — LIFO at scope exit. */
    Tag named(7);
    Tag(8);

    /* a guard scoped to a bare block — destructed at the block's end. */
    {
        Tag(50);
        __println("inside block");
    }
    __println("after block");

    /* nested-class instantiation statement. */
    Outer:Inner(9);

    /* a local class instantiated before its textual definition — the
       use-before-decl case, resolved within the block. */
    {
        Probe;
        Probe(int p_ = 0) {
            _() {
                __println("Probe:ctor");
            }
            ~() {
                __println("Probe:dtor");
            }
        }
    }

    earlyReturn(true);

    //-EXPECT-ERROR: has no constructor or destructor
    //{
    //    Inert;
    //}

    return 0;
}
