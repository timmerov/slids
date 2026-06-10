/*
test sizeof built-in function.

sizeof returns the size in bytes of the type or expression.
the return type is intptr.
the sizeof a string literal includes the terminating null.

    sizeof(Type)
    sizeof(string-literal)
    sizeof(expression)

examples:

    Type x;
    Type^ ref;
    Type arr[N];

    sizeof(Type);
    sizeof(x);
    sizeof(ref);
    sizeof(ref^);
    sizeof(arr);
    sizeof("Hello, World!");  // 14

notes:
sizeof is usually a foldable constant expression.
sizeof accepts classes when they land.
sizeof accepts qualified types.
*/

/*
claude says:

- a kSizeofExpr. the grammar parses `sizeof` `(` then EITHER a type (the paren
  content starts with a type keyword -> parseType onto return_type) OR an
  expression (an identifier is ambiguous [alias vs variable] and is parsed as an
  expression, like ##type). an ident naming a type (alias / enum, bare or
  qualified) is measured as that type; any other ident is a value, measured by
  its type. sizeof never evaluates its operand.
- the size (widen::typeByteSize): a reference / iterator / nullptr is a pointer
  (8); a fixed array is its TOTAL bytes (product of dims x element size); a string
  literal is its byte length INCLUDING the terminating null (NOT sizeof(char[]) =
  8). otherwise it is the primitive's width.
- CONSTFOLD folds the statically-known operands (a type, a string, nullptr, an
  address-of, a plain ident) to a STRONG `intptr` literal — before const capture,
  so `const n = sizeof(int)` and `sizeof(a) + sizeof(b)` are compile-time
  constants. the residual operands that need type inference (a deref, an index,
  arithmetic) lower in CLASSIFY instead (also an `intptr` literal). sizeof in an
  array DIMENSION (`T arr[sizeof(int)]`) works too: a const-expression dim parses
  as an expression, bakes a provisional `[1]`, and constfold folds + bakes the
  real size in (see pointer/array.sl).
- a slid completed in another TU isn't sized until link — that path (a runtime
  `getelementptr null, 1` / `ptrtoint`) lands with classes (Phase 5/8); until then
  typeByteSize returns -1 only for a slid and classify reports an error.
- a value operand is UNEVALUATED: sizeof reads only its type, so an
  uninitialized local is fine — `sizeof(u)` on an undefined `u` measures its
  declared type and does not require definite assignment (an "unevaluated
  context" suppresses the use-before-init check, recursing through arithmetic /
  index / deref operands). the operand is still read-marked, so it is not swept
  as an unused local; sizeof does not COUNT as an assignment, so a later real
  read of `u` still errors.
*/

int32 foo() {
    return 0;
}

Box {
    const int32 k = 0;
}

Space {
    enum Compass ( north, east, south, west );
}

Simple(
    int x_ = -1,
    float64 y_ = 3.14
) {
    _() {
        __println("Simple:ctor: " + x_);
    }
    ~() {
        __println("Simple:dtor: " + x_);
    }
}

int32 main() {
    /* primitive widths. */
    __println("char= "    + sizeof(char));            // 1
    __println("int16= "   + sizeof(int16));           // 2
    __println("int= "     + sizeof(int));             // 4
    __println("int64= "   + sizeof(int64));           // 8
    __println("intptr= "  + sizeof(intptr));          // 8
    __println("float32= " + sizeof(float32));         // 4
    __println("float64= " + sizeof(float64));         // 8

    /* any pointer / iterator is 8. */
    __println("void^= "   + sizeof(void^));           // 8
    __println("char[]= "  + sizeof(char[]));          // 8

    /* a variable, a reference, a dereference, an array (total bytes), a
       multi-dimensional array, an array element, an address-of, an iterator. */
    int32  x = 5;
    int32^ ref = ^x;
    int32  arr[4];
    arr[0] = 0;
    int32  grid[3][5];
    grid[0][0] = 0;
    int32[] it = ^arr[0];
    __println("var= "      + sizeof(x));              // 4
    __println("ref= "      + sizeof(ref));            // 8
    __println("ref^= "     + sizeof(ref^));           // 4
    __println("arr= "      + sizeof(arr));            // 16
    __println("grid= "     + sizeof(grid));           // 60
    __println("element= "  + sizeof(grid[0][0]));     // 4
    __println("addrof= "   + sizeof(^x));             // 8
    __println("iter= "     + sizeof(it));             // 8
    __println("nullptr= "  + sizeof(nullptr));        // 8
    __println("arith= "    + sizeof(x + x));          // 4

    /* a string literal: content bytes + the terminating null. */
    __println("string= " + sizeof("Hello, World!")); // 14

    /* an array TYPE spelling as the operand (not a variable): total bytes. */
    __println("arrtype= "  + sizeof(int[3]));         // 12
    __println("arrtype2= " + sizeof(int[2][3]));      // 24

    /* an alias, a bare enum, and a namespace-qualified enum type all resolve to
       their underlying. */
    __println("alias= "     + sizeof(Integer));       // 8
    __println("enum= "      + sizeof(Dir));           // 4
    __println("qualified= " + sizeof(Space:Compass)); // 4

    /* sizeof is an intptr value, usable like any other. */
    intptr total = sizeof(int) + sizeof(int64);
    __println("sum= " + total);                       // 12

    /* sizeof is a compile-time constant: it folds in constfold, so it may
       initialize a const (typed or inferred) and feed a const expression. */
    const intptr cn = sizeof(int);
    const cm = sizeof(int16) + sizeof(int64);
    __println("const= "  + cn);                       // 4
    __println("const2= " + cm);                       // 10

    Simple simple;
    simple_ref = ^simple;
    intptr simple_size1 = sizeof(Simple);
    intptr simple_size2 = sizeof(simple);
    intptr simple_size3 = sizeof(simple_ref^);
    __println("simple_size = " + simple_size1 + " " + simple_size2 + " " + simple_size3);

    /* compile errors — each uncommented in isolation by the negative runner. */

    //-EXPECT-ERROR: 'foo' is a function, not a value or a type
    //__println("e= " + sizeof(foo));

    //-EXPECT-ERROR: 'Box' is a namespace, not a value or a type
    //__println("e= " + sizeof(Box));

    //-EXPECT-ERROR: 'nope' is not a value or a type
    //__println("e= " + sizeof(nope));

    /* void has no size. */
    //-EXPECT-ERROR: Cannot take sizeof of 'void'
    //__println("e= " + sizeof(void));

    /* a value operand is unevaluated: sizeof reads only the TYPE, so an
       uninitialized local needs no definite assignment, and the use-before-init
       suppression recurses through arithmetic. */
    int32 u;
    int32 v;
    __println("uninit= "  + sizeof(u));               // 4
    __println("uninit2= " + sizeof(v + 1));           // 4

    return 0;
}

alias Integer = int64;
enum Dir ( kN, kE, kS, kW );
