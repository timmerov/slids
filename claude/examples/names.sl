import string;

/* block comments /* nest */ like this. */

enum Color ( kRed, kGreen, kBlue );
enum int64 Big ( kHuge = 5_000_000_000 );

alias Pixel = (Color, int);

global int count_ = 0;                      // lives for the whole program
global stats(int hits_ = 0) { }             // named group: stats:hits_

Shapes {                                    // namespace
    int area(int w, int h) {
        return w * h;
    }
}

Outer(int o_ = 1) {
    Inner(int i_ = 2) { }                   // hoisted: Outer:Inner
}

int32 main() {
    Color c = kGreen;                       // the target type supplies the enum
    Pixel px = (Color:kBlue, 7);
    count_ += 1;
    stats:hits_ = 3;
    Outer:Inner in;

    switch (c) {
        kRed: { }
        kGreen: {
            println(String + "green");
        }
        default: { }
    }

    println(String + Shapes:area(2, 3) + " " + px[1] + " " + Big:kHuge + " " + ::count_ + " " + stats:hits_ + " " + in.i_);
    println(String + sizeof(int64) + " " + sizeof(Pixel) + " " + sizeof("abc"));
    println(String + ##type(px) + " " + ##name(c) + " " + ##func + " " + ##file);
    return 0;
}
