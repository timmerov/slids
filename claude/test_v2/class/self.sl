/*
test usage of self.

self is how a method refers to the object.

    Self(int x_) {
        bool equal(Self^ other) {
            if (^self == other) {
                return true;
            }
            if (self.x_ == other^.x_) {
                return true;
            }
            return false;
        }
    }

by default, self is mutable.
use the const keyword before the method name to make self not-mutable.

    Self(int x_) {
        void const method() {
            __println("self is const.");
        }
    }

note:
const-ness is currently parsed but not enforced.
*/

/*
claude says:

tbd
*/

/*
Self(int x_) {
    Self^ this() {
        return ^self;
    }

    int shadow() {
        int x_ = 3;
        x_ += self.x_;
        self.x_ = x_ + 3;
        return x_;
    }
}
*/

int32 main() {
/*
    Self s(47);
    ref = s.this;
    __println("ref = " + ref^.x_);

    int shadow = s.shadow();
    __println("shadow = " + shadow);
    __println("s = " + s.x_);
*/
    return 0;
}
