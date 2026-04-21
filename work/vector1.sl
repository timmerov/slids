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
        size = sizeof(v);
        __println("sizeof(v)<16>=" + size);
    }

    {
        __println("Vector<Value>:");
        Vector<Value> v;
        v.resize(3);
        size = sizeof(v);
        __println("sizeof(v)<16>=" + size);
    }

    {
        __println("Vector<String>:");
        Vector<String> v;
        v.resize(3);
        size = sizeof(v);
        __println("sizeof(v)<16>=" + size);
    }

    return 0;
}
