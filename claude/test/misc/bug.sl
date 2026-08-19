/*
the bug of the day.
*/

/*
claude is forbidden from modifying this file and its golden file.
claude is forbidden from whining about the user changing this file.
claude is forbidden from whining about this file not compiling.
claude is forbidden to mention this file unless the user specifically
puts it in scope.
*/

import dump;
import string;
import vector;

Container(int a) {
    bool op<(Container^ rhs) {
        return (a < rhs^.a);
    }
}
alias Containers = Vector<Container>;

alias SignedIntegers = <int|int8|int16|int32|int64>;
alias UnsignedIntegers = <uint|uint8|uint16|uint32|uint64>;
alias Integers = <SignedIntegers|UnsignedIntegers>;
alias Floats = <float|float32|float64>;
alias Numbers = <Integers|Floats>;
alias Pointers = <^|[]>;
alias Primitives = <bool|char|intptr|Numbers|Pointers>;
alias Arrays = <[N]>;
alias Tuples = <()>;
alias Classes = !<Arrays|Tuples|Primitives>;

intptr countof<T=[N]>(T arg) {
    return N;
}

intptr countof<T=Classes>(T arg) {
    return arg.size();
}

void sort<T>(mutable T container) {

    /* this doesn't work for non-class types like int[N]. */
    sz = countof<T>(container);
    subsort(0, sz-1);

    void subsort(intptr first, intptr last) {
        /* done */
        if (first >= last) {
            return;
        }

        /* grab the pivot. */
        pivot = container[first];

        /* loop. */
        limit = first;
        scan = last;
        while {
            /*
            small values stay in place.
            big values move to the end.
            */
            value = container[scan];
            if (pivot < value) {
                --scan;
            } else {
                container[limit] = value;
                ++limit;
                container[scan] = container[limit];
            }
        } (limit < scan);

        /* place the pivot at the correct place. */
        container[scan] = pivot;

        /* recurse. */
        subsort(first, scan-1);
        subsort(scan+1, last);
    }
}

int32 main() {

    //print("Hello, World!");

    int iarr[2] = (2, 1);
    sort(iarr);
    //dump(#iarr[0]);
    //dump(#iarr[1]);
    println(String + iarr[0] + " < " + iarr[1]);

    float farr[2] = (4.1, 3.2);
    sort(farr);
    println(String + farr[0] + " < " + farr[1]);

    Containers carr;
    carr.resize(2);
    carr[0].a = 6;
    carr[1].a = 5;

    /* thes should all work but don't. */
    /* The 'mutable' qualifier applies only to a pointer (reference / iterator) or array parameter. */
    //sort(carr);
    //sort<Containers>(carr);
    //sort<Containers>(^carr);
    /* A method call requires a class object; got 'T'. */
    //sort(^carr);

    println(String + carr[0].a + " < " + carr[1].a);

    return 0;
}
