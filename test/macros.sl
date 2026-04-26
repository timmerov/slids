/*
test the stringification operator across every emit context:
- main, free function, free function template,
- class method, class template (and template method),
- hoisted nested class method, hoisted nested class template method.
*/

void freefn(int x) {
    __println("freefn  ##func=" + ##func + " ##name=" + ##name(x) + " ##type=" + ##type(x));
    tp = #x;
    __println("freefn  #x: " + tp[0] + " " + tp[1] + " = " + tp[2]);
}

void freetpl<T>(T v) {
    __println("freetpl ##func=" + ##func + " ##name=" + ##name(v) + " ##type=" + ##type(v));
    tp = #v;
    __println("freetpl #v: " + tp[0] + " " + tp[1] + " = " + tp[2]);
}

Klass(int k_ = 0) {
    void method(int y) {
        __println("Klass.method ##func=" + ##func + " ##name=" + ##name(y) + " ##type=" + ##type(y));
        tp = #y;
        __println("Klass.method #y: " + tp[0] + " " + tp[1] + " = " + tp[2]);
    }
}

KlassTpl<T>(T t_) {
    void tmethod(T q) {
        __println("KlassTpl.tmethod ##func=" + ##func + " ##name=" + ##name(q) + " ##type=" + ##type(q));
        tp = #q;
        __println("KlassTpl.tmethod #q: " + tp[0] + " " + tp[1] + " = " + tp[2]);
    }
}

OuterC(int o_ = 0) {
    InnerC(int i_ = 0) {
        void show(int z) {
            __println("OuterC:InnerC.show ##func=" + ##func + " ##name=" + ##name(z) + " ##type=" + ##type(z));
            tp = #z;
            __println("OuterC:InnerC.show #z: " + tp[0] + " " + tp[1] + " = " + tp[2]);
        }
    }
    InnerCT<T>(T it_) {
        void tshow(T q) {
            __println("OuterC:InnerCT.tshow ##func=" + ##func + " ##name=" + ##name(q) + " ##type=" + ##type(q));
            tp = #q;
            __println("OuterC:InnerCT.tshow #q: " + tp[0] + " " + tp[1] + " = " + tp[2]);
        }
    }
}

int32 main() {
    int some_int = 42;
    float64 some_float = 3.14;
    bool some_bool = true;

    // ##name(x) — variable name as string
    char[] n1 = ##name(some_int);      // "some_int"
    char[] n2 = ##name(some_float);    // "some_float"
    char[] n3 = ##name(some_bool);     // "some_bool"
    __println("##name(some_int  )=" + n1);
    __println("##name(some_float)=" + n2);
    __println("##name(some_bool )=" + n3);

    // ##type(x) — variable type as string
    char[] t1 = ##type(some_int);      // "int"
    char[] t2 = ##type(some_float);    // "float64"
    char[] t3 = ##type(some_bool);     // "bool"
    __println("##type(some_int  )=" + t1);
    __println("##type(some_float)=" + t2);
    __println("##type(some_bool )=" + t3);

    // ##line — current source line as string
    char[] ln = ##line;
    __println("##line=" + ln);

    // ##file — source filename as string
    char[] f = ##file;
    __println("##file=" + f);

    // ##func — enclosing function name as string
    char[] fn = ##func;
    __println("##func=" + fn);

    // ##date / ##time — compilation date and time as strings (vary per build)
    char[] d = ##date;
    char[] tm = ##time;
    __println("##date=" + d);
    __println("##time=" + tm);

    // #x — desugar: (##type(x), ##name(x), x)
    tpi = #some_int;
    tpf = #some_float;
    tpb = #some_bool;
    __println("#some_int  : " + tpi[0] + " " + tpi[1] + " = " + tpi[2]);
    __println("#some_float: " + tpf[0] + " " + tpf[1] + " = " + tpf[2]);
    __println("#some_bool : " + tpb[0] + " " + tpb[1] + " = " + tpb[2]);

    // free function
    freefn(7);

    // free function template — note ##func currently leaks the mangled name
    freetpl<int>(3);
    freetpl<float64>(2.5);

    // class method
    Klass k(1);
    k.method(99);

    // class template method
    KlassTpl<uint16> kt(5);
    kt.tmethod(8);

    // hoisted nested class method
    OuterC:InnerC ii(1);
    ii.show(2);

    // hoisted nested class template method
    OuterC:InnerCT<uint8> it(3);
    it.tshow(4);

    return 0;
}
