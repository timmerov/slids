/*
nested features.

list by claude.
edits by timmer.
============================================================
Slids and friends — declarable constructions.

Y = supported, N = not supported, P = parses but codegen lags,
D = defered, * = see notes.
current / spec
Class means the code body of a class declaration.
Block means all blocks where runtime code can be.
includes the code bodies of: function, method, template function,
template method, for update (weird but allowed) and loop, while,
if, else, switch, plain code block, etc.
============================================================

SHAPE                                                       | Global | Class | Block | Notes
------------------------------------------------------------|--------|-------|-------|--------------------------------------------
Class               Name(fields) { defs }                   |   Y    |   Y   |   Y   |
Derived             Base : Name(fields) { defs }            |   Y    |   Y   |   Y   |
Reopen Class        Name() { defs }                         |   Y    |  N/Y  |  N/Y  |
Incomplete Class    Name() { defs }                         |   Y    |  ?/Y  |  ?/Y  | close-once
Template Class      Name<T>(fields) { defs }                |   Y    |   Y   |   P   | block: parses; codegen file-scope-centric
Function            RetType name(params) { body }           |   Y    |   N   |   Y   |
Method              RetType name(params) { body }           |   N    |   Y   |   Y*  | needs clarification
Template Function   RetType name<T>(params) { body }        |   Y    |   N   |  N/D  |
Template Method     RetType name<T>(params) { body }        |   N    |   Y*  |  N/D  | meeds clarification
External Method     RetType Class:method(...) { body }      |   Y    |  N/Y  |  N/Y  |
Imported Function   RetType name(params) = import;          |   Y    |  N/Y  |  N/Y  |
Namespace           Name { defs }                           |   Y    |  N/Y  |  N/Y  |
Reopen Namespace    Name { defs }                           |   Y    |  N/Y  |  N/Y  |
Import              import Name;                            |   Y    |   N   |   N   |
Import Block        import { decls; }                       | P* /Y  |  N/Y  |  N/Y  | * only inside a namespace
Constructor         _() { body }                            |   N    |   Y   |   N   |
Destructor          ~() { body }                            |   N    |   Y   |   N   |
Overload Operator   op<sym>(params) { body }                |   N    |   Y   |   N   |
Method Restriction  RetType name(params) = default;         |   N    |   Y   |   N   |
                    RetType name(params) = delete;          |   N    |   Y   |   N   |
Local Variable      Type name;                              |   N    |   N   |   Y   |
                    Type name = expr;                       |   N    |   N   |   Y   |
Inferred Type       var = expr                              |   Y    |   N   |   Y   |
Aliases             alias Name = TypeExpr;                  |   Y    |  N/Y  |   Y   | class-body alias not exercised
                    alias Name<T,...> = TypeExpr;           |   Y    |  N/Y  |   Y   |
                    alias Name = Namespace;                 |  N/Y   |  N/Y  |  N/Y  |
                    alias Namespace;                        |  N/Y   |  N/Y  |  N/Y  | Namespace becomes global scope.
Const               const Type name = expr;                 |   Y    |   Y   |   Y   |
Enum                enum Name (a, b, c);                    |   Y    |   Y   |  N/Y  | reopen needs review
Global Namespace    global Name (...) {...}                 |   Y    |  N/Y  |  N/Y  |
Global Variable     global Type field = expr;               |   Y    |  N/Y  |  N/Y  |
                    Type name;                              |   Y    |   N   |   N   |
Glboal Inferred     global name = expr;                     |   Y    |  N/Y  |  N/Y  |
                    name = expr;                            |   Y    |   N   |   N   |
Global Scope        global;                                 |   N    |   N   |   Y*  | only inside main()
Instantiate         Name<T>(types);                         |   *    |   N   |   N   | only used by the pre-linker
Unnamed Object      Name;                                   |   Y    |   N   |   Y   | global = eager unnamed; block = scope-lifetime
                    Name(args);                             |   Y    |   N   |   Y   |

A few things worth flagging that aren't a clean Y/N:

- Field/local/global Type name; is the same parsed shape across all three scopes, but means three different things — a global at
file scope, a field in a class body, a local in a block. Same source, different construct per context.
- Templ cls block-scope is P: the parser accepts a templated local class, but the template-instantiation codegen paths are
file-scope-centric and unverified for block-scope instances. Open follow-up in project_block_scope_walker.
- Import blk is global-only via the Name import { ... } shorthand or Namespace { import { ... } } — it never stands alone.
- Global anchor (global;) is the lifetime statement, not a declaration; only legal in main()'s code block.
- Type alias in class body — currently N (untested); the T:Element design conversation we had needed exactly this and flagged it as
separate landing work.
*/

Outer(
    int x_ = 1,
    int y_ = 2,
    ...
) {
    Inner(
        int a_ = 3,
        int b_ = 4,
        Tag tag_ = kLow
    ) {
        /* a const and a nested enum inside a nested class. */
        const int kBase = 30;
        enum Tag (kLow, kHigh);

        void print(char[] name) {
            __println("Inner: " + name + ": a=" + a_ + " b=" + b_);
        }

        /* inferred local of the nested-enum field type. */
        int tag() {
            t = tag_;
            return t;
        }

        /* switch over a nested-class enum — bare case label. */
        int classify() {
            int r;
            switch (tag_) {
            case kHigh:
                r = 9;
                break;
            default:
                r = 0;
                break;
            }
            return r;
        }
    }

    void test1() {
        Inner inn(5);
        inn.print("Outer:test inn");
    }

    InTemplate<T>(
        T m_
    ) {
        void print(char[] name) {
            __println("InTemplate: " + ##type(m_) + " " + name + " = " + m_);
        }
    }

    void test2() {
        InTemplate<uint16> it(10);
        __println("Outer:test2 it");
    }

    /* base and derived class defined inside another class's body. */
    Bird(int wings_ = 2) {
        void chirp() { __println("Outer:Bird wings=" + wings_); }
    }
    Bird : Robin(int feathers_ = 100) {
        void sing() { __println("Outer:Robin wings=" + wings_ + " feathers=" + feathers_); }
    }
}

Outer (
    ...,
    int z_ = 100
) {
    Inner2(
        int c_ = 101
    ) {
        void print(char[] name) {
            __println("Inner2: " + name + ": c=" + c_);
        }
    }
}

/* derived hoist: Guest derives from HostA but is hoisted in HostB's namespace. Access path is HostB:Guest. */
HostA(int a_ = 1) {
    void show() { __println("HostA a=" + a_); }
}
HostB(int b_ = 2) {
    HostA:Guest(int g_ = 3) {
        void show() { __println("HostB:Guest a=" + a_ + " g=" + g_); }
    }
    /* multi-colon base inside a class body: derives from Outer:Bird (a hoist of a different file-scope class). */
    Outer:Bird : Hawk(int speed_ = 99) {
        void dive() { __println("HostB:Hawk wings=" + wings_ + " speed=" + speed_); }
    }
}

/* multi-colon base at file scope: derives from Outer:Bird. */
Outer:Bird : Eagle(int beak_ = 7) {
    void scream() { __println("Eagle wings=" + wings_ + " beak=" + beak_); }
}

/* negative: a hoisted class cannot share its immediate enclosing's name. */
//-EXPECT-ERROR: shadows enclosing class
// DirectShadow(int x_ = 0) {
//     DirectShadow(int y_ = 0) { }
// }

/* negative: a hoisted class cannot share any transitive enclosing's name. */
//-EXPECT-ERROR: shadows enclosing class
// TransitiveShadow(int x_ = 0) {
//     Middle(int y_ = 0) {
//         TransitiveShadow(int z_ = 0) { }
//     }
// }

/* negative: a derived class references a base that doesn't exist. */
//-EXPECT-ERROR: Base class 'DoesNotExist'
// DoesNotExist : OrphanDerived(int x_ = 0) { }

/* local class and a derived class inside an if block. */
void test_in_if() {
    int n = 1;
    if (n == 1) {
        InIf(int v_ = 10) {
            void hi() { __println("InIf v=" + v_); }
        }
        InIf : DerIf(int w_ = 12) {
            void bye() { __println("DerIf v=" + v_ + " w=" + w_); }
        }
        InIf x(11);
        x.hi();
        DerIf y(13, 14);
        y.bye();
    }
}

/* local class and a derived class inside a for block. */
void test_in_for() {
    for (i : 0..1) {
        InFor(int v_ = 20) {
            void hi() { __println("InFor v=" + v_); }
        }
        InFor : DerFor(int w_ = 22) {
            void bye() { __println("DerFor v=" + v_ + " w=" + w_); }
        }
        InFor x(21);
        x.hi();
        DerFor y(23, 24);
        y.bye();
    }
}

/* local class and a derived class inside a while block. */
void test_in_while() {
    int n = 1;
    while (n > 0) {
        InWhile(int v_ = 30) {
            void hi() { __println("InWhile v=" + v_); }
        }
        InWhile : DerWhile(int w_ = 32) {
            void bye() { __println("DerWhile v=" + v_ + " w=" + w_); }
        }
        InWhile x(31);
        x.hi();
        DerWhile y(33, 34);
        y.bye();
        n = n - 1;
    }
}

/* local class and a derived class inside a switch case. */
void test_in_switch() {
    int n = 1;
    switch (n) {
    case 1:
        InSwitch(int v_ = 40) {
            void hi() { __println("InSwitch v=" + v_); }
        }
        InSwitch : DerSwitch(int w_ = 42) {
            void bye() { __println("DerSwitch v=" + v_ + " w=" + w_); }
        }
        InSwitch x(41);
        x.hi();
        DerSwitch y(43, 44);
        y.bye();
        break;
    default:
        break;
    }
}

/* local class with a multi-colon base inside a function block. */
void test_multi_colon_base() {
    Outer:Bird : LocalEagle(int span_ = 6) {
        void soar() { __println("LocalEagle wings=" + wings_ + " span=" + span_); }
    }
    LocalEagle le(11, 33);
    le.soar();
}

/* local class and a derived class inside a deeply nested anonymous block. */
void test_deep_nest() {
    {{{{{
        Deep(int v_ = 50) {
            void hi() { __println("Deep v=" + v_); }
        }
        Deep : DerDeep(int w_ = 52) {
            void bye() { __println("DerDeep v=" + v_ + " w=" + w_); }
        }
        Deep x(51);
        x.hi();
        DerDeep y(53, 54);
        y.bye();
    }}}}}
}

int32 main() {
    Outer out;
    out.test1();
    out.test2();

    Outer:Inner main_in(6, 7, Outer:Inner:kHigh);
    main_in.print("main_inn");
    __println("main_inn tag = " + main_in.tag());
    __println("main_inn classify = " + main_in.classify());
    __println("Outer:Inner:sizeof = " + Outer:Inner:sizeof());

    Outer:InTemplate<char> main_it(11);
    main_it.print("main_it");

    Outer:Inner2 main_in2(102);
    main_in2.print("main_in2");

    /* nested-class const and nested-enum value via multi-colon scope. */
    __println("Outer:Inner:kBase = " + Outer:Inner:kBase);
    __println("Outer:Inner:kHigh = " + Outer:Inner:kHigh);

    Outer:Bird main_bird(3);
    main_bird.chirp();
    Outer:Robin main_robin(4, 200);
    main_robin.sing();

    HostB:Guest guest(4);
    guest.show();

    Eagle eag(8, 14);
    eag.scream();
    HostB:Hawk hk(15, 88);
    hk.dive();

    test_in_if();
    test_in_for();
    test_in_while();
    test_in_switch();
    test_deep_nest();
    test_multi_colon_base();

    return 0;
}
