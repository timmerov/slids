/*
last one standing.

100 people in a circle.
1 tags out 2.
3 tags out 4.
...
99 tags out 100.
1 tags out 3.
5 tags out 7.
...
97 tags out 99.
1 tags out 5.
5 tags out 9.
...
89 tags out 93.
97 tags out 1.
and things get interesting.
*/

import string;
import vector;


int32 main() {

    println(String + "Last one standing:");

    Vector<int> circle;
    circle.reserve(200);
    for (i : 1..<=100) {
        circle.append(i);
    }

    for (i=0) (i+1 < circle.size()) {i+=2;} {
        a = circle[i];
        b = circle[i+1];
        circle.append(a);
        println(String + a + " taps out " + b + ".");
    }

    winner = circle[circle.size()-1];
    println(String + "Last one standing is: " + winner);

    return 0;
}
