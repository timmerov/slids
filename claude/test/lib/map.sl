/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
test key/value map implementation.
*/

/*
claude says:
Map's only observers are insert() and print(), so each block prints the
whole tree (in order, one "| " per depth below the root's children).
ascending / descending / zig-zag orders drive the left rotations, the right
rotations, and the inner-child (case 2) rotations; the nested block runs two
maps' destructors at different scope exits.
*/

import+ map;
import string;
alias slids;

Map<String, String> : CapitalMap() {

/*
    this is the old code that assumed Map Key and Value
    could be converted to String.

    /* recursively print the tree. */
    void print(int spaces) {
        if (^self == nullptr) {
            return;
        }
        spaces += 2;

        lf = <MapIter> left_;
        lf^.print(spaces);

        String color = "red";
        if (color_ == kBlack) {
            color = "black";
        }

        if (spaces > 2) {
            ::print("  ");
            for (i : 4..spaces+2) {
                ::print("| ");
            }
        }
        println(key_ + " - " + value_ + " (" + color + ")");

        rt = <MapIter> right_;
        rt^.print(spaces);
    }
*/

    void print() {
        for (it : self) {
            println(it^.key_ + " - " + it^.value_);
        }
    }
}

/* a pointee for the pointer-valued map. */
Capital(int pop_ = 0) { }

int32 main() {
    println(String + "---countries---");
    {
        CapitalMap map;
        map.insert("USA", "Washington DC");
        map.insert("Canada", "Ottawa");
        map.insert("Mexico", "Mexico City");
        map.insert("Belize", "Belmopan");
        map.insert("Guatemala", "Guatemala City");
        map.insert("Honduras", "Tegucigalpa");
        map.insert("El Salvador", "San Salvador");
        map.insert("Nicaragua", "Managua");
        map.insert("Costa Rica", "San José");
        map.insert("Panama", "Panama City");
        map.print();
    }

    println(String + "---empty---");
    {
        CapitalMap map;
        map.print();
    }

    println(String + "---one insert---");
    {
        CapitalMap map;
        map.insert("one", "1");
        map.print();
    }

    println(String + "---ascending---");
    {
        CapitalMap map;
        map.insert("A", "alpha");
        map.insert("B", "bravo");
        map.insert("C", "charlie");
        map.insert("D", "delta");
        map.insert("E", "echo");
        map.insert("F", "foxtrot");
        map.insert("G", "golf");
        map.insert("H", "hotel");
        map.insert("I", "india");
        map.insert("J", "juliett");
        map.print();
    }

    println(String + "---descending---");
    {
        CapitalMap map;
        map.insert("J", "juliett");
        map.insert("I", "india");
        map.insert("H", "hotel");
        map.insert("G", "golf");
        map.insert("F", "foxtrot");
        map.insert("E", "echo");
        map.insert("D", "delta");
        map.insert("C", "charlie");
        map.insert("B", "bravo");
        map.insert("A", "alpha");
        map.print();
    }

    println(String + "---zig-zag---");
    {
        CapitalMap map;
        map.insert("E", "echo");
        map.insert("A", "alpha");
        map.insert("J", "juliett");
        map.insert("B", "bravo");
        map.insert("I", "india");
        map.insert("C", "charlie");
        map.insert("H", "hotel");
        map.insert("D", "delta");
        map.insert("G", "golf");
        map.insert("F", "foxtrot");
        map.print();
    }

    println(String + "---nested scopes---");
    {
        CapitalMap outer;
        outer.insert("outer", "first");
        {
            CapitalMap inner;
            inner.insert("inner", "second");
            inner.insert("innermost", "third");
            inner.print();
        }
        println(String + "inner gone");
        outer.print();
    }
    println(String + "outer gone");

    println(String + "---pointer values---");
    {
        /* insert takes `mutable Key` / `mutable Value`: a POINTER value is
           stored as given (not munged to point-to-const), so the map's
           pointers write through to the pointees. */
        Capital ottawa(1);
        Capital lima(10);
        Map<int, Capital^> pm;
        pm.insert(2, ^lima);
        pm.insert(1, ^ottawa);
        for (it : pm) {
            it^.value_^.pop_ += 100;
        }
        for (it : pm) {
            println(String + it^.key_ + " - " + it^.value_^.pop_);
        }
        println(String + "ottawa = " + ottawa.pop_);
    }

    /* compile error: a const pointer cannot be stored as a value of mutable
       pointers — the value type says the pointee is writable. */
    //-EXPECT-ERROR: Cannot pass a const value to a 'mutable' parameter
    //Capital nc(5);
    //(const Capital)^ ncp = ^nc;
    //Map<int, Capital^> npm;
    //npm.insert(1, ncp);
    //println(String + "inserted");

    return 0;
}
