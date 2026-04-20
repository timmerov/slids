/*
use a template class declared in a heeader.
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
}

int32 main() {
    /*{
        __println("Vector<int>:");
        Vector<int> vint;
        vint.resize(10);
    }*/

    {
        __println("Vector<Value>:");
        Vector<Value> vval;
        vval.resize(3);
    }

    return 0;
}
