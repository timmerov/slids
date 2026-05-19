/*
resolve ambiguous semantics.
*/

/*
is this a forward class declaration? no, slids does not need them.
or a global variable? yes.
or a syntax error? no, but currently yes.
*/
Vex1;

/*
if that is a forward class declaration,
what's this?
n/a.
*/
Vex1;

/* compile error: no name and no ctor/dtor. */
//NoCtorDtor;

/*
can i force a forward declaration?
no and maybe.
*/
//Vex1() { }
//void undefined_function(ForwardDeclaredClass^ cls);

int32 main() {
    __println("Hello, World!");
    __println("two Vex1 ctors:");
    {
        global;
        {
            /*
            is this a forward class declaration? no.
            or a local variable? yes.
            or syntax error? no.
            */
            __println("two Vex3 ctors:");
            Vex3;
            Vex3;

            /*
            and these?
            currently a syntax error.
            same as Vex1.
            appears before class declaration.
            */
            __println("two Vex2 ctors:");
            Vex2;
            Vex2;

            /* compile error: no name and no ctor/dtor. */
            //NoCtorDtor;

            {
                /* this needs to be in scope. */
                //Vex2;
                //{{{{{{{ Vex2; }}}}}}}
            }

            Vex2(int x_ = 0) {
                _() {
                    __println("Vex2:ctor");
                }
                ~() {
                    __println("Vex2:dtor");
                }
            }
            __println("two Vex2 dtors:");
            __println("two Vex3 dtors:");
        }
        __println("two Vex1 dtors:");
    }
    __println("Goodbye, World!");

    return 0;
}

Vex1(int x_ = 0) {
    _() {
        __println("Vex1:ctor");
    }
    ~() {
        __println("Vex1:dtor");
    }
}

Vex3(int x_ = 0) {
    _() {
        __println("Vex3:ctor");
    }
    ~() {
        __println("Vex3:dtor");
    }
}

NoCtorDtor(int x_ = 0) {
}
