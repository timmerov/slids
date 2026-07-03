/*
gap in implicit pointer-base compatibility check.

These cases violate the pointer-cast spec (void^ requires explicit cast
to any typed pointer) but currently compile silently. Only DeclStmt-with-
init and plain AssignStmt to a pointer variable invoke the Slids-level
pointee-type check via requirePtrSlidCompat. Every other implicit
pointer-init site only calls requirePtrInit, which checks the LLVM type
(both sides are "ptr") and accepts.

Pre-existing gap; tracked in TODO.md.
*/

void take_int8(int8^ p) {
}

int8^ return_void_as_int8() {
    void^ v = nullptr;
    return v;       /* gap: return statement. */
}

Box(int8^ data_ = nullptr) {
}

int32 main() {
    void^ v = nullptr;
    int8 a = 0;

    /* gap 1: function argument (emitArgForParam). */
    take_int8(v);

    /* gap 2: field assignment (FieldAssign). */
    Box b;
    b.data_ = v;

    /* gap 3: return statement (Return). */
    int8^ ret = return_void_as_int8();

    /* gap 4: tuple element initialization at decl-site (TupleExpr emit). */
    (int8^, int8^) tup = (v, v);

    /* gap 5: tuple element write via IndexAssign. */
    (int8^, int8^) tup2 = (^a, ^a);
    tup2[0] = v;

    __println("bug14: all gap cases compiled silently");
    return 0;
}
