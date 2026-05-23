/*
built-in dtors.
*/

Value1(
    int a_ = 0
) {
    _() {
        __println("Value1:ctor");
    }
    ~() {
        __println("Value1:dtor");
    }
}

Value2(
    Value1 value_
) {
    _() {
        __println("Value2:ctor");
    }
    ~() {
        __println("Value2:dtor");
    }
}

NoBody(
    Value1 v_
) {
    /*
    compiler generates ctor/dtor
    which calls v_ ctor/dtor.

    this class is local to this file.
    so those methods are created here
    and used here.
    and nowhere else.

    but...
    if this class is declared in nobody.slh then
    where is the code for the compiler generated
    ctor/dtor?
    usually a .slh has an associated .sl that
    defines all of the methods declared in the .slh.
    but an empty class like this one, has no .sl.
    except that under the hood, it's not empty.
    it has a non-empty ctor that calls the field ctors.

    we cannot define it in every file that imports
    the header.
    we will get multiple link definitions.
    we could mark that as allowed.
    the duplicates would be small and identical.
    but that violates the spirit of slids.

    option 1:
    force the user to declare ctor/dtor in the .slh.
    and add a file to define the empty ctor/dtor.
    blech.

    option 2:
    handle this like a tuple.
    the ctor/dtor responsibilities are inline at
    the site of instantiation.

    option 2 wins.
    */
}

int32 main() {

    {
        int x = 42;
        x.~();
        __println("int has a dtor.");
    }

    {
        int^ p = nullptr;
        p.~();
        __println("int^ has a dtor.");
    }

    {
        __println("expected v1:ctor v2:ctor v2:dtor v1:dtor");
        Value2 value2;
    }

    {
        __println("expected v1:ctor x2 v1:dtor x3");
        __println("before Value1");
        Value1 value = 37;
        __println("before tuple");
        tuple = (42, value);
        /* tuple[1].a_ is not yet supported. */
        __println("(" + tuple[0] + "," + "tbd" + ")");
        __println("before tuple.~()");
        tuple.~();
        __println("after  tuple.~()");
    }

    {
        __println("expected ctors:1212 dtors:212121");
        __println("before Value2");
        Value2 value = 37;
        __println("before tuple");
        tuple = (42, value);
        __println("after  tuple");
        __println("(" + tuple[0] + "," + "tbd" + ")");
        __println("before tuple.~()");
        tuple.~();
        __println("after  tuple.~()");
    }

    return 0;
}
