/*
use a template class declared in a heeader.
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
    {
        __println("Vector<int>:");
        Vector<int> v;
        v.resize(10);
    }

    {
        __println("Vector<Value>:");
        Vector<Value> v;
        v.resize(3);
    }

    {
        __println("Vector<String>:");
        Vector<String> v;
        v.resize(3);
    }

    return 0;
}
