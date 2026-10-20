import string;

Animal(int legs_ = 4) {
    virtual void speak() = delete;          // pure virtual: Animal is abstract
    int legs() {
        return legs_;
    }
}

Animal : Dog(int tricks_ = 0) {
    virtual void speak() {
        println(String + "woof " + legs_ + " " + tricks_);
    }
}

Animal : Bird() {
    virtual void speak() {
        println(String + "tweet " + Animal:legs());     // qualified base call
    }
}

int32 main() {
    Dog dog(4, 2);                          // base fields first
    Bird bird(2);
    Animal^ pets[2] = (^dog, ^bird);        // derived to base: implicit
    for (p : pets) {
        p^.speak();
    }
    Dog^ back = <Dog^> pets[0];             // base to derived: explicit
    println(String + back^.tricks_);
    return 0;
}
