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
3. siblings are the same color.
4. inserted nodes (after the first) are red.
if this breaks rules 2,3 then recolor/rebalance.
*/
Tree<Key, Value>(
    Key key_,
    Value value_,
    ...
) {
    _(); ~();

    /* insert the key value pair into the tree. */
    void insert(Key key, Value value);

    /* print the tree. */
    void print();
}

Tree<Key, Value>(
    Color color_ = kHost,
    Tree^ parent_ = nullptr,
    Tree^ left_ = nullptr,
    Tree^ right_ = nullptr
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
    void insert(Key key, Value value) {

        /* create a new red node. */
        child = new Tree;
        child^.color_ = kRed;
        child^.key_ = key;
        child^.value_ = value;

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
            if (key < parent^.key_) {
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

        /* recolor and/or rebalance the tree. */
        while (parent^.color() == kRed) {

            granny = parent^.parent_;
            aunt = granny^.left_;
            uncle = granny^.right_;
            if (aunt^.color() == kRed && uncle^.color() == kRed) {
                /*
                case 1: recolor
                parent and parent's sibling are both red.
                change them to black.
                change grandparent to red.
                iterate up the tree with new child is old granny.
                */
                granny^.color_ = kRed;
                if (aunt) {
                    aunt^.color_ = kBlack;
                }
                if (uncle) {
                    uncle^.color_ = kBlack;
                }
                child = granny;
                parent = child^.parent();
            } else {
                /* case 2: move inner child out. */
                if (child == aunt^.right()) {
                    child = parent;
                    child^.rotate_left();
                    parent = child^.parent();
                } else if (child == uncle^.left()) {
                    child = parent;
                    child^.rotate_right();
                    parent = child^.parent();
                }
                /* case 3: rotate granny. */
                granny^.color_ = kRed;
                parent^.color_ = kBlack;
                if (child == parent^.left_) {
                    granny^.rotate_right();
                } else {
                    granny^.rotate_left();
                }
            }
        }

        /* set the root node to black. */
        parent_^.color_ = kBlack;
    }

    /*
    rotate right:

         great            great
           |                |
         self      ->     aunt
        /    \           /    \
       aunt  uncle    child  self
      /    \                /    \
    child  sibling      sibling  uncle
    */
    void rotate_right() {
        great = parent_;
        aunt = left_;
        sibling = aunt^.right();

        great^.change_child(^self, aunt);
        parent_ = aunt;
        left_ = sibling;
        if (aunt) {
            aunt^.parent_ = great;
            aunt^.right_ = ^self;
        }
        if (sibling) {
            sibling^.parent_ = ^self;
        }
    }

    /*
    rotate left:

         great              great
           |                  |
         self        ->     uncle
        /    \              /    \
      aunt  uncle         self  child
            /    \       /    \
        sibling child  aunt  sibling
    */
    void rotate_left() {
        great = parent_;
        uncle = right_;
        sibling = uncle^.left();

        great^.change_child(^self, uncle);
        parent_ = uncle;
        right_ = sibling;
        if (uncle) {
            uncle^.parent_ = great;
            uncle^.left_ = ^self;
        }
        if (sibling) {
            sibling^.parent_ = ^self;
        }
    }

    /*
    change the parent's child.
    if this is the host then the child is the new root.
    otherwise change the left/right child.
    */
    void change_child(
        Tree^ former_child,
        mutable Tree^ new_child
    ) {
        if (color_ == kHost) {
            parent_ = new_child;
        } else if (left_ == former_child) {
            left_ = new_child;
        } else {
            right_ = new_child;
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

    /* print the tree. */
    void print() {
        parent_^.print(0);
    }

    /* recursively pretty print the tree. */
    void print(int spaces) {
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
    Tree<String, String> tree;
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
    tree.print();
}
