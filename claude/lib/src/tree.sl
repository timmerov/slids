/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
red black tree implementation.

1. the root is black.
2. red nodes have no red children.
3. every path to leaf nodes must have the same number of black nodes.
4. inserted nodes (after the first) are red.
if this breaks rules 2,3 then recolor/rebalance.
*/

import tree;

TreeNode() {
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

    /* return the tree node after this one. */
    TreeNode^ next() {
        if (^self == nullptr) {
            return nullptr;
        }
        if (right_ == nullptr) {
            /*
            there is nothing after this node.
            go up the tree until hit root.
            or we find the parent of a left child.
            */
            it = ^self;
            while() {
                TreeNode^ parent = it^.parent_;
                if (parent == nullptr) {
                    return nullptr;
                }
                if (parent^.right_ != it) {
                    return parent;
                }
                it = parent;
            }
        }
        /*
        drop down the left side of the tree
        until we reach a node with no left.
        */
        it = right_;
        while (it^.left_) {
            it = it^.left_;
        }
        return it;
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
