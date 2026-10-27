/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
test the Vector class.
*/

/*
claude says:
covers Vector<int> and Vector<Value>: resize / reserve, copy and move,
indexing and iteration, insert / append, remove. g_count tracks live Values,
so every block must end with the count it started with.
Value has no op<, and Vector's comparisons need T's op< — a template METHOD is
built only when used, so Vector<Value> is fine until something compares two of
them (the negative at the end). Ranked has op<; its vectors compare.
the stem matches the library's (lib/hdr/vector.sl), so the Makefile builds
this file as t_vector — the instantiated library lands as vector.ll in the
same build directory — and the lexer still loads the library's template
source, because it is a different file by path.
*/

import string;
import+ vector;
alias slids;

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

/* an element class WITH op< — Vector<Ranked> may compare. */
Ranked(
    int r_ = 0
) {
    bool const op<(Ranked^ other) {
        return r_ < other^.r_;
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
    println(String + "---compare---");
    {
        /* primitive elements compare. */
        Vector<int> ia;
        ia.resize(2);
        ia[0] = 1;
        ia[1] = 2;
        Vector<int> ib;
        ib.resize(2);
        ib[0] = 1;
        ib[1] = 3;
        println(String + "int: == " + (ia == ib) + " < " + (ia < ib)
            + " > " + (ia > ib));
        /* class elements with op< compare. */
        Vector<Ranked> ra;
        ra.resize(1);
        ra[0].r_ = 5;
        Vector<Ranked> rb;
        rb.resize(1);
        rb[0].r_ = 4;
        println(String + "Ranked: == " + (ra == rb) + " < " + (ra < rb)
            + " > " + (ra > rb));
    }
    println(String + "---pointer elements---");
    {
        /* append/insert take `mutable T`: a POINTER element is stored as
           given (not munged to point-to-const), so the vector's pointers
           write through to the pointees. destroying the vector is a no-op on
           each pointer — the pointees are untouched. */
        Ranked r1;
        r1.r_ = 1;
        Ranked r2;
        r2.r_ = 2;
        Ranked r3;
        r3.r_ = 3;
        Vector<Ranked^> pv;
        pv.append(^r1);
        pv.append(^r3);
        pv.insert(1, ^r2);
        pv[0]^.r_ = 10;
        print(String + "pv = [");
        for (Ranked^ p : pv) {
            print(String + " " + p^.r_);
        }
        println(String + " ]");
        println(String + "r1 = " + r1.r_);
    }
    println(String + "----------");

    /* OPEN: a const-pointee vector should take const pointers, but
       `mutable T` with T = `(const Ranked)^` still rejects a const argument
       ("Cannot pass a const value to a 'mutable' parameter"). */
    //-EXPECT-ERROR-DEFERRED: mutable T over a const-pointee T rejects const args
    //Ranked cr3;
    //(const Ranked)^ cr = ^cr3;
    //Vector<(const Ranked)^> cv;
    //cv.append(cr);
    //println(String + "cv = [ " + cv[0]^.r_ + " ]");

    /* compile error: a const pointer cannot be stored in a vector of mutable
       pointers — the element type says the pointee is writable. */
    //-EXPECT-ERROR: Cannot pass a const value to a 'mutable' parameter
    //Ranked nr;
    //(const Ranked)^ ncr = ^nr;
    //Vector<Ranked^> npv;
    //npv.append(ncr);
    //println(String + "npv = " + npv.size());

    /* compile error: comparing vectors whose element class has no op< builds
       Vector<Value>'s comparison methods, which need it. */
    //-EXPECT-ERROR: Operator '<' is not defined on class 'Value'.
    //Vector<Value> na;
    //Vector<Value> nb;
    //bool nlt = na < nb;
    //println(String + "nlt = " + nlt);

    return 0;
}
