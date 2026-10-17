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
            child^.parent_ = ^self;
            return;
        }

        /* insert the new node. */
        Tree^ parent = parent_;
        while() {
            if (key^ < parent^.key_) {
                if (parent^.left_ == nullptr) {
                    parent^.left_ = child;
                    child^.parent_ = parent;
                    break;
                }
                parent = parent^.left_;
            } else {
                if (parent^.right_ == nullptr) {
                    parent^.right_ = child;
                    child^.parent_ = parent;
                    break;
                }
                parent = parent^.right_;
            }
        }

        /*
        =tsc=
        logic wrong.
        while parent color is red:
            if case 1
                fix
            else
                if case 2
                    fix
                fix case 3
        */

        /*
        case 0:
        we're done if our parent is black.
        */
        if (parent^.color_ == kBlack) {
            return;
        }

        /*
        we just added a red child to a red parent.
        that's not allowed.
        */

        /*
        case 1:
        if parent's sibling is red then all we need
        to do is recolor some nodes.
        use getter functions when walking the tree.
        */
        done = false;
        while() {
            granny = parent^.parent();

            /* stop if child or parent is root. */
            if (parent == nullptr || granny == nullptr) {
                break;
            }

            /* we're done recoloring
             if either sibling is not red. */
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
            parent = granny^.parent();

        }
        if (done) {
            return;
        }

        /*
        case 2:
        adjust the tree if child is "inner".

             granny(b)    ->      granny(b)
            /        \            /       \
        aunt(r)  uncle(b)    child(r)  uncle(b)
            \                   /
            child(r)         aunt(r)

             granny(b)    ->      granny(b)
            /        \            /      \
        aunt(b)  uncle(r)    aunt(b)  child(r)
                  /                      \
            child(r)                   uncle(r)

        and swap child with aunt or uncle.
        */
        granny = parent^.parent();
        aunt = granny^.left();
        uncle = granny^.right();
        if (aunt^.right() == child) {
            granny^.left_ = child;
            child^.parent_ = granny;
            child^.left_ = aunt;
            aunt^.parent_ = child;
            aunt^.right_ = nullptr;
            child <--> aunt;
        } else if (uncle^.left() == child) {
            granny^.right_ = child;
            child^.parent_ = granny;
            child^.right_ = uncle;
            uncle^.parent_ = child;
            uncle^.left_ = nullptr;
            child <--> uncle;
        }

        /*
        case 3:
        rebalance and recolor the tree.

             granny(b)    ->       aunt(b)
            /        \            /      \
          aunt(r)  uncle(b)    child(r)  granny(r)
          /     \                       /      \
        child(r) sibling             sibling uncle(b)

             granny(b)    ->         uncle(b)
            /      \                /       \
        aunt(b)  uncle(r)         granny(r)  child(r)
                 /   \            /     \
             sibling child(r)  aunt(b) sibling
        */
        great = granny^.parent_;
        Tree^ new_granny = nullptr;
        if (aunt^.left() == child) {
            sibling = aunt^.right_;
            aunt^.color_ = kBlack;
            aunt^.parent_ = great;
            aunt^.right_ = granny;
            granny^.color_ = kRed;
            granny^.parent_ = aunt;
            granny^.left_ = sibling;
            if (sibling) {
                sibling^.parent_ = granny;
            }
            new_granny = aunt;
        } else {
            sibling = uncle^.left_;
            uncle^.color_ = kBlack;
            uncle^.parent_ = great;
            uncle^.left_ = granny;
            granny^.color_ = kRed;
            granny^.parent_ = uncle;
            granny^.right_ = sibling;
            if (sibling) {
                sibling^.parent_ = granny;
            }
            new_granny = uncle;
        }

        /*
        if the great grandparent is the host
            point the host at the new grandparent.
        otherwise
            replace the great grandparent's child
            with the new grandparent.
        */
        if (great^.color_ == kHost) {
            great^.parent_ = new_granny;
        } else if (great^.left_ == granny) {
            great^.left_ = new_granny;
        } else {
            great^.right_ = new_granny;
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
