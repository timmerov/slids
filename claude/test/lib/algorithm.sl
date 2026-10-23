/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
test algorithms.
*/

/*
claude says:
covers countof (arrays, a tuple array, Vector, String) and quicksort (order
shapes, 1 and 2 elements, float64 / char / int8 / uint64 above 2^63,
Vector<int>, Vector<String>, an empty Vector, arrays of classes with op<,
and Tracked, whose live count must be unchanged by the sort).
an array of a TU-local class (Item, Tracked) instantiates HERE, not in the
library, which could not spell it. a const container is rejected: here for a
local element type, in the library's --instantiate build for a header-visible
one (a demand keeps the buried const, as a call does).
DEFERRED (commented out in main):
- countof(int[2][3]): algorithm.slh's `T=[N]` arm matches one dimension
  only, so a 2-D array falls into ClassSet and fails on size(). What it
  should return is undecided.
*/

import+ algorithm;
import string;
import vector;
alias slids;

/* a small class ordered by op<. */
Item(int key_ = 0) {
    bool const op<(Item^ rhs) {
        return key_ < rhs^.key_;
    }
}

/* counts live instances: sorting must neither lose nor duplicate one. */
global int g_live = 0;

Tracked(int key_ = 0) {
    _() { ++g_live; }
    ~() { --g_live; }
    bool const op<(Tracked^ rhs) {
        return key_ < rhs^.key_;
    }
}

void show_int5(int a[5]) {
    String s = "";
    for (x : a) {
        s = s + " " + x;
    }
    println(s);
}

int32 main() {
    println(String + "---countof---");
    {
        int a5[5] = (1, 2, 3, 4, 5);
        println(String + "int[5] = " + countof(a5));
        int a1[1] = (7);
        println(String + "int[1] = " + countof(a1));
        Item items[3];
        println(String + "Item[3] = " + countof(items));
        (int, int) pairs[4] = ((1, 2), (3, 4), (5, 6), (7, 8));
        println(String + "(int, int)[4] = " + countof(pairs));
        /* DEFERRED: multi-dimensional countof (see claude says). */
        //int grid[2][3] = ((1, 2, 3), (4, 5, 6));
        //println(String + "int[2][3] = " + countof(grid));
        Vector<int> v;
        v.resize(3);
        println(String + "Vector<int> = " + countof(v));
        String str = "hello";
        println(String + "String = " + countof(str));
    }

    println(String + "---quicksort int[5]---");
    {
        int r[5] = (3, 5, 1, 4, 2);
        quicksort(r);
        show_int5(r);
        int s[5] = (1, 2, 3, 4, 5);
        quicksort(s);
        show_int5(s);
        int v[5] = (5, 4, 3, 2, 1);
        quicksort(v);
        show_int5(v);
        int e[5] = (7, 7, 7, 7, 7);
        quicksort(e);
        show_int5(e);
        int d[5] = (2, 1, 2, 1, 2);
        quicksort(d);
        show_int5(d);
    }

    println(String + "---quicksort small---");
    {
        int one[1] = (9);
        quicksort(one);
        println(String + "one: " + one[0]);
        int two[2] = (9, 8);
        quicksort(two);
        println(String + "two: " + two[0] + " " + two[1]);
    }

    println(String + "---quicksort types---");
    {
        float64 f[4] = (2.5, -1.25, 0.0, 1.5);
        quicksort(f);
        println(String + "float64: " + f[0] + " " + f[1] + " " + f[2] + " " + f[3]);
        char c[4] = ('d', 'a', 'c', 'b');
        quicksort(c);
        println(String + "char: " + c[0] + c[1] + c[2] + c[3]);
        int8 n[4] = (5, -128, 127, -1);
        quicksort(n);
        println(String + "int8: " + n[0] + " " + n[1] + " " + n[2] + " " + n[3]);
        /* above 2^63: the comparison must be unsigned. */
        uint64 u[3] = (18446744073709551615, 1, 9223372036854775808);
        quicksort(u);
        println(String + "uint64: " + u[0] + " " + u[1] + " " + u[2]);
    }

    println(String + "---quicksort Vector---");
    {
        Vector<int> vi;
        vi.resize(5);
        vi[0] = 3; vi[1] = 1; vi[2] = 4; vi[3] = 1; vi[4] = 5;
        quicksort(vi);
        String s = "Vector<int>:";
        for (x : vi) {
            s = s + " " + x;
        }
        println(s);

        Vector<String> vs;
        vs.resize(4);
        vs[0] = "pear"; vs[1] = "apple"; vs[2] = "fig"; vs[3] = "banana";
        quicksort(vs);
        String t = "Vector<String>:";
        for (String^ x : vs) {
            t = t + " " + x^;
        }
        println(t);

        Vector<int> empty;
        quicksort(empty);
        println(String + "empty: " + empty.size());
    }

    println(String + "---quicksort classes---");
    {
        Item items[4];
        items[0].key_ = 30; items[1].key_ = 10; items[2].key_ = 40; items[3].key_ = 20;
        quicksort(items);
        println(String + "Item: " + items[0].key_ + " " + items[1].key_ + " "
            + items[2].key_ + " " + items[3].key_);
    }
    {
        Tracked tr[5];
        tr[0].key_ = 5; tr[1].key_ = 2; tr[2].key_ = 4; tr[3].key_ = 1; tr[4].key_ = 3;
        println(String + "live before: " + g_live);
        quicksort(tr);
        println(String + "live after: " + g_live);
        println(String + "Tracked: " + tr[0].key_ + " " + tr[1].key_ + " "
            + tr[2].key_ + " " + tr[3].key_ + " " + tr[4].key_);
    }
    println(String + "live at end: " + g_live);

    return 0;
}

/* countof needs an array or a class with size(). */
//-EXPECT-ERROR: countof
//int32 neg_countof_int() {
//    int x = 3;
//    return <int32> countof(x);
//}

/* countof's class specialization calls size(). */
//-EXPECT-ERROR: size
//NoSize(int x_ = 0) { }
//int32 neg_countof_nosize() {
//    NoSize ns;
//    return <int32> countof(ns);
//}

/* quicksort compares elements with op<. */
//-EXPECT-ERROR: <
//NoLess(int x_ = 0) { }
//void neg_sort_noless() {
//    NoLess arr[2];
//    quicksort(arr);
//}

/* quicksort writes its container, so a const one is rejected. The elements are a
   TU-local class so the instance builds HERE, where the runner can see the error
   (a const int[3] is demanded from the library and rejected in ITS build). */
//-EXPECT-ERROR: Cannot write to a const value
//void neg_sort_const() {
//    const Item arr[2] = ((2), (1));
//    quicksort(arr);
//}
