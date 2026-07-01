/*
test refined classes.

classes may be refined in a run-time scope.
however, this is an illusion that looks like re-opening.
an entirely new derived class that usurps the original class is created.
this gives the appearance of re-opening.
and makes the syntax friendly to humans.
the closed form: Class() { ... }
and external form: void Class:method() { ... }
are valid.

    Class(int x) { }
    Class cls;
    void fn2() {
        Class() { alias Integer = int; }
        void Class:method() { }
        cls.method();
    }

this desugars to:

    Class(int x) { }
    Class cls;
    void fn2() {
        Class : $Class() {
            void method() { }
        }
        (<Class:$Class^> ^cls)^.method();
    }
*/

/*
claude says:

tbd
*/

int32 main() {

    return 0;
}
