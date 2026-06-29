/*
test class as namespace.

constants, globals, aliases, enums, etc may be defined inside a class body.

classes aren't exactly namespaces.
you can't define functions inside a class.
you can't alias the class as a namespace - it's a type.

a class conceptually desugars to a namespace and a named tuple.
like a namespace, you may declare and define things within the class body.
naked code in the class body is a compile error.
the field list is a data tuple where the slots are accessed by name.
*/

/*
claude says:

a class is both a type and a namespace. its body holds member definitions —
aliases, consts, and enums. a method is a member too; what's excluded is a
non-member (free) function. qualify members with the class name: Space:Float (a
member type), Space:kPi (a member const), Space:Count:kOne (an enum member —
the enum keeps its name in the path; members don't flatten into the class).

a type-alias to a class sees through to both facets: via alias Time = Space,
Time:Count (type) and Time:Count:kZero (value) both resolve.

alias Space; is rejected — a class is a type, not an importable namespace.
*/

Space(int x_) {
    alias Float = float;
    const Float kPi = 3.14;
    enum int Count (kZero, kOne, kTwo, kThree);
}

alias Time = Space;

/* a namespace nested in a class body — Box:Util. a class may HOLD a namespace,
   the converse of a namespace holding a class. the nested namespace carries its
   own consts, functions, and deeper namespaces; a method reaches a sibling
   namespace member bare (Util:kScale), like any bare member access through self. */
Box(int n_) {
    Util {
        const int kScale = 10;
        int bump(int v) {
            return v + 1;
        }
        Deep {
            const int kZ = 7;
        }
    }
    int scaled() {
        return n_ * Util:kScale + Util:bump(n_);
    }
}

/* a class holding a namespace that itself holds a CLASS — Crate:Bin:Item, the
   deepest converse nesting (class in namespace in class). a method constructs the
   nested class (Bin:Item) and reaches a bare sibling method (seed). */
Crate(int lot_) {
    Bin {
        Item(int sku_) {
            int sku() { return sku_; }
        }
    }
    int seed() { return lot_ * 100; }
    int first() {
        Bin:Item it(seed());
        return it.sku();
    }
}

/* a METHOD signature typed by a sibling class defined LATER (Widget) — a regression
   for the forward-ref fix: a member signature type resolves after every name exists,
   so it may name any class regardless of order. */
Caller(int h_) {
    int useW(Widget^ w) { return w^.val(); }
}
Widget(int v_) {
    int val() { return v_; }
}

/* a class FIELD typed by a host MEMBER ALIAS (Reading) — aliases resolve first in
   the TYPES phase, with the host frame open, so the field type expands the alias. */
Gauge(int n_) {
    alias Reading = int;
    Dial(Reading r_) {
        int read() { return r_; }
    }
    int show() {
        Dial d(n_);
        return d.read();
    }
}

int32 main() {

    Space:Float press = 101.325;
    // should be Space:Float
    __println(##type(press) + " pressure = " + press);

    // should be const Space:Float
    __println(##type(Space:kPi) + " pi = " + Space:kPi);

    __println("count: " + Space:Count:kOne + " " + Space:Count:kTwo + " " + Space:Count:kThree);

    // should be Time:Count
    Time:Count zero = Time:Count:kZero;
    __println(##type(zero) + " zero = " + zero);

    /* a class holding a namespace (Box:Util) — qualified access, deep nesting,
       and a method that reaches the nested namespace's members bare. */
    Box b(3);
    __println("scaled = " + b.scaled());
    __println("Box:Util:kScale = " + Box:Util:kScale);
    __println("Box:Util:Deep:kZ = " + Box:Util:Deep:kZ);

    /* a class in a namespace in a class — internal use and external qualified
       construction (Crate:Bin:Item). */
    Crate cr(7);
    __println("crate first = " + cr.first());
    Crate:Bin:Item di(55);
    __println("Crate:Bin:Item = " + di.sku());

    /* a method signature forward-references a sibling class. */
    Caller ho(0);
    Widget wg(7);
    __println("useW = " + ho.useW(wg));

    /* a class field typed by a host member alias. */
    Gauge gg(5);
    __println("gauge = " + gg.show());

    return 0;
}

/* compile errors. */

// a class is a type, not an importable namespace.
//-EXPECT-ERROR: A class is a type, not an importable namespace.
//alias Space;

/* ----- bare (naked) code in a class body is a compile error ----- */
/* a class body holds only the ctor/dtor, member definitions, and methods —
   never a naked statement. tested across all three forms × three contexts. */

/* file-scope class: naked expression-statement. */
//-EXPECT-ERROR: A class body holds the constructor
//BareExpr(int x_) { __println("naked"); }

/* file-scope class: naked variable declaration. */
//-EXPECT-ERROR: A class body holds the constructor
//BareDecl(int x_) { int z = 5; }

/* file-scope class: naked control-flow. */
//-EXPECT-ERROR: A class body holds the constructor
//BareIf(int x_) { if (x_ > 0) { } }

/* hoisted class: naked expression-statement. */
//-EXPECT-ERROR: A class body holds the constructor
//BareHostExpr(int x_) {
//    Mem(int y_) { __println("naked"); }
//}

/* hoisted class: naked variable declaration. */
//-EXPECT-ERROR: A class body holds the constructor
//BareHostDecl(int x_) {
//    Mem(int y_) { int z = 5; }
//}

/* hoisted class: naked control-flow. */
//-EXPECT-ERROR: A class body holds the constructor
//BareHostIf(int x_) {
//    Mem(int y_) { if (y_ > 0) { } }
//}

/* local class: naked expression-statement. */
//-EXPECT-ERROR: A class body holds the constructor
//int neg_bare_local_expr() {
//    BareLoc(int x_) { __println("naked"); }
//    return 0;
//}

/* local class: naked variable declaration. */
//-EXPECT-ERROR: A class body holds the constructor
//int neg_bare_local_decl() {
//    BareLoc(int x_) { int z = 5; }
//    return 0;
//}

/* local class: naked control-flow. */
//-EXPECT-ERROR: A class body holds the constructor
//int neg_bare_local_if() {
//    BareLoc(int x_) { if (x_ > 0) { } }
//    return 0;
//}

/* ----- no visibility leak: members need the class qualifier ----- */

/* file-scope class (Space): a member alias is not visible bare. */
//-EXPECT-ERROR: 'Float' needs a namespace qualifier.
//int neg_leak_alias() { Float p = 1.0; return 0; }

/* file-scope class (Space): a member const is not visible bare. */
//-EXPECT-ERROR: 'kPi' needs a namespace qualifier.
//int neg_leak_const() { __println("" + kPi); return 0; }

/* file-scope class (Space): a member enum type is not visible bare. */
//-EXPECT-ERROR: 'Count' needs a namespace qualifier.
//int neg_leak_enum() { Count c = Space:Count:kZero; return 0; }

/* file-scope class (Space): a member enum value is not visible bare. */
//-EXPECT-ERROR: 'kOne' needs a namespace qualifier.
//int neg_leak_enum_value() { __println("" + kOne); return 0; }

/* file-scope class (Space): the enum keeps its name in the path — its members
   do not flatten into the class. */
//-EXPECT-ERROR: 'Space' has no member 'kOne'.
//int neg_leak_flatten() { __println("" + Space:kOne); return 0; }

/* file-scope class (Space): the enum's own name is not a bare namespace. */
//-EXPECT-ERROR: 'Count' is not a namespace.
//int neg_leak_enum_bare_qual() { __println("" + Count:kOne); return 0; }

/* hoisted class (Host:Mem): a member alias is not visible bare. */
//-EXPECT-ERROR: 'Float' needs a namespace qualifier.
//HostFloat(int x_) {
//    Mem(int y_) { alias Float = float; }
//}
//int neg_hoist_alias() { Float p = 1.0; return 0; }

/* hoisted class (Host:Mem): a member const is not visible bare. */
//-EXPECT-ERROR: 'kPi' needs a namespace qualifier.
//HostConst(int x_) {
//    Mem(int y_) { const int kPi = 3; }
//}
//int neg_hoist_const() { __println("" + kPi); return 0; }

/* hoisted class (Host:Mem): a member enum type is not visible bare. */
//-EXPECT-ERROR: 'Count' needs a namespace qualifier.
//HostEnum(int x_) {
//    Mem(int y_) { enum int Count (kZero, kOne, kTwo); }
//}
//int neg_hoist_enum() { Count c = HostEnum:Mem:Count:kZero; return 0; }

/* hoisted class (Host:Mem): a member enum value is not visible bare. */
//-EXPECT-ERROR: 'kOne' needs a namespace qualifier.
//HostEnumVal(int x_) {
//    Mem(int y_) { enum int Count (kZero, kOne, kTwo); }
//}
//int neg_hoist_enum_value() { __println("" + kOne); return 0; }

/* hoisted class (Host:Mem): the enum does not flatten into the hoisted class. */
//-EXPECT-ERROR: 'Host:Mem' has no member 'kOne'.
//Host(int x_) {
//    Mem(int y_) { enum int Count (kZero, kOne, kTwo); }
//}
//int neg_hoist_flatten() { __println("" + Host:Mem:kOne); return 0; }

/* hoisted class: the hoisted name needs its host — bare `Mem` is not reachable. */
//-EXPECT-ERROR: 'Mem' is not a namespace.
//HostMissing(int x_) {
//    Mem(int y_) { alias Float = float; }
//}
//int neg_hoist_missing_host() { Mem:Float p = 1.0; return 0; }

/* hoisted class: a member of the hoisted class is not a member of the host. */
//-EXPECT-ERROR: 'Float' is not a type in 'Host'.
//Host(int x_) {
//    Mem(int y_) { alias Float = float; }
//}
//int neg_hoist_wrong_level() { Host:Float p = 1.0; return 0; }

/* local class (Loc): a member alias is not visible bare. */
//-EXPECT-ERROR: 'Float' needs a namespace qualifier.
//int neg_local_alias() {
//    Loc(int x_) { alias Float = float; }
//    Float p = 1.0;
//    return 0;
//}

/* local class (Loc): a member const is not visible bare. */
//-EXPECT-ERROR: 'kPi' needs a namespace qualifier.
//int neg_local_const() {
//    Loc(int x_) { const int kPi = 3; }
//    __println("" + kPi);
//    return 0;
//}

/* local class (Loc): a member enum type is not visible bare. */
//-EXPECT-ERROR: 'Count' needs a namespace qualifier.
//int neg_local_enum() {
//    Loc(int x_) { enum int Count (kZero, kOne, kTwo); }
//    Count c = Loc:Count:kZero;
//    return 0;
//}

/* local class (Loc): a member enum value is not visible bare. */
//-EXPECT-ERROR: 'kOne' needs a namespace qualifier.
//int neg_local_enum_value() {
//    Loc(int x_) { enum int Count (kZero, kOne, kTwo); }
//    __println("" + kOne);
//    return 0;
//}

/* local class (Loc): the enum does not flatten into the local class. */
//-EXPECT-ERROR: 'Loc' has no member 'kOne'.
//int neg_local_flatten() {
//    Loc(int x_) { enum int Count (kZero, kOne, kTwo); }
//    __println("" + Loc:kOne);
//    return 0;
//}

/* ----- a namespace nested in a class body (Class:Ns) ----- */

/* ctor/dtor are method-shaped — illegal in a namespace, even one inside a class. */
//-EXPECT-ERROR: A constructor or destructor may only appear in a class body.
//NegNsDtor(int n_) {
//    Util { ~() { } }
//}

/* the nested namespace keeps its name in the path — its members do not flatten
   into the class. */
//-EXPECT-ERROR: 'Box' has no member 'kScale'.
//int neg_ns_no_flatten() { __println("" + Box:kScale); return 0; }

/* a nested-namespace member is not visible bare in a method — it needs the
   namespace qualifier (Util:kScale), like a free function in that namespace. */
//-EXPECT-ERROR: 'kScale' needs a namespace qualifier.
//NegNsBare(int n_) {
//    Util { const int kScale = 10; }
//    int bad() { return kScale; }
//}

/* a namespace nested in a class is a namespace, not a type — it can't be
   instantiated, just like a file-scope namespace. */
//-EXPECT-ERROR: 'Util' is not a type in 'Holder'.
//Holder(int h_) { Util { const int kX = 1; } }
//int neg_ns_member_not_type() { Holder:Util u; return 0; }
