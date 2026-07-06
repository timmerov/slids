/*
test for over enum.

    for (var : enum) {body}

var and enum are required.
body may be empty.
parentheses and curly brackets are required.

desugars to:

    for (
        type var = enum.first
    ) (
        var <= enum.last
    ) {
        var++;
    } {
        body
    }


for the purposes of shadowing variables, there are 3 scopes counting the
enclosing scope:
normal local variable shadowing rules for scopes apply to these scopes.

    |--enclosing--------------|
    { for (var : enum) {body} }
                       |body|
          |--loop-var-------|

note:
the range is over the *first* enum defined
to the *last* enum defined.
not the minimum and maximum values of the enum.
if the enum is not contiguous, then the behavior is defined
but should be used with caution.

it is a compile error if enum.last < enum.first.
the loop body is unreachable code.

note:
should we check for contiguousness?
lean no.
*/

enum Color ( red, green, blue );
enum Level ( low = 10, mid, high );

/* a descending enum (first defined > last defined) — used by a negative below. */
enum Down ( hi = 5, lo = 1 );

/* non-contiguous: first = a(1), last = c(3) -> iterates values 1,2,3 (hits the
   non-member 2, skips b=5) -> 1+2+3 = 6, NOT the member sum. */
enum Sparse ( a = 1, b = 5, c = 3 );
/* single member. */
enum Solo ( only );
/* negative-valued: n2=-2, n1=-1, z=0. */
enum Neg ( n2 = -2, n1, z );
/* a char-underlying enum: a..d = 97..100. */
enum char Letter ( a = 'a', b, c, d );
/* an int64-underlying enum (a non-int, non-char underlying type). */
enum int64 Big ( bx = 1000000000000, by, bz );
/* a zero-member enum — used by a negative below. */
enum Empty ( );

Space {
    enum Dir ( north, east, south, west );
}

int sum_sparse() {
    int s = 0;
    for (Sparse x : Sparse) {
        s = s + x;
    }
    return s;
}

int count_solo() {
    int c = 0;
    for (Solo x : Solo) {
        c = c + 1;
    }
    return c;
}

int sum_neg() {
    int s = 0;
    for (Neg x : Neg) {
        s = s + x;
    }
    return s;
}

/* a namespace-qualified enum (qualified enum-ref + qualified member-refs). */
int count_dir() {
    int c = 0;
    for (Space:Dir d : Space:Dir) {
        c = c + 1;
    }
    return c;
}

/* a char-underlying enum with a char loop var. */
int count_letters() {
    int n = 0;
    for (char ch : Letter) {
        n = n + 1;
    }
    return n;
}

/* count the members: red..blue inclusive = 3. */
int count_colors() {
    int c = 0;
    for (Color x : Color) {
        c = c + 1;
    }
    return c;
}

/* sum the member values (0 + 1 + 2). */
int sum_colors() {
    int s = 0;
    for (Color x : Color) {
        s = s + x;
    }
    return s;
}

/* the loop var may be the underlying type instead of the enum. */
int count_via_int() {
    int c = 0;
    for (int x : Color) {
        c = c + 1;
    }
    return c;
}

/* a non-zero-start enum: first = low (10), last = high (12) -> 10+11+12. */
int sum_levels() {
    int s = 0;
    for (Level l : Level) {
        s = s + l;
    }
    return s;
}

/* an empty body still iterates. */
int empty_enum_body() {
    int last = 0;
    for (Color x : Color) {
    }
    return last;
}

/* break exits the enum loop. */
int enum_break() {
    int c = 0;
    for (Color x : Color) {
        if (x == 1) {
            break;
        }
        c = c + 1;
    }
    return c;
}

/* continue skips one member (green = 1). */
int enum_continue() {
    int s = 0;
    for (Color x : Color) {
        if (x == 1) {
            continue;
        }
        s = s + x;
    }
    return s;
}

/* nested enum loops. */
int nested_enum() {
    int total = 0;
    for (Color a : Color) {
        for (Color b : Color) {
            total = total + 1;
        }
    }
    return total;
}

/* a typeless enum loop var — its type is inferred from the first member, so it
   takes the enum type (Color). */
int count_typeless() {
    int c = 0;
    for (x : Color) {
        c = c + 1;
    }
    return c;
}

/* sum the member values through a typeless loop var. */
int sum_typeless() {
    int s = 0;
    for (x : Color) {
        s = s + x;
    }
    return s;
}

/* a typeless loop var over a char-underlying enum infers char. */
int letters_typeless() {
    int n = 0;
    for (ch : Letter) {
        n = n + 1;
    }
    return n;
}

/* a typeless loop var over a namespace-qualified enum. */
int dir_typeless() {
    int c = 0;
    for (d : Space:Dir) {
        c = c + 1;
    }
    return c;
}

/* nested TYPELESS loops over DIFFERENT enums — the synthesized bound is minted
   per loop in desugar, so the inner loop's bound can't clobber the outer's. */
int nested_typeless_diff() {
    int total = 0;
    for (a : Color) {
        for (b : Level) {
            total = total + 1;
        }
    }
    return total;
}

/* a labeled break from the inner loop exits the OUTER enum loop. */
int enum_labeled_break() {
    int c = 0;
    for (Color a : Color) {
        for (Color b : Color) {
            c = c + 1;
            if (b == 1) {
                break scan;
            }
        }
    } :scan;
    return c;
}

/* a numbered break exits both enum loops at once. */
int enum_numbered_break() {
    int c = 0;
    for (Color a : Color) {
        for (Color b : Color) {
            c = c + 1;
            if (b == 1) {
                break 2;
            }
        }
    }
    return c;
}

/* a labeled continue restarts the OUTER enum loop. */
int enum_labeled_continue() {
    int c = 0;
    for (Color a : Color) {
        for (Color b : Color) {
            if (b == 1) {
                continue outer;
            }
            c = c + 1;
        }
    } :outer;
    return c;
}

/* a typeless loop var reuses an enclosing local — observable after. The enum loop
   counts up to last and then past it, so the var ends at last + 1 (blue + 1 = 3). */
int enum_typeless_reuse() {
    int prev = 99;
    for (prev : Color) {
    }
    return prev;
}

/* an int64-underlying enum: bx + by + bz = 3 * 1e12 + 3. */
int64 sum_big() {
    int64 s = 0;
    for (Big v : Big) {
        s = s + v;
    }
    return s;
}

int32 main() {
    __println("count_colors() = " + count_colors());    // 3
    __println("sum_colors() = " + sum_colors());        // 3
    __println("count_via_int() = " + count_via_int());  // 3
    __println("sum_levels() = " + sum_levels());        // 33
    __println("empty_enum_body() = " + empty_enum_body());  // 0
    __println("enum_break() = " + enum_break());        // 1
    __println("enum_continue() = " + enum_continue());  // 2
    __println("nested_enum() = " + nested_enum());      // 9
    __println("sum_sparse() = " + sum_sparse());        // 6
    __println("count_solo() = " + count_solo());        // 1
    __println("sum_neg() = " + sum_neg());              // -3
    __println("count_dir() = " + count_dir());          // 4
    __println("count_letters() = " + count_letters());  // 4
    __println("count_typeless() = " + count_typeless());    // 3
    __println("sum_typeless() = " + sum_typeless());        // 3
    __println("letters_typeless() = " + letters_typeless());  // 4
    __println("dir_typeless() = " + dir_typeless());        // 4
    __println("nested_typeless_diff() = " + nested_typeless_diff());  // 9
    __println("enum_labeled_break() = " + enum_labeled_break());      // 2
    __println("enum_numbered_break() = " + enum_numbered_break());    // 2
    __println("enum_labeled_continue() = " + enum_labeled_continue());  // 3
    __println("enum_typeless_reuse() = " + enum_typeless_reuse());    // 3
    __println("sum_big() = " + sum_big());                  // 3000000000003
    return 0;
}

/*
negatives — one //-block uncommented per run.
*/

/* a descending enum is an empty loop (first 5 <= last 1 is false). */
//-EXPECT-ERROR: Invalid range.
//int neg_enum_descending() {
//    for (Down d : Down) {
//        __println(d);
//    }
//    return 0;
//}

/* a typeless loop var over a descending enum is flagged the same way. */
//-EXPECT-ERROR: Invalid range.
//int neg_enum_typeless_descending() {
//    for (d : Down) {
//        __println(d);
//    }
//    return 0;
//}

/* iterating a non-enum. */
//-EXPECT-ERROR: is not an enum
//int neg_not_enum(int n) {
//    for (int x : n) {
//        __println(x);
//    }
//    return 0;
//}

/* iterating an unknown name. */
//-EXPECT-ERROR: Unknown enum 'Nope'
//int neg_unknown_enum() {
//    for (Color x : Nope) {
//        __println(x);
//    }
//    return 0;
//}

/* a zero-member enum has nothing to iterate. */
//-EXPECT-ERROR: has no members to iterate
//int neg_empty_enum() {
//    for (Empty x : Empty) {
//        __println(x);
//    }
//    return 0;
//}
