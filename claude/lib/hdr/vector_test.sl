/*
use a template class declared in a header.
*/

import string;

import vector;

global int g_count = 0;

Value(
    int x_
) {
    _() {
        ++g_count;
        //println(String + "Value:ctor");
    }
    ~() {
        --g_count;
        //println(String + "Value:dtor");
    }
    op<--(mutable Value^ rhs) {
        println(String + "Value::move");
        x_ = rhs^.x_;
    }
}

int32 main() {
    println(String + "---Vector<int>---");
    {
        Vector<int> intvec;
        println(String + "resize(3)");
        intvec.resize(3);
    }
    println(String + "ctor/dtor count = " + g_count);
    println(String + "---Vector<Value>---");
    {
        Vector<Value> valvec;
        println(String + "resize(3)");
        valvec.resize(3);
        println(String + "reserve(5)");
        valvec.reserve(5);
        println(String + "resize(7)");
        valvec.resize(7);
        println(String + "resize(5)");
        valvec.resize(5);
        //println(String + "---dtors---");
    }
    println(String + "ctor/dtor count = " + g_count);
    println(String + "---copy/move---");
    {
        Vector<Value> a;
        println(String + "resize(3)");
        a.resize(3);
        println(String + "copy");
        Vector<Value> b = a;
        println(String + "move");
        Vector<Value> c <-- a;
        println(String + "a.size() = " + a.size());
        println(String + "b.size() = " + b.size());
        println(String + "c.size() = " + c.size());
        //println(String + "---dtors---");
    }
    println(String + "ctor/dtor count = " + g_count);
    println(String + "---index---");
    {
        Vector<Value> vec;
        println(String + "resize(3)");
        vec.resize(3);
        vec[0].x_ = 10;
        vec[1].x_ = 20;
        vec[2].x_ = 40;
        print(String + "vec (by ref) = [");
        for (Value^ v : vec) {
            print(String + " " + v^.x_);
        }
        println(String + " ]");
        //println(String + "---dtors---");
    }
    println(String + "ctor/dtor count = " + g_count);
    println(String + "---insert/append---");
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
        print(String + "vec = [");
        for (Value^ v : vec) {
            print(String + " " + v^.x_);
        }
        println(String + " ]");
        //println(String + "---dtors---");
    }
    println(String + "ctor/dtor count = " + g_count);
    println(String + "---remove---");
    {
        Vector<Value> vec;
        vec.resize(7);
        for (i : 0..7) {
            vec[i].x_ = i;
        }
        vec.remove(2, 3);
        print(String + "vec = [");
        for (Value^ v : vec) {
            print(String + " " + v^.x_);
        }
        println(String + " ]");
        //println(String + "---dtors---");
    }
    println(String + "ctor/dtor count = " + g_count);
    println(String + "----------");

    return 0;
}
