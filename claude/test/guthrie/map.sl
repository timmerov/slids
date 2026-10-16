/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
std c++ map

need minimum:
for (it : map) { }
clear()
find()
end()
insert()
size()
*/

import map;
import string;

Tree(
    Tree^ parent_,
    Tree^ left_,
    Tree^ right_,
    String key_,
    String value_
) {
    _() {}
    ~() {
        parent_^.remove();
    }

    Tree^ insert(String^ key, String^ value) {
        tree = new Tree;
        tree^.key_ = key^;
        tree^.value_ = value^;

        link = ^parent_;
        while() {
            where = link^;
            if (where == nullptr) {
                link^ = tree;
                break;
            }
            if (key^ < where^.key_) {
                link = ^where^.left_;
            } else {
                link = ^where^.right_;
            }
        }

        return tree;
    }

    void remove() {
        if (^self == nullptr) {
            return;
        }
        left_^.remove();
        right_^.remove();
        // is this safe? does this even work?
        delete ^self;
    }

    void print(int spaces = 0) {
        if (^self == nullptr) {
            return;
        }
        left_^.print(spaces+2);
        print_spaces(spaces);
        println(key_ + " - " + value_);
        right_^.print(spaces+2);
    }

    void print_spaces(int spaces) {
        if (spaces == 0) {
            return;
        }
        ::print("  ");
        for (i : 2..spaces+2) {
            ::print("| ");
        }
    }
}

void test_map() {
    Tree tree;
    tree.insert("USA", "Washington DC");
    tree.insert("Canada", "Ottawa");
    tree.insert("Mexico", "Mexico City");
    tree.insert("Belize", "Belmopan");
    tree.insert("Guatemala", "Guatemala City");
    tree.insert("Honduras", "Tegucigalpa");
    tree.insert("El Salvador", "San Salvador");
    tree.insert("Nicaragua", "Managua");
    tree.insert("Costa Rica", "San José");
    tree.insert("Panama", "Panama City");
    tree.parent_^.print();
}
