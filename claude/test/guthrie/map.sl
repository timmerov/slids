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
            if (where^.left_ == nullptr) {
                where^.left_ = tree;
                break;
            }
            if (where^.right_ == nullptr) {
                where^.right_ = tree;
                break;
            }
            link = ^where^.left_;
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
        print_spaces(spaces);
        if (^self == nullptr) {
            println("none");
            return;
        }
        println(key_ + " - " + value_);
        spaces += 2;
        left_^.print(spaces);
        right_^.print(spaces);
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
