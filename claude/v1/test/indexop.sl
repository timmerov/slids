/*
class overloads the index operators.
*/

Index(
    int rgb_[3]
) {
    int^ op[](intptr idx) {
        __println("indexing rgb_[" + idx + "]=" + rgb_[idx]);
        return ^rgb_[idx];
    }

    void print() {
        __println("rgb=(" + rgb_[0] + "," + rgb_[1] + "," + rgb_[2] + ")");
    }
}

int32 main() {
    Index color;
    color.rgb_[0] = 100;
    color.rgb_[1] = 200;
    color.rgb_[2] = 300;
    color.print();

    color[0] = 10;
    color[1] = 20;
    color[2] = 30;
    color.print();

    r = color[0];
    g = color[1];
    b = color[2];
    __println("r=" + r);
    __println("g=" + g);
    __println("b=" + b);

    return 0;
}
