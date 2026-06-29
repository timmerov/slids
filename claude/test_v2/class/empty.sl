/*
test classes with methods but no fields.

empty classes may be instantiated.
they need to have a minimum size.
so they can be distinguished from each other.
*/

/*
claude says:

an empty class (no fields) is instantiable, so distinct instances must occupy
distinct storage — `^a == ^b` is observable. it therefore gets a 1-byte minimum
size (C++'s empty-class rule): the kSlid lowers to `{ i8 }`, not `{  }` (0 bytes).
- a single var: alloca is 1 byte; two locals get distinct addresses.
- an array `Empty c[2]`: elements stride by 1, so `^c[0] != ^c[1]`. with a
  0-byte element the stride would be 0 and every element would alias.
- a tuple `(Empty(), Empty())`: slots sit at offsets 0 and 1, not both at 0.
- sizeof(Empty) is 1 (the per-class __$sizeof helper GEPs the padded struct);
  new Empty[n] strides by 1.
the padding byte is never named (the class has no fields), so field-init and
.field access are unaffected. only a class is padded — an empty tuple is not.

note: `intptr p = ^a;` takes the address as an integer; comparing intptr p,q is
the address-identity check. a pointer flows into an intptr lvalue via ptrtoint.
*/

Empty() {
    _() { __println("Empty:ctor"); }
    ~() { __println("Empty:dtor"); }
}

int32 main() {

    {
        Empty a;
        Empty b;
        intptr p = ^a;
        intptr q = ^b;
        __println("same address: " + (p==q));
    }

    {
        Empty c[2];
        intptr p = ^c[0];
        intptr q = ^c[1];
        __println("same address: " + (p==q));
    }

    {
        tuple = (Empty(), Empty());
        intptr p = ^tuple[0];
        intptr q = ^tuple[1];
        __println("same address: " + (p==q));
    }

    return 0;
}
