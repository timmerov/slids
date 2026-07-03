/*
test references.

references are a type of pointer.
they point to a single object.
references may be assigned, re-assigned, and set to nullptr.
you may not do math operations on references.

caret ^ after a type indicates a reference.
caret ^ after a reference type variable is the object.
caret ^ before a variable is a reference to the object.

    int^ ref = nullptr;
    int x = 0;
    ref = ^x;
    int y = ref^;

pointers may be compared to other pointers where the object is the same type.
iterators may be compared to references, and vice versa.
comparison operations: == !=
*/

/*
claude says:

- a reference is `Type^`. it stores the address of a single object; references
  lower to an LLVM `ptr`.
- `^var` (prefix) is the address of var, a reference. `ref^` (suffix) is the
  pointee, usable as an rvalue (read) or an lvalue (write-through).
- references compare only with `==` and `!=` (against any pointer of the same
  pointee type, and against `nullptr`). ordering (`< <= > >=`) is rejected — a
  reference has no sequence position. references support NO arithmetic.
*/

int32 main() {
    int x = 5;
    int^ ref = ^x;
    __println("ref^= " + ref^);              // 5

    /* write through the reference. */
    ref^ = 99;
    __println("x= " + x);                    // 99
    __println("ref^= " + ref^);              // 99

    /* re-point the reference. */
    int y = 7;
    ref = ^y;
    __println("ref^= " + ref^);              // 7

    /* identity comparison — only == and != (both directions). */
    int^ same = ^y;
    int^ other = ^x;
    __println("ref==same= " + (ref == same));    // true  (both -> y)
    __println("ref==other= " + (ref == other));  // false (y vs x)
    __println("ref!=other= " + (ref != other));  // true
    __println("ref!=same= " + (ref != same));    // false

    /* nullptr. */
    int^ none = nullptr;
    __println("none==nullptr= " + (none == nullptr));  // true
    __println("none!=nullptr= " + (none != nullptr));  // false
    none = ^x;
    __println("none==nullptr= " + (none == nullptr));  // false

    return 0;
}

/* references support no arithmetic — every math form is rejected. */
//-EXPECT-ERROR: Arithmetic is not allowed on a reference.
//void neg_preinc() {
//    int x = 0;
//    int^ ref = ^x;
//    ++ref;
//}
//-EXPECT-ERROR: Arithmetic is not allowed on a reference.
//void neg_postdec() {
//    int x = 0;
//    int^ ref = ^x;
//    ref--;
//}
//-EXPECT-ERROR: Arithmetic is not allowed on a reference.
//void neg_pluseq() {
//    int x = 0;
//    int^ ref = ^x;
//    ref += 1;
//}
//-EXPECT-ERROR: Arithmetic is not allowed on a reference.
//void neg_minuseq() {
//    int x = 0;
//    int^ ref = ^x;
//    ref -= 1;
//}
//-EXPECT-ERROR: Arithmetic is not allowed on a reference.
//void neg_stareq() {
//    int x = 0;
//    int^ ref = ^x;
//    ref *= 2;
//}
//-EXPECT-ERROR: Arithmetic is not allowed on a reference.
//void neg_slasheq() {
//    int x = 0;
//    int^ ref = ^x;
//    ref /= 2;
//}
//-EXPECT-ERROR: Arithmetic is not allowed on a reference.
//void neg_add() {
//    int x = 0;
//    int^ ref = ^x;
//    ref = ref + 1;
//}
//-EXPECT-ERROR: Arithmetic is not allowed on a reference.
//void neg_sub() {
//    int x = 0;
//    int^ ref = ^x;
//    ref = ref - 1;
//}
//-EXPECT-ERROR: Arithmetic is not allowed on a reference.
//void neg_mul() {
//    int x = 0;
//    int^ ref = ^x;
//    ref = ref * 2;
//}
//-EXPECT-ERROR: Arithmetic is not allowed on a reference.
//intptr neg_refdiff() {
//    int x = 0;
//    int y = 0;
//    int^ a = ^x;
//    int^ b = ^y;
//    intptr d = a - b;
//    return d;
//}

/* a comparison between pointers of different pointee types is rejected. */
//-EXPECT-ERROR: Pointer comparison requires the same pointee type.
//bool neg_pointee() {
//    int x = 0;
//    char c = 'a';
//    int^ ref = ^x;
//    char^ cref = ^c;
//    bool b = (ref == cref);
//    return b;
//}

/* references support no ordering — only == and != . */
//-EXPECT-ERROR: References support only '==' and '!=' comparison.
//bool neg_lt() {
//    int x = 0;
//    int y = 0;
//    int^ a = ^x;
//    int^ b = ^y;
//    return a < b;
//}
//-EXPECT-ERROR: References support only '==' and '!=' comparison.
//bool neg_le() {
//    int x = 0;
//    int y = 0;
//    int^ a = ^x;
//    int^ b = ^y;
//    return a <= b;
//}
//-EXPECT-ERROR: References support only '==' and '!=' comparison.
//bool neg_gt() {
//    int x = 0;
//    int y = 0;
//    int^ a = ^x;
//    int^ b = ^y;
//    return a > b;
//}
//-EXPECT-ERROR: References support only '==' and '!=' comparison.
//bool neg_ge() {
//    int x = 0;
//    int y = 0;
//    int^ a = ^x;
//    int^ b = ^y;
//    return a >= b;
//}
