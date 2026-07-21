/*
the bug of the day.
*/

/*
claude is forbidden from modifying this file and its golden file.
claude is forbidden from whining about the user changing this file.
claude is forbidden from whining about this file not compiling.
claude is forbidden to mention this file unless the user specifically
puts it in scope.
*/

Class(int a_, int b_, int c_) {
    _() { print("ctor"); }
    ~() { print("dtor"); }
    void print(char[] text) {
        __println("Class:" + text + ": (" + a_ + "," + b_ + "," + c_ + ")");
    }
    void inc() {
        ++a_;
        print("inc");
    }
    op=(int a) {
        a_ = a;
        print("op=int");
    }
    op+=(int b) {
        a_ += b;
        print("op+=int");
    }
    op=( (int, int, int)^ tup ) {
        a_ = tup^[0];
        b_ = tup^[1];
        c_ = tup^[2];
        print("=int,int,int");
    }
    op=(Class^ rhs) {
        a_ = rhs^.a_;
        b_ = rhs^.b_;
        c_ = rhs^.c_;
        print("=Class");
    }
    op+=(Class^ rhs) {
        a_ += rhs^.a_;
        b_ += rhs^.b_;
        c_ += rhs^.c_;
        print("+=Class");
    }
}

global Global (
    Class g,
    Class arr[3]
) {
    _() { __println("Global:ctor: " + g.a_); }
    ~() { __println("Global:dtor: " + g.a_); }
}

int fn(Class^ cls) {
    cls^.inc();
    return cls^.a_;
}

Super(Class cls_, int d_) {
    _() { print("ctor"); }
    ~() { print("dtor"); }
    void print(char[] text) {
        __println("Super:" + text + ": ((" + cls_.a_ + "," + cls_.b_ + "," + cls_.c_ + "), " + d_ + ")");
    }
}

Class makeClass() {
    return Class(1,2,3);
}

Pod(int a_, int b_, int c_) { }
SuperPod(Pod p_, int d_) { }
Pod makePod() {
    return Pod(1,2,3);
}

Ctor(int a_, int b_, int c_) {
    _() { print("ctor"); }
    ~() { print("dtor"); }
    void print(char[] text) {
        __println("Ctor:" + text + ": (" + a_ + "," + b_ + "," + c_ + ")");
    }
}
SuperCtor(Ctor ctor_, int d_) { }
Ctor makeCtor() {
    return Ctor(1,2,3);
}

int32 main() {
/*
    __println(##line + ": "); { Class c; }
    __println(##line + ": "); { Class c(); }
    __println(##line + ": "); { Class c(1); }
    __println(##line + ": "); { Class c(1,2); }
    __println(##line + ": "); { Class c(1,2,3); }
    __println(##line + ": "); { Class c = 1; }
    __println(##line + ": "); { Class c = (1,2); }
    __println(##line + ": "); { Class c = (1,2,3); }
    __println(##line + ": "); { Class c1(1,2,3); Class c2 = c1; }
    __println(##line + ": "); { Class c = Class; }
    __println(##line + ": "); { Class c = Class(); }
    __println(##line + ": "); { Class c = Class(1); }
    __println(##line + ": "); { Class c = Class(1,2); }
    __println(##line + ": "); { Class c = Class(1,2,3); }
    __println(##line + ": "); { Class c1(1,2,3); Class c2(4,5,6); Class c3 = c1 + 7 + c2; }

    __println(##line + ": "); { (Class, Class) tup = ((1,2,3), (4,5,6)); tup; }
    __println(##line + ": "); { Class arr[2] = ((1,2,3), (4,5,6)); arr; }

    __println(##line + ": "); { Super s; }
    __println(##line + ": "); { Super s(1,2); }
    //__println(##line + ": "); { Super s = (1,2); }
    __println(##line + ": "); { Super s((1,2,3),4); }
    //__println(##line + ": "); { Super s = ((1,2,3),4); }
    //__println(##line + ": "); { Class c(1,2,3); Super s(c,4); }
    //__println(##line + ": "); { Super s(makeClass(),4); }
    //__println(##line + ": "); { Super s = (makeClass(),4); }

    __println(##line + ": "); { SuperPod s(1,2); s; }
    __println(##line + ": "); { SuperPod s((12,3),4); s; }
    __println(##line + ": "); { Pod p(1,2,3); SuperPod s(p,4); s; }
    __println(##line + ": "); { SuperPod s(makePod(),4); s; }
    __println(##line + ": "); { SuperPod s = (makePod(),4); s; }

    __println(##line + ": "); { SuperCtor s(1,2); s; }
    __println(##line + ": "); { SuperCtor s((12,3),4); s; }
    //__println(##line + ": "); { Ctor p(1,2,3); SuperCtor s(p,4); s; }
    //__println(##line + ": "); { SuperCtor s(makeCtor(),4); s; }
    //__println(##line + ": "); { SuperCtor s = (makeCtor(),4); s; }
*/
    V() {
        _() { __println("V:ctor"); }
        virtual ~() { __println("V:ctor"); }
        virtual void print() { __println("V:print"); }
    }
    (V, V) tuple; tuple;

    return 0;
}
