/*
test aliases.

    alias Name = type;

##type returns the alias.
    Name x;
    ##type(x);

todo:

drop qualifiers:
    alias Namespace;

rename functions:
    alias Fn = LongOrWrongFunctionName;
use case is importing c functions with stupid names.
*/

alias Integer = int;
alias Float = float;
alias Whole = Integer;        // chained to another alias
alias IntPtr = int^;          // alias whose target is a pointer type

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

int32 main() {

    Integer x = 42;
    __println(##type(x) + " " + ##name(x) + " = " + x);

    Float y = 3.14;
    __println(##type(y) + " " + ##name(y) + " = " + y);

    Whole z = 7;              // Whole -> Integer -> int
    __println(##type(z) + " " + ##name(z) + " = " + z);

    Integer w;               // declare-then-assign through an alias type
    w = doubled(20);
    __println(##type(w) + " " + ##name(w) + " = " + w);

    Dir d = Dir:kNorth;
    __println(##type(d) + " " + ##name(d) + " = " + d);

    /* propagation: alias + same alias -> alias; + int -> drops to underlying;
       + literal -> alias (flexes in); a comparison -> bool. */
    Integer p = 10;
    Integer q = 20;
    int     m = 5;
    __println("p+q : " + ##type(p + q));      // Integer
    __println("p+m : " + ##type(p + m));      // int  (alias dropped)
    __println("p+1 : " + ##type(p + 1));      // Integer
    __println("p<q : " + ##type(p < q));      // bool

    /* an alias type carries a pointer suffix: a reference and an iterator. the
       alias label rides along the `^` / `[]` (an array dim goes after the name,
       so `Integer nums[3]` already worked — these are the suffix-before-name
       forms that needed the typed-decl lookahead). */
    Integer val = 100;
    Integer^ ref = ^val;
    __println(##type(ref) + " " + ##name(ref) + " = " + ref^);   // Integer^ ref = 100
    Integer nums[3];
    nums[0] = 11;
    nums[1] = 22;
    nums[2] = 33;
    Integer[] iter = ^nums[0];
    iter = iter + 1;
    __println(##type(iter) + " " + ##name(iter) + " = " + iter^); // Integer[] iter = 22

    /* a for-array by-REFERENCE loop var of an alias type — the alias base
       (Integer -> int) is resolved before the element-type match. */
    Integer total = 0;
    for (Integer^ e : nums) { total = total + e^; }
    __println("for-ref sum= " + total);                          // 66

    /* an enum type takes a reference suffix too. */
    Dir^ dp = ^d;
    __println(##type(dp) + " " + ##name(dp) + " = " + dp^);       // Dir^ dp = 0

    /* a namespace-qualified type with a suffix (the qualified-name walk then the
       suffix). */
    Space:Compass c = Space:Compass:south;
    Space:Compass^ cp = ^c;
    __println(##type(cp) + " " + ##name(cp) + " = " + cp^);       // Space:Compass^ cp = 1

    /* an alias whose target is a pointer (IntPtr = int^) resolves and keeps its
       label. */
    IntPtr ip = ^val;
    __println(##type(ip) + " " + ##name(ip) + " = " + ip^);       // IntPtr ip = 100

    /* an alias-reference param. */
    __println("bump= " + bump(^val));                            // 101

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

    return 0;
}

//-EXPECT-ERROR: Unknown type 'Nope'
//alias Bad = Nope;

//-EXPECT-ERROR: is part of a cycle
//alias Loop1 = Loop2;
//alias Loop2 = Loop1;

//-EXPECT-ERROR: Duplicate declaration of 'Integer'
//alias Integer = float;
