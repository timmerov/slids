/*
test class as namespace.

constants, globals, aliases, enums, etc may be defined inside a class body.

classes aren't exactly namespaces.
you can't define functions inside a class.
you can't alias the class as a namespace - it's a type.
*/

/*
claude says:

a class is both a type and a namespace. its body holds member definitions —
aliases, consts, and enums. a method is a member too; what's excluded is a
non-member (free) function. qualify members with the class name: Space:Float (a
member type), Space:kPi (a member const), Space:Count:kOne (an enum member —
the enum keeps its name in the path; members don't flatten into the class).

a type-alias to a class sees through to both facets: via alias Time = Space,
Time:Count (type) and Time:Count:kZero (value) both resolve.

alias Space; is rejected — a class is a type, not an importable namespace.
*/

Space(int x_) {
    alias Float = float;
    const Float kPi = 3.14;
    enum int Count (kZero, kOne, kTwo, kThree);
}

alias Time = Space;

int32 main() {

    Space:Float press = 101.325;
    // should be Space:Float
    __println(##type(press) + " pressure = " + press);

    // should be const Space:Float
    __println(##type(Space:kPi) + " pi = " + Space:kPi);

    __println("count: " + Space:Count:kOne + " " + Space:Count:kTwo + " " + Space:Count:kThree);

    // should be Time:Count
    Time:Count zero = Time:Count:kZero;
    __println(##type(zero) + " zero = " + zero);

    return 0;
}

/* compile errors. */

// a class is a type, not an importable namespace.
//-EXPECT-ERROR: A class is a type, not an importable namespace.
//alias Space;
