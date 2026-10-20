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

import map;
import string;

int32 main() {
    println(String + "---countries---");
    {
        Map<String, String> map;
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
        Map<String, String> map;
        map.print();
    }

    println(String + "---one insert---");
    {
        Map<String, String> map;
        map.insert("one", "1");
        map.print();
    }

    println(String + "---ascending---");
    {
        Map<String, String> map;
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
        Map<String, String> map;
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
        Map<String, String> map;
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
        Map<String, String> outer;
        outer.insert("outer", "first");
        {
            Map<String, String> inner;
            inner.insert("inner", "second");
            inner.insert("innermost", "third");
            inner.print();
        }
        println(String + "inner gone");
        outer.print();
    }
    println(String + "outer gone");

    return 0;
}
