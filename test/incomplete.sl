/*
incomplete classes.

including partially complete classes.
these are all in-file.
not checking when the open is in a header,
the friends are in a second header,
and the closing is in a source file.
*/

/* declare the incomplete class. */
Incomplete(
    int x_ = 1,
    /* trailing ellipsis means this class is incomplete. */
    ...
) {
    _();
    ~();
    void greet();
}

/* reopen and declare friend fields and methods. */
Incomplete (
    ...,
    int y_,
    /* trailing ellipsis means this class is incomplete. */
    ...
) {
    void friend();
}

/* normal reopen with no new fields. */
Incomplete {
    void reopen() {
        __println("Grand reopening!");
    }
}

/* weird reopen with no new fields. */
Incomplete(
    /* the leading ellipsis means this class is incomplete. */
    ...,
    /* trailing ellipsis means this class is incomplete. */
    ...
) {
    void weirdness();
}

/* reopen and complete the class. */
Incomplete(
    /* the leading ellipsis means this class is incomplete. */
    ...,
    int z_
    /* no trailing ellipsis means this class is now complete. */
) {
    _() {
        __println("Incomplete:ctor");
    }

    ~() {
        __println("Incomplete:dtor");
    }

    void greet() {
        __println("Hello, World!");
    }

    void friend() {
        __println("Best friends forever!");
    }

    void weirdness() {
        __println("You can never have too many ellipses.");
    }
}

/* open another incomplete class. */
Incomplete2(
    /* trailing ellipsis means this class is incomplete. */
    ...
) {
}

/* reopen and complete the class. */
Incomplete2(
    /* the leading ellipsis means this class is incomplete. */
    ...
    /* no trailing ellipsis means this class is now complete. */
) {
}

int32 main() {
    /*
    y and z have no default value.
    but they're hidden.
    */
    Incomplete inc;
    inc.greet();
    inc.friend();
    inc.reopen();
    inc.weirdness();

    /* init x */
    Incomplete incx(10);

    /* init x,y: compile error */
    //Incomplete incxy(20, 21);

    /* init x,y,z: compile error */
    //Incomplete incxyz(30, 31, 32);

    /* check alternative method to closing. */
    Incomplete2 inc2;
    size = sizeof(inc2);
    __println("sizeof(inc2)<0>=" + size);

    /* builtin sizeof variable */
    size = sizeof(inc);
    __println("sizeof(inc)<12>=" + size);

    /* builtin sizeof type */
    size = sizeof(Incomplete);
    __println("sizeof(Incomplete)<12>=" + size);

    /* sizeof by class method */
    size = inc.sizeof();
    __println("inc.sizeof()<12>=" + size);

    /* sizeof by type */
    size = Incomplete.sizeof();
    __println("Incomplete.sizeof()<12>=" + size);

    return 0;
}
