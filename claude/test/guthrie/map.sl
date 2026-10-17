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

/*
red black tree.
1. the root is black.
2. red nodes have no red children.
3. every path from a node to its null leaves have
the same number of black nodes.
4. inserted nodes (after the first) are red.
if this breaks rule 3, then move and recolor nodes.
*/
Tree(
    Color color_ = kHost,
    Tree^ parent_ = nullptr,
    Tree^ left_ = nullptr,
    Tree^ right_ = nullptr,
    String key_,
    String value_
) {
    enum Color (kHost, kRed, kBlack, kNull);

    _() {}
    ~() {
        if (color_ == kHost) {
            parent_^.remove();
        }
    }

    /* color getter. */
    Color color() {
        if (^self == nullptr) {
            return kNull;
        }
        return color_;
    }

    /* parent getter */
    Tree^ parent() {
        if (^self == nullptr) {
            return nullptr;
        }
        if (parent_ == nullptr) {
            return nullptr;
        }
        if (parent_^.color_ == kHost) {
            return nullptr;
        }
        return parent_;
    }

    /* left getter */
    Tree^ left() {
        if (^self == nullptr) {
            return nullptr;
        }
        return left_;
    }

    /* right getter */
    Tree^ right() {
        if (^self == nullptr) {
            return nullptr;
        }
        return right_;
    }

    /* insert key/value pair into the tree. */
    void insert(String^ key, String^ value) {

        /* create a new red node. */
        child = new Tree;
        child^.color_ = kRed;
        child^.key_ = key^;
        child^.value_ = value^;

        /* the root node is black. */
        if (parent_ == nullptr) {
            parent_ = child;
            child^.color_ = kBlack;
            child^.parent_ = parent_;
            return;
        }

        /* insert the new node. */
        father = parent_;
        while() {
            if (key^ < father^.key_) {
                if (father^.left_ == nullptr) {
                    father^.left_ = child;
                    child^.parent_ = father;
                    break;
                }
                father = father^.left_;
            } else {
                if (father^.right_ == nullptr) {
                    father^.right_ = child;
                    child^.parent_ = father;
                    break;
                }
                father = father^.right_;
            }
        }

        /* we're done if our parent is black. */
        if (father^.color_ == kBlack) {
            return;
        }

        /*
        we just added a red child to a red parent.
        that's not allowed.
        */

        /*
        if parent's sibling is red then all we need
        to do is recolor some nodes.
        use getter functions when walking the tree.
        */
        done = false;
        while() {
            granny = father^.parent();

            /* stop if child or father is root. */
            if (father == nullptr || granny == nullptr) {
                break;
            }

            /* we're done recoloring if either sibling is not red. */
            aunt = granny^.left();
            uncle = granny^.right();
            if (aunt^.color() != kRed || uncle^.color() != kRed) {
                break;
            }

            /*
            at least one set of siblings were both red.
            so we don't have to check for other fix-ups.
            */
            done = true;

            /* flip colors. */
            granny^.color_ = kRed;
            aunt^.color_ = kBlack;
            uncle^.color_ = kBlack;

            /*
            we just flipped granny's color to red.
            go up two generations and repeat.
            */
            father = granny^.parent();

        }
        if (done) {
            return;
        }

        /*
        adjust the tree if we are an inner child.
             granny(b)            granny(b)
            /        \            /       \
        parent(r)  uncle(b)    tree(r)  uncle(b)
            \                   /
            child(r)         child(r)
        */
    }

    /* recursively delete children then self. */
    void remove() {
        if (^self == nullptr) {
            return;
        }
        left_^.remove();
        right_^.remove();
        /* return immediately. no longer in a safe state. */
        delete ^self;
    }

    /* recursively pretty print the tree. */
    void print(int spaces = 0) {
        if (^self == nullptr) {
            return;
        }
        left_^.print(spaces+2);

        String color = "red";
        if (color_ == kBlack) {
            color = "black";
        }
        print_spaces(spaces);
        println(key_ + " - " + value_ + " (" + color + ")");

        right_^.print(spaces+2);
    }

    /* print vertical bars to associate related tree nodes. */
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
