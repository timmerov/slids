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

/* tree.slh ================================================= */
TreeNode(...) {
    _();
    ~();

    /* getters. */
    Color color();
    TreeNode^ parent();
    TreeNode^ left();
    TreeNode^ right();

    /* setters. */
    void set_color(Color color);
    void set_parent(mutable TreeNode^ parent);
    void set_left(mutable TreeNode^ left);
    void set_right(mutable TreeNode^ right);

    /* recolor/rebalance the tree. */
    void rebalance();
}

/* map.slh ================================================= */
TreeNode : MapNode<Key, Value>(
    Key key_,
    Value value_
) {
    _();
    ~();
}

Map<Key, Value>(...) {
    alias MapIter = MapNode<Key, Value>^;

    _(); ~();

    void insert(Key key, Value value);
    void print();
}

/* tree.sl ================================================= */
/*
red black tree.
1. the root is black.
2. red nodes have no red children.
3. every path to leaf nodes must have the same number of black nodes.
4. inserted nodes (after the first) are red.
if this breaks rules 2,3 then recolor/rebalance.
*/
TreeNode(
    Color color_ = kRed,
    TreeNode^ parent_ = nullptr,
    TreeNode^ left_ = nullptr,
    TreeNode^ right_ = nullptr
) {
    enum Color (kRed, kBlack, kNull);

    _() {}
    ~() {}

    /* color getter. */
    Color color() {
        if (^self == nullptr) {
            return kNull;
        }
        return color_;
    }

    /* parent getter */
    TreeNode^ parent() {
        if (^self == nullptr) {
            return nullptr;
        }
        return parent_;
    }

    /* left getter */
    TreeNode^ left() {
        if (^self == nullptr) {
            return nullptr;
        }
        return left_;
    }

    /* right getter */
    TreeNode^ right() {
        if (^self == nullptr) {
            return nullptr;
        }
        return right_;
    }

    /* color setter. */
    void set_color(Color color) {
        if (^self) {
            color_ = color;
        }
    }

    /* parent setter */
    void set_parent(mutable TreeNode^ parent) {
        if (^self) {
            parent_ = parent;
        }
    }

    /* left setter */
    void set_left(mutable TreeNode^ left) {
        if (^self) {
            left_ = left;
        }
    }

    /* right setter */
    void set_right(mutable TreeNode^ right) {
        if (^self) {
            right_ = right;
        }
    }

    /* recolor/rebalance the tree. */
    void rebalance() {

        child = ^self;
        TreeNode^ parent = parent_;

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
                aunt^.set_color(Color:kBlack);
                uncle^.set_color(Color:kBlack);
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
        uncle^.set_parent(great);
        uncle^.set_left(^self);
        sibling^.set_parent(^self);
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
        aunt^.set_parent(great);
        aunt^.set_right(^self);
        sibling^.set_parent(^self);
    }

    /*
    change the parent's left or right child
    from former to new.
    */
    void change_child(
        TreeNode^ former_child,
        mutable TreeNode^ new_child
    ) {
        if (^self == nullptr) {
            return;
        }
        if (left_ == former_child) {
            left_ = new_child;
        } else {
            right_ = new_child;
        }
    }
}

/* map.sl ================================================= */
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
        parent = child;
        while() {
            ancestor = <MapIter> parent^.parent_;
            if (ancestor == nullptr) {
                break;
            }
            parent = ancestor;
        }
        root_ = parent;

        /* force the root to black. */
        root_^.color_ = kBlack;
    }

    void print() {
        if (root_) {
            root_^.print(0);
        }
    }
}

void test_map() {
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
