/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
key/value map implementation.
built upon red/black tree.
*/

import map;
import tree;

TreeNode : MapNode<Key, Value>() {

    alias MapIter = MapNode<Key, Value>^;

    _() {}
    ~() {}

    /* recursively delete the tree. */
    void destroy() {
        if (^self == nullptr) {
            return;
        }
        lf = <MapIter> left_;
        rt = <MapIter> right_;
        lf^.destroy();
        rt^.destroy();
        delete ^self;
    }

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
}

Map<Key, Value>(
    MapNode<Key, Value>^ root_
) {
    _() { }
    ~() {
        root_^.destroy();
    }

    void insert(Key key, Value value) {

        /* create a new red node. */
        child = new MapNode<Key, Value>;
        child^.color_ = kRed;
        child^.key_ = key;
        child^.value_ = value;

        /* the root node is black. */
        if (root_ == nullptr) {
            root_ = child;
            child^.color_ = kBlack;
            return;
        }

        /* insert the new node into the tree. */
        MapIter parent = root_;
        while() {
            if (key < parent^.key_) {
                if (parent^.left_ == nullptr) {
                    parent^.left_ = child;
                    break;
                }
                parent = <MapIter> parent^.left_;
            } else {
                if (parent^.right_ == nullptr) {
                    parent^.right_ = child;
                    break;
                }
                parent = <MapIter> parent^.right_;
            }
        }
        child^.parent_ = parent;

        /* recolor/rebalance the tree. */
        child^.rebalance();

        /* update root. */
        while() {
            ancestor = <MapIter> root_^.parent_;
            if (ancestor == nullptr) {
                break;
            }
            root_ = ancestor;
        }

        /* force the root to black. */
        root_^.color_ = kBlack;
    }

    void print() {
        if (root_) {
            root_^.print(0);
        }
    }
}
