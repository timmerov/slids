/*
nested features.

list by claude.
edits by timmer.
============================================================
Slids and friends — declarable constructions.

Y = supported, N = not supported, P = parses but codegen lags,
D = defered, * = see notes.
current / spec
============================================================

SHAPE                                                       | Global | Class | Block | Notes
------------------------------------------------------------|--------|-------|-------|--------------------------------------------
Class               Name(fields) { defs }                   |   Y    |   Y   |   Y   | class-body nested hoists to Outer.Inner
Derived             Base : Name(fields) { defs }            |   Y    |   Y   |   Y   |
Reopen              Name() { defs }                         |   Y    |  N/Y  |  N/Y  | close-once per TU
Template Class      Name<T>(fields) { defs }                |   Y    |   Y   |   P   | block: parses; codegen file-scope-centric
Function            RetType name(params) { body }           |   Y    |   Y   |   Y   | class = method; block = nested fn
Template Function   RetType name<T>(params) { body }        |   Y    |   Y   |  N/D  | nested templated fn: open follow-up
External Method     RetType Class:method(...) { body }      |   Y    |  N/Y  |  N/Y  | methods otherwise defined inline
Imported Function   RetType name(params) = import;          |   Y    |  N/Y  |  N/Y  | C ABI declaration
Namespace           Name { defs }                           |   Y    |  N/Y  |  N/Y  | block-scope: open feature
Import              import Name;                            |   Y    |   N   |   N   |
Import Block        import { decls; }                       |  P*/Y  |  N/Y  |  N/Y  | * only inside a namespace
Constructor         _() { body }                            |   N    |   Y   |   N   |
Destructor          ~() { body }                            |   N    |   Y   |   N   |
Overload Operator   op<sym>(params) { body }                |   N    |   Y   |   N   |
Method Restriction  RetType name(params) = default;         |   N    |   Y   |   N   |
                    RetType name(params) = delete;          |   N    |   Y   |   N   |
Local Variable      Type name;                              |   N    |   N   |   Y   |
                    Type name = expr;                       |   N    |   N   |   Y   |
Inferred Type       var = expr                              |   Y    |   Y   |   Y   |
Aliases             alias Name = TypeExpr;                  |   Y    |  N/Y  |   Y   | class-body alias not exercised
                    alias Name<T,...> = TypeExpr;           |   Y    |  N/Y  |   Y   |
                    alias Name = Namespace                  |  N/Y   |  N/Y  |  N/Y  |
Const               const Type name = expr;                 |   Y    |   Y   |   Y   |
Enum                enum Name (a, b, c);                    |   Y    |   Y   |  N/Y  | reopen needs review
Global Namespace    global Name (...) {...}                 |   Y    |  N/Y  |  N/Y  | cross-block visibility = the point
Global Variable     global Type field = expr;               |   Y    |  N/Y  |  N/Y  | by-design
                    Type name;                              |   Y    |  N/Y  |  N/Y  | meaning shifts by scope
Global Scope        global;                                 |   N    |   N   |   Y*  | only inside main()
Instantiate         Name<T>;                                |   *    |   N   |   N   | only used by the pre-linker
Unnamed             Name;                                   |   Y    |   N   |   Y   | global = eager unnamed; block = scope-lifetime
Unnamed             Name(args);                             |   Y    |   N   |   Y   | global = eager unnamed; block = scope-lifetime

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

    return 0;
}
