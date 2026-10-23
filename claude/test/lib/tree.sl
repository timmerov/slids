/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
test red black tree implementation.
*/

/*
claude says:
the library supplies TreeNode + rebalance(); the test supplies the rest of a
tree: Node adds an int key, Tree owns the root, inserts, destroys. rebalance()
can rotate the root down, so Tree::insert walks up to the new root and forces
it black (as Map does). each block prints the tree in order (one "| " per
depth) and check() verifies: black root, no red node with a red child, equal
black height on every path, every child's parent_ points back.
*/

import string;
import tree;
alias slids;

TreeNode : Node(
    int key_
) {
    alias NodeIter = Node^;

    _() {}
    ~() {}

    /* recursively delete the tree. */
    void destroy() {
        if (^self == nullptr) {
            return;
        }
        lf = <NodeIter> left_;
        rt = <NodeIter> right_;
        lf^.destroy();
        rt^.destroy();
        delete ^self;
    }

    /* recursively print the tree in order. */
    void print(int depth) {
        if (^self == nullptr) {
            return;
        }
        lf = <NodeIter> left_;
        lf^.print(depth + 1);

        String line;
        int i = 0;
        while (i < depth) {
            line = line + "| ";
            ++i;
        }
        String color = "red";
        if (color_ == kBlack) {
            color = "black";
        }
        println(line + key_ + " (" + color + ")");

        rt = <NodeIter> right_;
        rt^.print(depth + 1);
    }

    /* black height of the subtree, or -1 when a rule is broken. */
    int check() {
        if (^self == nullptr) {
            return 1;
        }
        lf = <NodeIter> left_;
        rt = <NodeIter> right_;
        int bad = 0;
        if (lf != nullptr) {
            if (lf^.parent_ != ^self) {
                println(String + "FAIL: " + lf^.key_ + " parent is not " + key_);
                bad = 1;
            }
            if (color_ == kRed && lf^.color_ == kRed) {
                println(String + "FAIL: red " + key_ + " has red child " + lf^.key_);
                bad = 1;
            }
        }
        if (rt != nullptr) {
            if (rt^.parent_ != ^self) {
                println(String + "FAIL: " + rt^.key_ + " parent is not " + key_);
                bad = 1;
            }
            if (color_ == kRed && rt^.color_ == kRed) {
                println(String + "FAIL: red " + key_ + " has red child " + rt^.key_);
                bad = 1;
            }
        }
        lh = lf^.check();
        rh = rt^.check();
        if (lh < 0 || rh < 0) {
            return -1;
        }
        if (lh != rh) {
            println(String + "FAIL: black heights differ under " + key_);
            return -1;
        }
        if (bad) {
            return -1;
        }
        if (color_ == kBlack) {
            return lh + 1;
        }
        return lh;
    }
}

Tree(
    Node^ root_ = nullptr
) {
    alias NodeIter = Node^;

    _() {}
    ~() {
        root_^.destroy();
    }

    void insert(int key) {

        /* create a new red node. */
        child = new Node;
        child^.key_ = key;

        /* the root node is black. */
        if (root_ == nullptr) {
            root_ = child;
            child^.color_ = kBlack;
            return;
        }

        /* insert the new node into the tree. */
        NodeIter parent = root_;
        while() {
            if (key < parent^.key_) {
                if (parent^.left_ == nullptr) {
                    parent^.left_ = child;
                    break;
                }
                parent = <NodeIter> parent^.left_;
            } else {
                if (parent^.right_ == nullptr) {
                    parent^.right_ = child;
                    break;
                }
                parent = <NodeIter> parent^.right_;
            }
        }
        child^.parent_ = parent;

        /* recolor/rebalance the tree. */
        child^.rebalance();

        /* the root may have rotated down: walk up to the new root. */
        parent = child;
        while() {
            ancestor = <NodeIter> parent^.parent_;
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
        root_^.print(0);
    }

    void check() {
        if (root_ != nullptr && root_^.parent_ != nullptr) {
            println(String + "FAIL: root has a parent");
        }
        if (root_ != nullptr && root_^.color_ != kBlack) {
            println(String + "FAIL: root is not black");
        }
        h = root_^.check();
        if (h < 0) {
            println(String + "check: FAIL");
        } else {
            println(String + "check: ok, black height " + h);
        }
    }
}

int32 main() {
    println(String + "---no-op: parent is black (2 1 3)---");
    {
        Tree t;
        t.insert(2);
        t.insert(1);
        t.insert(3);
        t.print();
        t.check();
    }

    println(String + "---case 1 recolor (2 1 3 4)---");
    {
        Tree t;
        t.insert(2);
        t.insert(1);
        t.insert(3);
        t.insert(4);
        t.print();
        t.check();
    }

    println(String + "---case 3 left-left, rotate at the root (3 2 1)---");
    {
        Tree t;
        t.insert(3);
        t.insert(2);
        t.insert(1);
        t.print();
        t.check();
    }

    println(String + "---case 3 right-right, rotate at the root (1 2 3)---");
    {
        Tree t;
        t.insert(1);
        t.insert(2);
        t.insert(3);
        t.print();
        t.check();
    }

    println(String + "---case 2+3 left-right (3 1 2)---");
    {
        Tree t;
        t.insert(3);
        t.insert(1);
        t.insert(2);
        t.print();
        t.check();
    }

    println(String + "---case 2+3 right-left (1 3 2)---");
    {
        Tree t;
        t.insert(1);
        t.insert(3);
        t.insert(2);
        t.print();
        t.check();
    }

    println(String + "---rotate below the root (1 2 3 4 5)---");
    {
        Tree t;
        t.insert(1);
        t.insert(2);
        t.insert(3);
        t.insert(4);
        t.insert(5);
        t.print();
        t.check();
    }

    println(String + "---recolor climbs into a rotation with a sibling subtree (1..8)---");
    {
        Tree t;
        int i = 1;
        while (i <= 7) {
            t.insert(i);
            ++i;
        }
        println(String + "before 8:");
        t.print();
        t.check();
        t.insert(8);
        println(String + "after 8:");
        t.print();
        t.check();
    }

    return 0;
}
