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
    Vector<Value> valvec;
    Vector<String> strvec;

    intvec.resize(3);
    valvec.resize(3);
    strvec.resize(3);
    println(String + "intvec.size()<10>=" + intvec.size());
    println(String + "valvec.size()<10>=" + valvec.size());
    println(String + "strvec.size()<10>=" + strvec.size());

    return 0;
}
