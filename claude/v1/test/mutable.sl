/*
rudimentary mutable keyword.

it's required for move and swap overloads.
that's tested elsewhere.

mutable applies only to a pointer passed to a
function or method.
*/

/* check methods. */
Class(int x_ = 0) {

    /* correct syntax */
    void copy1(mutable int8[] dst, int8[] src) { }
    void copy2(mutable Class^ dst, Class^ src) { }

    /* compile error: 'mutable' on non-pointer param. */
    // void wrong1(mutable int x) { }
}

/* correct syntax */
void copy3(mutable int8[] dst, int8[] src) { }
void copy4(mutable Class^ dst, Class^ src) { }

/* compile error: 'mutable' on non-pointer param (free function). */
// void wrong2(mutable int x) { }

/* check template methods. */
Box<T>(T value_ = 0) {
    /* correct syntax */
    op<--(mutable Box<T>^ other) {
        __println("Box<T>:op<--(mutable Box<T>^)");
    }
}

BadBox<T>(T value_ = 0) {
    /* compile error: missing 'mutable' on op<-- pointer param in template. */
    // op<--(BadBox<T>^ other) { }
}

/* ----------------------------------------------------------------------
   Default-const for indirect params.

   Slids passes by value or reference-to-const — a plain T^ / T[] param is
   read-only inside the body. `mutable T^` / `mutable T[]` opts in to writes.
   Writes through an unmarked indirect param are compile errors.
   ---------------------------------------------------------------------- */

/* positive: write through a mutable indirect param. */
void writeMut(mutable int^ p) { p^ = 99; }

/* positive: read-only is fine with no annotation. */
int readPlain(int^ p) { return p^; }

/* compile error: write through unmarked T^ — default reference-to-const. */
//-EXPECT-ERROR: const
// void writePlainPtr(int^ p) {
//     p^ = 5;
// }

/* compile error: write through unmarked T[] iterator. */
//-EXPECT-ERROR: const
// void writePlainIter(char[] buf) {
//     buf^ = 'x';
// }

/* compile error: pre-inc through unmarked iterator. */
//-EXPECT-ERROR: const
// void incPlainIter(char[] buf) {
//     ++buf^;
// }

/* compile error: delete unmarked pointer (rebind via free). */
//-EXPECT-ERROR: const
// void deletePlain(int^ p) {
//     delete p;
// }

/* ----------------------------------------------------------------------
   decl/def `mutable` agreement.

   A forward decl in the .slh (or earlier in the same .sl) and its def must
   match exactly on per-slot `mutable` annotations. Same canonical signature
   with different mutable bits is a compile error.
   ---------------------------------------------------------------------- */

/* compile error: decl has mutable, def does not — free fn. */
//-EXPECT-ERROR: mutable
// void decl_def_free(mutable int^ p);
// void decl_def_free(int^ p) { }

/* compile error: decl lacks mutable, def adds it — free fn. */
//-EXPECT-ERROR: mutable
// void decl_def_free2(int^ p);
// void decl_def_free2(mutable int^ p) { p^ = 0; }

/* compile error: decl-def mutable mismatch on a method. */
//-EXPECT-ERROR: mutable
// DeclDefMethod(int x_ = 0) {
//     void set(mutable int^ p);
// }
// DeclDefMethod() {
//     void set(int^ p) { }
// }

int32 main() {
    /* exercise positive template instantiation. */
    Box<int> bx;
    Box<int> by;
    bx <-- by;

    /* compile error verification messages. */
    __println("01: Not allowed: void wrong1(mutable int x) (member, non-pointer)");
    __println("02: Not allowed: void wrong2(mutable int x) (free fn, non-pointer)");
    __println("03: Not allowed: op<--(BadBox<T>^ other) (template missing 'mutable')");

    return 0;
}
