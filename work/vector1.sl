/*
use a template class declared in a header.
*/

import vector;

global int g_count = 0;

Value(
    int x_
) {
    _() {
        ++g_count;
        //__println("Value:ctor");
    }
    ~() {
        --g_count;
        //__println("Value:dtor");
    }
    op<--(mutable Value^ rhs) {
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
    __println("ctor/dtor count = " + g_count);
    __println("---Vector<Value>---");
    {
        Vector<Value> valvec;
        __println("resize(3)");
        valvec.resize(3);
        __println("reserve(5)");
        valvec.reserve(5);
        __println("resize(7)");
        valvec.resize(7);
        __println("resize(5)");
        valvec.resize(5);
        __println("---dtors---");
    }
    __println("ctor/dtor count = " + g_count);
    __println("---copy/move---");
    {
        Vector<Value> a;
        __println("resize(3)");
        a.resize(3);
        __println("copy");
        Vector<Value> b = a;
        __println("move");
        Vector<Value> c <-- a;
        __println("a.size() = " + a.size());
        __println("b.size() = " + b.size());
        __println("c.size() = " + c.size());
        __println("---dtors---");
    }
    __println("ctor/dtor count = " + g_count);
    __println("---index---");
    {
        Vector<Value> vec;
        __println("resize(3)");
        vec.resize(3);
        vec[0].x_ = 10;
        vec[1].x_ = 20;
        vec[2].x_ = 40;
        __print("vec (by ref) = [");
        for (Value^ v : vec) {
            __print(" " + v^.x_);
        }
        __println(" ]");
        Value v;
        __print("vec (by value) = [");
        for (v : vec) {
            __print(" " + v.x_);
        }
        __println(" ]");
        __println("---dtors---");
    }
    __println("ctor/dtor count = " + g_count);
    __println("---insert/append---");
    {
        Vector<Value> vec;
        Value val;
        val.x_ = 100;
        vec.append(val);
        vp = vec.append();
        vp^.x_ = 400;
        val.x_ = 200;
        vec.insert(1, ^val);
        vp = vec.insert(2);
        vp^.x_ = 300;
        __print("vec = [");
        for (Value^ v : vec) {
            __print(" " + v^.x_);
        }
        __println(" ]");
        __println("---dtors---");
    }
    __println("ctor/dtor count = " + g_count);
    __println("---remove---");
    {
        Vector<Value> vec;
        vec.resize(7);
        for (i : 0..7) {
            vec[i].x_ = i;
        }
        vec.remove(2, 3);
        __print("vec = [");
        for (Value^ v : vec) {
            __print(" " + v^.x_);
        }
        __println(" ]");
        __println("---dtors---");
    }
    __println("ctor/dtor count = " + g_count);
    __println("----------");

    return 0;
}
