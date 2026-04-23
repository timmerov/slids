/*
use a template class declared in a header.
*/

import string;
import vector;

Value(
    int x_
) {
    _() {
        __println("Value:ctor");
    }
    ~() {
        __println("Value:dtor");
    }
}

int32 main() {
    Vector<int> intvec;
    Vector<Value> valuevec;
    Vector<String> stringvec;

    intvec.resize(10);
    intptr size = intvec.size();
    println(String + "intvec.size()<10>=" + size);

    return 0;
}
