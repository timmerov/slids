import string;

Counter(int value_ = 0, int step_ = 1) {
    _() {                                   // ctor: runs after the fields are set
        println(String + "hello " + value_);
    }
    ~() {                                   // dtor: runs at end of scope
        println(String + "bye " + value_);
    }

    void bump() {
        value_ += step_;
    }
    int const get() {                       // const: does not modify self
        return value_;
    }
}

void Counter:reset() {                      // external method
    value_ = 0;
}

Counter() {                                 // re-open to add more
    const int kLimit = 100;
    bool full() {
        return self.value_ >= kLimit;
    }
}

int32 main() {
    Counter a;                              // value_ = 0, step_ = 1
    Counter b(10, 5);                       // by tuple
    Counter c = (20, 2);                    // by assignment
    b.bump();
    c.reset();
    x = Counter(7).get();                   // nameless temporary, dies at the ;
    println(String + a.get() + " " + b.get() + " " + c.get() + " " + x + " " + b.full() + " " + Counter:kLimit);
    return 0;
}
