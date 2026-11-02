/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
key/value map implementation.
built upon red/black tree.
*/

import map;
import tree;

/* slids namespace */
slids {

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
        clear();
    }

    /*
    assignment from Vector.
    copy operator.
    rhs may be self.
    */
    op=(Map<Key, Value>^ rhs) {
        /* don't copy self. */
        if (rhs == ^self) {
            return;
        }

        /* make space. */
        clear();

        /* copy the tree the slow way. */
        for (it : rhs^) {
            insert(it^.key_, it^.value_);
        }
    }

    /*
    move operator.
    rhs may be self.
    */
    op<--(mutable Map<Key, Value>^ rhs) {
        /* don't move self. */
        if (rhs == ^self) {
            return;
        }

        /* delete our storage. */
        clear();

        /* copy from rhs. */
        size_ = rhs^.size_;
        root_ <-- rhs^.root_;
        first_ <-- rhs^.first_;
        last_ <-- rhs^.last_;

        /* clear rhs. */
        rhs^.size_ = 0;
    }

    /* size of map. */
    intptr size() {
        return size_;
    }

    /* first element of map. */
    MapIter begin() {
        return first_;
    }

    /* end of the map. */
    MapIter end() {
        return nullptr;
    }

    /* step to the next iterator. */
    MapIter next(MapIter it) {
        return <MapIter> it^.next();
    }

    /* remove all elements from the map. */
    void clear() {
        root_^.destroy();
        size_ = 0;
        root_ = nullptr;
        first_ = nullptr;
        last_ = nullptr;
    }

    /* find the element with the key. */
    MapIter find(Key key) {
        MapIter it = root_;
        while () {
            if (it == nullptr) {
                return end();
            }
            if (key < it^.key_) {
                it = <MapIter> it^.left_;
            } else if (it^.key_ < key) {
                it = <MapIter> it^.right_;
            } else {
                return it;
            }
        }
    }

    /* insert the key/value pair into the map. */
    void insert(mutable Key key, mutable Value value) {

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

} /* slids */
