/*
test anonymous tuples.

a tuple is a comma separated list of types or data.
types can be mixed.
the slots are not named.
tuples are assign-able.
slots can be accessed by constant index.
tuples can be used whenever variables are used.
tuples are initializers to classes and arrays.
tuples may be nested.

    (1, 2, 3)
    ("Hello", 4.5, 7)
    pair = (Dir:kN, false);
    dir = pair[0];
    good = pair[1];
    other = pair;
    Class c = (123, "Maple");
    Class c(456, "Main");
    array[3][2] = ((1,2), (3,4), (5,6));

    ##type(pair) is (Dir, bool)
    (Dir, bool) function( (Dir, bool)^ tuple );
    pair = function(^other);

    #x desugars to:
    (##file, ##line, ##type(x), ##name(x), ^x)

tuples may be destructured into variables.
slots may be empty.

    (dir, result) = pair;
    (dir, ) = pair;

tuples may be operands in math operations.
the operation is applied by slot iteratively and recursively.
the types stored at each slot must be compatible via widening rules.
the tuples must have the same shape.

    (1,2,3) + (4,5,6) is (5,7,9)

a math operation between a scalar and a tuple applies the math
operation between the scalar and every slot iteratively and recursively.

    (1,2,3) + 7 = (8,9,10)

for loop iterates over a homogeneous tuple.

    for (x : (1,3,4)) { }

notes:

tuples generally must be size 2 or more.
a tuple of size 1 is a scalar.
a scalar is a tuple of size 1.
is this a parenthesized expression?
or a tuple of size 1?
answer: it's both.
they're interchangeable.

    (expr)

arrays are homogenous tuples that can be accessed by a runtime index.
*/

/*
claude says:

landing 1 — MVP. the smallest tuple that stands on its own.

covered here:
  - construct a tuple literal: (Dir:kN, false)
  - a tuple-typed local: (Dir, bool) pair = ...
  - whole-tuple copy with value semantics: (Dir, bool) other = pair;
  - read a slot by CONSTANT index, heterogeneous: pair[0] is Dir, pair[1] is bool
  - ##type renders the structured tuple type: ##type(pair) is (Dir, bool)

the tuple type is a real structured type object, not a type-string. the
(Dir, bool) spelling is rendered only for ##type and diagnostics.

a slot index must be a compile-time constant — the result type depends on a
static index. a runtime index is rejected (that is array subscript, not a tuple
slot).

deferred to later landings:
  - slot write (pair[0] = ...), destructuring ((a,b)=pair, empty slot (a,))
  - slot-wise math + scalar broadcast, move/swap (<-- / <-->)
  - tuple params/returns + tuple references
  - array-init-via-tuple, for-tuple, #x, class init
*/

enum Dir ( kN, kE, kS, kW );

/* landing 4 — tuple params / returns + tuple references */

(int, int) addpair( (int, int)^ p ) {       // by-value tuple param + tuple return
    return (p^[0] + 1, p^[1] + 1);
}

int firstRef( (int, int)^ pr ) {           // by-reference tuple param
    return pr^[0];                          // deref the ref, read slot 0
}

int32 main() {

    (Dir, bool) pair = (Dir:kN, false);

    (Dir, bool) other = pair;          // whole-tuple copy

    Dir  dir  = pair[0];               // const-index slot read (Dir)
    bool good = pair[1];               // const-index slot read (bool)

    __println(##type(pair));           // (Dir, bool)
    __println(##type(other));          // (Dir, bool)
    __println(##type(dir));            // Dir
    __println(##type(good));           // bool
    __println(good);                   // false

    /* landing 2 — slot write (destructure tests moved to destructure.sl) */

    pair[0] = Dir:kS;                  // write a slot by const index
    __println("w0= " + pair[0]);       // 2

    /* landing 3 — slot-wise math + scalar broadcast */

    (int, int, int) a3 = (1, 2, 3);
    (int, int, int) b3 = (4, 5, 6);

    (int, int, int) s3 = a3 + b3;      // slot-wise add: (5, 7, 9)
    __println("s3= " + s3[0] + " " + s3[1] + " " + s3[2]);   // 5 7 9

    (int, int, int) c3 = a3 + 7;       // scalar broadcast: (8, 9, 10)
    __println("c3= " + c3[0] + " " + c3[1] + " " + c3[2]);   // 8 9 10

    (int, int, int) d3 = 100 - a3;     // scalar on the LEFT: (99, 98, 97)
    __println("d3= " + d3[0] + " " + d3[1] + " " + d3[2]);   // 99 98 97

    /* landing 4 — tuple through functions */

    (int, int) q = (10, 20);
    (int, int) r = addpair(q);         // by-value param + tuple return
    __println("r= " + r[0] + " " + r[1]);   // 11 21

    __println("fr= " + firstRef(^q));  // by-reference param: 10

    /* assign a tuple from an array. */
    int8 a1[4] = (1,2,3,4);
    (int,int,int,int) t1 = a1;
    __println("t1=("+t1[0]+","+t1[1]+","+t1[2]+","+t1[3]+")");

    /* tuple of arrays. */
    (int[3], int[4]) t2 = ((1,2,3), (4,5,6,7));
    __print(##type(t2) + " t2 = ( (");
    for (i : 0..3) {
        __print(" " + t2[0][i]);
    }
    __print(" ) (");
    for (i : 0..4) {
        __print(" " + t2[i, 1]);
    }
    __println(")");

    /* a MULTI-DIM array slot (int[2][3]): the slot's own 2-D indexing composes
       with the tuple-slot read. */
    (int[2][3], int[4]) t3 = ( ((1,2,3),(4,5,6)), (7,8,9,10) );
    __println("t3= " + t3[0][0][0] + " " + t3[0][1][2] + " " + t3[1][3]);  // 1 6 10

    /* an ARRAY of tuples-of-arrays: a mixed array->tuple-slot->array index chain,
       handled by the per-segment index walk. */
    (int[3],int[4]) t4[2] = ( ((1,2,3),(4,5,6,7)), ((8,9,10),(11,12,13,14)) );
    __println("t4= " + t4[0][0][1] + " " + t4[1][1][3] + " " + t4[0][1][0]); // 2 14 4

    /* a STORE through the composed lvalue — the walk addresses array->tuple->array. */
    t4[0][0][1] = 99;
    __println("t4store= " + t4[0][0][1]);   // 99

    return 0;
}
