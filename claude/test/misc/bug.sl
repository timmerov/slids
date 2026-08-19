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

import algorithm;
import dump;
import string;
import vector;

Container(int a) {
    bool op<(Container^ rhs) {
        return (a < rhs^.a);
    }
}
alias Containers = Vector<Container>;

int32 main() {

    //print("Hello, World!");

    int iarr[2] = (2, 1);
    quicksort(iarr);
    //dump(#iarr[0]);
    //dump(#iarr[1]);
    println(String + iarr[0] + " < " + iarr[1]);

    float farr[2] = (4.1, 3.2);
    quicksort(farr);
    println(String + farr[0] + " < " + farr[1]);

    Containers carr;
    carr.resize(2);
    carr[0].a = 6;
    carr[1].a = 5;

    /* thes should all work but don't. */
    /* The 'mutable' qualifier applies only to a pointer (reference / iterator) or array parameter. */
    quicksort(carr);
    //quicksort<Containers>(carr);
    //quicksort<Containers>(^carr);
    /* A method call requires a class object; got 'T'. */
    //quicksort(^carr);

    println(String + carr[0].a + " < " + carr[1].a);

    return 0;
}
