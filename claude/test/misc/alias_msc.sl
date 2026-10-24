/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
test aliases.

    alias Name = Type;

##type returns the alias.
    Name x;
    ##type(x);

drop namespace qualifiers for the remainder of the scope:

    alias Namespace;

create sets of types for filtered template:

    alias SignedIntegers = <int|int8|int16|int32|int64>;
    void math_function<T=SignedIntegers>(T a, T b) { }

rename functions:
use case is importing c functions with stupid names.

    alias Fn = LongOrWrongFunctionName;
    alias fabs = fabsf;

field pseudonym:
sometimes we want to access a 3d vector as an array.
sometimes as named fields: x,y,z.
a standard library must use generic terms for its fields
like Map's key_, value_.
an author may give them pseudonyms: name_, address_.
feature is valid for all flavors of classes and templates:
complete, incomplete, completed, opaque, local, public,
inherited, virtual, etc.


    /* inline */
    Color(
        int cmp[3],
        alias r = cmp[0],
        alias g = cmp[1],
        alias b = cmp[2]
    ) { }

    /* reopen class */
    Vector3(int x, int y, int z) { }
    Vector3(
        alias x0 = x,
        alias x1 = y,
        alias x2 = z
    ) { }

    /* reopen template */
    Template<Key>(Key key) { }
    Template<Key>(alias name = key) { }

    /* chain fields to any depth. */
    A(int a) { }
    B(A b) { }
    C(B c) { }
    D(C d) { }
    D(alias e = d.c.b.a) { }

    /* flat data fields only - no indirection. */
    Acorn(int a) { }
    BCorn(
        Acorn^ b,
        Acorn[] c,
        alias d = b^.a  /* compile error. */
        alias e = c[0].a  /* compile error. */
        alias f = b,
        alias g = c
    ) { }
*/

/*
claude says:

field aliases. a NAME, never a slot: no layout, sizeof, construction,
or transfer change. kept in Node::field_aliases, apart from params.
path = children[0]: kIdentExpr root, then kFieldExpr / kIndexExpr hops.
grammar parseFieldAlias rejects ^ / ^^ outright; parseParamList
rejects alias outside a class field list (allow_incomplete gate).
resolve: reopens park aliases in ClassInfo::pending_aliases;
registerClassBody folds them into alias_params + the duplicate-member
check (fields, aliases, class-frame members). bare names in methods
ride collectMethodFields like fields. peekMemberType lets resolve's
early type peeks (for-iterable dispatch) see an alias.
checkTemplateAliasRoots: a class template's alias ROOTS are checked
at the definition, used or not (guthrie 471 was a NEW unused template,
not a reopen of the library MapNode). skipped when the base spelling
depends on the template's list. only the root; the rest is per flavor.
constfold folds index exprs inside field_aliases.
classify is the ONE expansion site: kFieldExpr field miss ->
findFieldAlias (base chain; a nearer field shadows) -> expandFieldAlias
rewrites the node in place to the cloned path, _$base hops included.
validateFieldAliases runs once per class (aliases_checked), used or
not, against a synthetic _$alias_probe entry pushed straight onto
tree.entries (addEntry asserts: no frame in classify).
template-source aliases are private like source fields
(memberDeclVisibleAt); a declare_only flavor drops them from source
reopen clones. refinements move field_aliases to the usurper.
untested here (single TU): opaque / cross-TU flavors, consumer aliases.
OPEN (agreed 2026-10-23, see memory project_field_alias_reopen_resume):
qualified / alias-open reopens (`Ns:X<.>(alias..)`, `alias Ns; X<.>`)
must reach Ns:X — today `Ns:X(nonempty)` is inheritance from Ns.
consumer alias-only reopens of header templates. `Key=Type` constrained
reopens (today a reopen list is placeholder names: ALL flavors).
`Ns:X<A,B>() { .. }` never reaches Ns:X (methods AND aliases lost):
grammar looksLikeQualifiedScopeDef doesn't skip `<..>`, so it parses as
a NEW file-scope template deriving from Ns, silent while unused.
*/

import string;
alias slids;

alias Integer = int;
alias Float = float;
alias Whole = Integer;        // chained to another alias
alias IntPtr = int^;          // alias whose target is a pointer type

const int kVecN = 3;
alias Vec = int[kVecN];       // alias TARGET has a const-EXPRESSION array dim

enum Dir ( kNorth, kSouth, kEast, kWest );

Space {
    enum Compass ( north, south );
}

int32 doubled(Integer n) {    // alias in param + return position
    return n + n;
}

int32 bump(Integer^ p) {      // alias-REFERENCE in param position
    return p^ + 1;
}

/* FIELD PSEUDONYMS — `alias name = path` in a class field list: a new label for a
   field / array element / embedded-field path. no storage, not a slot; reads and
   writes go through to the original. */

/* inline: array elements, plus the whole array. */
Color(
    int cmp[3],
    alias r = cmp[0],
    alias g = cmp[1],
    alias b = cmp[2],
    alias rgb = cmp
) {
    int sum() { return r + g + b; }               // bare alias reads in a method
    void bright() { r = 255; g += 1; b++; }        // bare alias store / aug / incr
    int green() { return self.g; }                // self.alias
}

/* reopen class */
Vector3(int x, int y, int z) { }
Vector3(
    alias x0 = x,
    alias x1 = y,
    alias x2 = z
) { }

/* reopen template: every flavor. */
Template<Key>(Key key) { }
Template<Key>(alias name = key) { }

/* chain fields to any depth. */
A(int a) { }
B(A b) { }
C(B c) { }
D(C d) { }
D(alias e = d.c.b.a) { }

/* a pointer FIELD may be named (the alias is the field, not what it points at). */
Acorn(int a) { int twice() { return a + a; } }
BCorn(
    Acorn^ b,
    Acorn[] c,
    alias f = b,
    alias g = c
) { }

/* an alias of an alias; a constant-EXPRESSION index. */
const int kGreen = 1;
Pixel(int ch[3], alias green = ch[kGreen], alias verde = green) { }

/* an alias of a CLASS-typed field: its fields and methods reach through it. */
Holder(Acorn nut, alias kernel = nut) { }

/* inherited: a base's alias through a derived object, and bare in a derived method. */
Shape(int sides, alias n = sides) { }
Shape : Square(int len) { int perimeter() { return n * len; } }
/* never instantiated: alias roots are still checked at the definition. */
Shape : Shaped<T>(T t, alias s = sides, alias nn = n, alias tt = t) { }

/* incomplete: the completing re-open adds aliases (one names a primary field). */
Part(int id, ...) { }
Part(int qty, alias count = qty, alias tag = id) { }

/* virtual class. */
Animal(int legs, alias feet = legs) { virtual int speak() { return feet; } }

int32 main() {

    Integer x = 42;
    println(String + ##type(x) + " " + ##name(x) + " = " + x);

    Float y = 3.14;
    println(String + ##type(y) + " " + ##name(y) + " = " + y);

    Whole z = 7;              // Whole -> Integer -> int
    println(String + ##type(z) + " " + ##name(z) + " = " + z);

    Integer w;               // declare-then-assign through an alias type
    w = doubled(20);
    println(String + ##type(w) + " " + ##name(w) + " = " + w);

    Dir d = Dir:kNorth;
    println(String + ##type(d) + " " + ##name(d) + " = " + d);

    /* an alias whose TARGET has a const-EXPRESSION dim (`alias Vec = int[kVecN]`):
       resolve expanded each use eagerly with the provisional dim; constfold bakes
       the alias target and REFRESHES the uses, so indexing, sizeof of the variable
       (entry type) and sizeof of the alias TYPE (node type) all see the real size. */
    Vec vrow = (4, 5, 6);
    vrow[1] = 50;
    println(String + ##type(vrow) + " vrow= " + vrow[0] + " " + vrow[1] + " " + vrow[2]);  // Vec vrow= 4 50 6
    println(String + "sizeof(vrow)= " + sizeof(vrow) + " sizeof(Vec)= " + sizeof(Vec));    // 12 12

    /* propagation: alias + same alias -> alias; + int -> drops to underlying;
       + literal -> alias (flexes in); a comparison -> bool. */
    Integer p = 10;
    Integer q = 20;
    int     m = 5;
    println(String + "p+q : " + ##type(p + q));      // Integer
    println(String + "p+m : " + ##type(p + m));      // int  (alias dropped)
    println(String + "p+1 : " + ##type(p + 1));      // Integer
    println(String + "p<q : " + ##type(p < q));      // bool

    /* an alias type carries a pointer suffix: a reference and an iterator. the
       alias label rides along the `^` / `[]` (an array dim goes after the name,
       so `Integer nums[3]` already worked — these are the suffix-before-name
       forms that needed the typed-decl lookahead). */
    Integer val = 100;
    Integer^ ref = ^val;
    println(String + ##type(ref) + " " + ##name(ref) + " = " + ref^);   // Integer^ ref = 100
    Integer nums[3];
    nums[0] = 11;
    nums[1] = 22;
    nums[2] = 33;
    Integer[] iter = ^nums[0];
    iter = iter + 1;
    println(String + ##type(iter) + " " + ##name(iter) + " = " + iter^); // Integer[] iter = 22

    /* a for-array by-REFERENCE loop var of an alias type — the alias base
       (Integer -> int) is resolved before the element-type match. */
    Integer total = 0;
    for (Integer^ e : nums) { total = total + e^; }
    println(String + "for-ref sum= " + total);                          // 66

    /* an enum type takes a reference suffix too. */
    Dir^ dp = ^d;
    println(String + ##type(dp) + " " + ##name(dp) + " = " + dp^);       // Dir^ dp = 0

    /* a namespace-qualified type with a suffix (the qualified-name walk then the
       suffix). */
    Space:Compass c = Space:Compass:south;
    Space:Compass^ cp = ^c;
    println(String + ##type(cp) + " " + ##name(cp) + " = " + cp^);       // Space:Compass^ cp = 1

    /* an alias whose target is a pointer (IntPtr = int^) resolves and keeps its
       label. */
    IntPtr ip = ^val;
    println(String + ##type(ip) + " " + ##name(ip) + " = " + ip^);       // IntPtr ip = 100

    /* a pointer-target alias as a condition tests the pointer against null. */
    IntPtr np = nullptr;
    if (ip) { println("ip truthy"); }                                   // ip truthy
    if (np) { println("np truthy"); } else { println("np falsy"); }     // np falsy
    println(String + "!np= " + !np);                                    // !np= true

    /* an alias-reference param. */
    println(String + "bump= " + bump(^val));                            // 101

    /* FIELD PSEUDONYMS. writes through the alias land in the original. */
    Color col;
    col.r = 10;
    col.g = 20;
    col.b = 30;
    println(String + "cmp= " + col.cmp[0] + " " + col.cmp[1] + " " + col.cmp[2]); // 10 20 30
    col.cmp[1] = 21;                                                    // read-through
    println(String + "g= " + col.g + " sum= " + col.sum());             // g= 21 sum= 61
    col.bright();
    println(String + "bright= " + col.r + " " + col.g + " " + col.b);   // 255 22 31
    println(String + "green= " + col.green());                          // 22
    col.r += 1;
    col.b++;
    println(String + "aug= " + col.cmp[0] + " " + col.cmp[2]);          // 256 32
    int^ rp = ^col.r;                                                   // address of an alias
    rp^ = 99;
    println(String + "addr= " + col.cmp[0]);                            // 99
    println(String + "type= " + ##type(col.r) + " " + ##type(col.rgb)); // int int[3]
    int rgb_sum = 0;
    for (v : col.rgb) { rgb_sum = rgb_sum + v; }                        // iterate the alias
    println(String + "rgb sum= " + rgb_sum);                            // 99+22+32 = 153
    println(String + "sizeof(Color)= " + sizeof(Color));                // 12 (no storage)

    /* a re-open's aliases are not slots: construction fills x, y, z. */
    Vector3 v3(1, 2, 3);
    v3.x1 = 20;
    println(String + "v3= " + v3.x0 + " " + v3.y + " " + v3.x2);        // 1 20 3

    /* every flavor of the template. */
    Template<int> ti(5);
    Template<float> tf(2.5);
    ti.name = ti.name + 1;
    println(String + "tmpl= " + ti.key + " " + tf.name);                // 6 2.5

    /* chained. */
    D dd;
    dd.e = 7;
    println(String + "chain= " + dd.d.c.b.a);                           // 7

    /* pointer / iterator fields named by an alias. */
    Acorn nut(4);
    Acorn nuts[2];
    nuts[1].a = 8;
    BCorn bc(^nut, ^nuts[0]);
    println(String + "ptr= " + bc.f^.a + " iter= " + bc.g[1].a);        // ptr= 4 iter= 8

    /* alias of an alias, constant-expression index. */
    Pixel px;
    px.verde = 9;
    println(String + "pixel= " + px.ch[1] + " " + px.green);            // 9 9

    /* alias of a class-typed field. */
    Holder h(nut);
    h.kernel.a = 6;
    println(String + "kernel= " + h.nut.a + " " + h.kernel.twice());    // 6 12

    /* inherited. */
    Square sq(4, 5);
    sq.n = 3;
    println(String + "square= " + sq.sides + " " + sq.perimeter());     // 3 15

    /* incomplete, completed by the aliasing re-open. */
    Part pt;
    pt.tag = 3;
    pt.count = 7;
    println(String + "part= " + pt.id + " " + pt.qty);                  // 3 7

    /* virtual. */
    Animal an;
    an.feet = 4;
    println(String + "animal= " + an.legs + " " + an.speak());          // 4 4

    /* local class. */
    Local(int span[2], alias lo = span[0], alias hi = span[1]) { }
    Local lc;
    lc.lo = 1;
    lc.hi = 2;
    println(String + "local= " + lc.span[0] + " " + lc.span[1]);        // 1 2

    //-EXPECT-ERROR: Cannot write to a const value.
    //const Color kc = Color();
    //kc.r = 1;

    //-EXPECT-ERROR: Unknown type 'Bogus'
    //Bogus q;

    //-EXPECT-ERROR: is a type, not a value
    //int32 v = Integer;

    //-EXPECT-ERROR: is a type, not a function
    //Integer(5);

    /* `Ident^ Ident` is read as a reference decl, not a bare XOR statement (which
       is not a statement form) — so `a^ b` looks for a type named `a`. */
    //-EXPECT-ERROR: 'a' is a variable, not a type
    //int a = 1;
    //a ^ b;

    /* `Ident[] Ident` (empty brackets) likewise reads as an iterator decl. */
    //-EXPECT-ERROR: 'a' is a variable, not a type
    //int a = 1;
    //a[] b;

    /* a name that resolves to a NON-type entry is reported by what it is, not as
       an "Unknown type": a namespace, a function, a constant. */
    //-EXPECT-ERROR: 'Space' is a namespace, not a type
    //Space ns;

    //-EXPECT-ERROR: 'doubled' is a function, not a type
    //doubled fn;

    //-EXPECT-ERROR: 'kc' is a constant, not a type
    //const int kc = 5;
    //kc cv;

    return 0;
}

//-EXPECT-ERROR: Unknown type 'Nope'
//alias Bad = Nope;

//-EXPECT-ERROR: is part of a cycle
//alias Loop1 = Loop2;
//alias Loop2 = Loop1;

//-EXPECT-ERROR: Duplicate declaration of 'Integer'
//alias Integer = float;

/* field pseudonyms: flat data only — no indirection. */
//-EXPECT-ERROR: a field alias cannot go through a pointer
//Nut(int a) { }
//Tree(Nut^ b, alias d = b^.a) { }

//-EXPECT-ERROR: a field alias cannot go through a pointer
//Nut(int a) { }
//Tree(Nut[] c, alias e = c[0].a) { }

//-EXPECT-ERROR: a field alias cannot go through a pointer
//Nut(int a) { }
//Tree(Nut^ b, alias d = b.a) { }

//-EXPECT-ERROR: A field alias index must be a compile-time constant.
//int gIdx = 0;
//Row(int v[3], alias m = v[gIdx]) { }

//-EXPECT-ERROR: Array index 3 is out of bounds for 'int[3]'.
//Row(int v[3], alias m = v[3]) { }

//-EXPECT-ERROR: Class 'Row' has no field 'w'.
//Row(int v[3], alias m = w) { }

//-EXPECT-ERROR: Duplicate member 'v' in class 'Row'.
//Row(int v[3], alias v = v[0]) { }

//-EXPECT-ERROR: Duplicate member 'm' in class 'Row'.
//Row(int v[3], alias m = v[0], alias m = v[1]) { }

//-EXPECT-ERROR: Duplicate member 'm' in class 'Row'.
//Row(int v[3]) { int m() { return 0; } }
//Row(alias m = v[0]) { }

//-EXPECT-ERROR: Field alias 'p' is part of a cycle.
//Row(int v[3], alias p = q, alias q = p) { }

//-EXPECT-ERROR: A field alias is legal only in a class field list.
//int pick(int v, alias m = v) { return v; }

//-EXPECT-ERROR: Expected '=' and a field path after the alias name.
//Row(int v[3], alias m) { }

//-EXPECT-ERROR: Class template 'Tri' has no field 'key_'.
//Shape : Tri<T>(T t, alias k = key_) { }

//-EXPECT-ERROR: Class template 'Bag' has no field 'u'.
//Bag<T>(T t, alias s = u) { }

//-EXPECT-ERROR: Class template 'Bag' has no field 'u'.
//Bag<T>(T t) { }
//Bag<T>(alias s = u) { }
