/*
test hoisted classes.

hoisted classes are declared inside the body of another class.
they have access to declarations inside the host class body.
but are not connected to a host object so do not have access to
the host class fields.

    Outer(int x_) {
        Inner(int y_) {
        }
    }

conceptually, they are part of the host class as namespace.
like constants, enums, aliases, etc.
*/

/*
claude says:

a hoisted class is a namespace-MEMBER of its host, like the host's aliases /
consts / enums — addressable only as `Outer:Inner` (and `Outer:Inner:Innerger`
for its own members), never bare from outside. It is NOT bound to a host object,
so it sees the host's namespace declarations (here `Outerger`, bare from inside
or qualified `Outer:Outerger`) but NOT the host's fields. Conversely the host
reaches into it: `Inner:Innerger` (bare `Inner`, from within Outer).

mechanism: registration is name-then-resolve, applied to the whole namespace
tree. Pass 1 recursively registers every addressable name — Outer, its members
Outerger/Inner, Inner's member Innerger — recursing through namespace-like
features (namespace/class/enum) but stopping at opaque function/ctor/dtor bodies.
Pass 2 resolves member types and ctor/dtor bodies with the enclosing frame(s)
OPEN, so every cross-reference resolves via the scope frame stack (resolveName) —
no findInLiveScopes leniency. A bare member type out of scope thus fails ("needs
a namespace qualifier"). Hoisted classes are kept distinct via def_id like local
classes; identity rides the handle, never a spelling.
*/

Outer(int x_ = -1) {
    alias Outerger = int;

    _() {
        Inner:Innerger x = x_;
        __println("Outer:ctor: " + x);
    }
    ~() {
        Outer:Inner:Innerger x = x_;
        __println("Outer:dtor: " + x);
    }

    Inner(int y_ = -2) {
        alias Innerger = int;

        _() {
            Outerger y = y_;
            __println("Inner:ctor: " + y);
        }
        ~() {
            Outer:Outerger y = y_;
            __println("Inner:dtor: " + y);
        }
    }
}

/* a host FIELD typed as a hoisted class — written BARE, resolved in the host's own
   scope (same scope its ctor/dtor see). The field's hooks run transitively. */
Box(Item i_) {
    Item(int v_) {
        _() { __println("Item:ctor: " + v_); }
        ~() { __println("Item:dtor: " + v_); }
    }
}

/* a hoisted class nested one level deeper, with a member enum at the bottom. */
Tree(int x_) {
    Branch(int y_) {
        enum int Side ( left, right );
        Leaf(int z_) {
            _() { __println("Leaf:ctor: " + z_); }
            ~() { __println("Leaf:dtor: " + z_); }
        }
    }
}

/* a hoisted class shadows a same-named FILE-SCOPE class (distinct types). */
Node(int z_) {
    _() { __println("FILE:Node: " + z_); }
    ~() { }
}
Tower(int x_) {
    Node(int y_) {
        _() { __println("HOISTED:Node: " + y_); }
        ~() { }
    }
}

/* a host VALUE member (const) reached from a hoisted class, bare and qualified. */
Cfg(int x_) {
    const int kMax = 99;
    Reader(int y_) {
        _() {
            int a = kMax;
            int b = Cfg:kMax;
            __println("Reader: " + a + " " + b);
        }
        ~() { }
    }
}

/* a hoisted class used as a function RETURN type and a pointer PARAMETER
   (a by-value class parameter is rejected language-wide; pass by reference). */
Maker(int x_) {
    Widget(int w_) {
        _() { __println("Widget:ctor: " + w_); }
        ~() { __println("Widget:dtor: " + w_); }
    }
}
Maker:Widget makeWidget() { Maker:Widget r(7); return r; }
void useWidget(Maker:Widget^ p) { __println("useWidget: " + p^.w_); }

/* an alias to the host class sees through to a hoisted member, exactly as a
   type-alias sees through a file-scope class's namespace (cf. Time:Count). */
alias HostAlias = Outer;

/* sibling hoisted classes that reference each other by REFERENCE — finite size
   and allowed (the by-VALUE sibling cycle is rejected below). */
Ring(int x_) {
    Ping(Pong^ p_) { }
    Pong(Ping^ p_) { }
}

/* a host member ENUM reached from a hoisted class, bare and qualified (the host
   const/alias forms are covered by Cfg/Outer above). */
Palette(int x_) {
    enum int Hue ( red, green, blue );
    Pen(int y_) {
        _() {
            int a = Hue:green;
            int b = Palette:Hue:green;
            __println("Pen: " + a + " " + b);
        }
        ~() { }
    }
}

/* a member alias + const defined INSIDE a hoisted class, named from OUTSIDE via
   the full path `Units:Metric:Real` / `Units:Metric:kScale`. */
Units(int x_) {
    Metric(int y_) {
        alias Real = float;
        const Real kScale = 2.5;
    }
}

/* one hoisted sibling held by VALUE inside another (acyclic) — transitive hooks
   fire (Pixel's ctor before Cell's, dtors in reverse). */
Frame(int x_) {
    Pixel(int v_) {
        _() { __println("Pixel:ctor: " + v_); }
        ~() { __println("Pixel:dtor: " + v_); }
    }
    Cell(Pixel p_) {
        _() { __println("Cell:ctor"); }
        ~() { __println("Cell:dtor"); }
    }
}

int32 main() {

    { Outer outer(1); }
    { Outer:Inner inner(2); }

    // a host field typed as a hoisted class — bare in the host; transitive hooks.
    { Box bx; __println("box.i = " + bx.i_.v_); }

    // sizeof + new / delete of a hoisted class.
    {
        __println("sizeof Item = " + sizeof(Box:Item));
        Box:Item^ p = new Box:Item(5);
        delete p;
    }

    // deeper nesting (3 levels) + a member enum at the bottom level.
    {
        Tree:Branch:Leaf leaf(8);
        __println("Side:right = " + Tree:Branch:Side:right);
    }

    // a hoisted class shadows a same-named file-scope class.
    { Node fn(1); }
    { Tower:Node hn(2); }

    // a host value member reached from a hoisted class (bare + qualified).
    { Cfg:Reader r(0); }

    // a hoisted class inside a LOCAL class (local + hoisted combined).
    {
        Loc(int x_) {
            Sub(int y_) {
                _() { __println("Sub:ctor: " + y_); }
                ~() { __println("Sub:dtor: " + y_); }
            }
        }
        Loc:Sub s(3);
    }

    // a hoisted class as a function return type + a pointer parameter.
    {
        Maker:Widget w = makeWidget();
        useWidget(w);
    }

    // an alias to the host sees through to a hoisted member.
    { HostAlias:Inner ai(5); }

    // sibling hoisted classes refer to each other by reference (finite size).
    __println("sizeof Ring:Ping = " + sizeof(Ring:Ping));

    // a host member enum reached from a hoisted class (bare + qualified).
    { Palette:Pen p(0); }

    // a member alias/const inside a hoisted class, named via the full path.
    {
        Units:Metric:Real s = Units:Metric:kScale;
        __println(##type(s) + " = " + s);
    }

    // a hoisted sibling held by value in another (acyclic) — transitive hooks.
    { Frame:Cell c; __println("c.pixel = " + c.p_.v_); }

    return 0;
}

/* compiler errors. */

/* a hoisted class is reached only as `Outer:Inner` — bare `Inner` at file scope
   is out of scope (it's a member, not a file-scope type). */
//-EXPECT-ERROR: 'Inner' needs a namespace qualifier
//int32 neg_bare_inner() {
//    Inner err1;
//    __println(##type(err1));
//    return 0;
//}

/* a deeper member needs the full path `Outer:Inner:Innerger`. */
//-EXPECT-ERROR: 'Innerger' needs a namespace qualifier
//int32 neg_bare_innerger() {
//    Innerger err2;
//    __println(##type(err2));
//    return 0;
//}

/* a hoisted class is NOT bound to a host object — it cannot see the host's FIELDS
   (only its namespace members). */
//-EXPECT-ERROR: Unresolved identifier 'hx'
//Holder(int hx) {
//    Peek(int py) {
//        _() { __println("" + hx); }
//        ~() { }
//    }
//}

/* a by-value cycle between two sibling hoisted classes is infinite size. */
//-EXPECT-ERROR: contains itself by value
//CycHost(int x_) {
//    A(B b_) { }
//    B(A a_) { }
//}

/* two field-bearing hoisted classes of the same name in one host collide — a re-open
   cannot add fields. */
//-EXPECT-ERROR: Duplicate definition of class 'Dup'; a re-open cannot add fields
//DupHost(int x_) {
//    Dup(int a_) { }
//    Dup(int b_) { }
//}
