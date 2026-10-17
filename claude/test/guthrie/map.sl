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
    enum Color (kHost, kRed, kBlack);

    _() {}
    ~() {
        if (color_ == kHost) {
            parent_^.remove();
        }
    }

    /* insert key/value pair into the tree. */
    void insert(String^ key, String^ value) {

        /* create a new red node. */
        tree = new Tree;
        tree^.color_ = kRed;
        tree^.key_ = key^;
        tree^.value_ = value^;

        /* the root node is black. */
        if (parent_ == nullptr) {
            parent_ = tree;
            tree^.color_ = kBlack;
            tree^.parent_ = parent_;
            return;
        }

        /* insert the new node. */
        parent = parent_;
        while() {
            if (key^ < parent^.key_) {
                if (parent^.left_ == nullptr) {
                    parent^.left_ = tree;
                    tree^.parent_ = parent;
                    break;
                }
                parent = parent^.left_;
            } else {
                if (parent^.right_ == nullptr) {
                    parent^.right_ = tree;
                    tree^.parent_ = parent;
                    break;
                }
                parent = parent^.right_;
            }
        }

        /* we're done if our parent is black. */
        if (parent^.color_ == kBlack) {
            return;
        }

        /*
        we just added a red child to a red parent.
        that's not allowed.
        */

        /*
        if parent's sibling is red then all we need
        to do is recolor some nodes.
        */
        done = false;
        while() {
            granny = parent^.parent_;
            //println(String + "parent:" + parent^.color_ + ": " + parent^.key_);
            //println(String + "granny:" + granny^.color_ + ": " + granny^.key_);

            /* stop if parent or granny is root. */
            if (parent^.color_ == kHost || granny^.color_ == kHost) {
                break;
            }

            /* we're done if there is no sibling. */
            aunt = granny^.left_;
            uncle = granny^.right_;
            if (aunt == nullptr || uncle == nullptr) {
                break;
            }
            /* we're done if either is black. */
            if (aunt^.color_ == kBlack || uncle^.color_ == kBlack) {
                break;
            }
            //println(String + "aunt  :" + aunt^.color_ + ": " + aunt^.key_);
            //println(String + "uncle :" + uncle^.color_ + ": " + uncle^.key_);

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
            parent = granny^.parent_;
        }
        if (done) {
            return;
        }
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
