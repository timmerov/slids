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

*/

int32 main() {

    return 0;
}
