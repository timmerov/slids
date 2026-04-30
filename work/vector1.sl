/*
use a template class declared in a header.
*/

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
    op<-(Value^ rhs) {
        __println("Value::move");
        x_ = rhs^.x_;
    }
}

int32 main() {
    __println("---Vector<int>---");
    {
        Vector<int> intvec;
        __println("resize(3)");
        intvec.resize(3);
    }
    __println("---Vector<Value>---");
    {
        Vector<Value> valvec;
        __println("reserve(5)");
        valvec.reserve(3);
        __println("resize(3)");
        valvec.resize(3);
        __println("---dtors---");
    }
    __println("----------");

    return 0;
}
