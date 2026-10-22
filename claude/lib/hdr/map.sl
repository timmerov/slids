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
}

Map<Key, Value>(
    intptr size_ = 0,
    MapIter root_ = nullptr,
    MapIter first_ = nullptr,
    MapIter last_ = nullptr
) {
    _() { }
    ~() {
        root_^.destroy();
    }

    intptr size() {
        return size_;
    }

    MapIter begin() {
        return first_;
    }

    MapIter end() {
        return nullptr;
    }

    MapIter next(MapIter it) {
        return <MapIter> it^.next();
    }

    void insert(Key key, Value value) {

        /* create a new red node. */
        child = new MapNode<Key, Value>;
        child^.color_ = kRed;
        child^.key_ = key;
        child^.value_ = value;
        ++size_;

        /* the root node is black. */
        if (root_ == nullptr) {
            root_ = child;
            first_ = child;
            last_ = child;
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

        /* update first and last */
        lf = first_^.left_;
        rt = last_^.right_;
        if (lf) {
            first_ = <MapIter> lf;
        }
        if (rt) {
            last_ = <MapIter> rt;
        }

        /* force the root to black. */
        root_^.color_ = kBlack;
    }
}
