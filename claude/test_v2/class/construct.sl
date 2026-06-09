/*
test class construction.

    Class (field-list) {body}

every field of a class is initialized before the constructor is called.
the destructor is called at end of scope.
the ctor and dtor are called exactly once each.
unlike other object oriented languages, the ctor does not contstruct the object.
ctor/dtor are hooks executed at the start and end of the object's scope.

a class conceptually desugars to a namespace and a named tuple.
like a namespace, you may declare and define things within the class body.
naked code in the class body is a compile error.
the field list is a data tuple where the slots are accessed by name.

ctor/dtor are optional.
they must appear together.
defining one without the other is a compile error.
the compiler will generate default (possibly) ctor/dtor if the author does not.

when a class is instantiated, the fields are initialized to:
1. the corresponding slot of an initialization tuple.
2. the default value, if any.
3. the appropriate zero value.
the default value for a field is zero unless otherwise defined by the author.


examples:

    Class(int f1_, int f2_) {
        _() {
            __println("Class:ctor");
        }
        ~() {
            __println("Class:dtor");
        }
    }

notes:

naming conventions are optional.
they are never used to resolve parse.

this file covers basic class features.
specialized class features are handled elsewhere.
*/

/*
claude says:

tbd
*/

MyFirstClass(
    int a_,
    int b_ = 1
) {
}

void print(MyFirstClass^ cls) {
    __println("MyFirstClass: a=" + cls^.a_ + " b=" + cls^.b_);
}

int32 main() {

    MyFirstClass cls0;
    print(^cls0);

    MyFirstClass cls1();
    print(^cls1);

    MyFirstClass cls2(2);
    print(^cls2);

    MyFirstClass cls3 = 3;
    print(^cls3);

    MyFirstClass cls4(4,5);
    print(^cls4);

    MyFirstClass cls5 = (6, 7);
    print(^cls5);

    return 0;
}
