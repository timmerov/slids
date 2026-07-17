/*
test import and linked files.
this is an auxiliary library file.
it defines things declared in library.slh.
*/

/*
claude says:

this file exists to pin one rule: a member DECLARED in a header may be defined in ANY
ONE .sl, not only the sibling. the use case is a header declaring several classes with
a source file each — Bird is library.slh's, but it is not library.sl's.

it is NOT library.slh's sibling (that is library.sl, by base name), so it emits none of
Bird's SYNTHESIZED members. library.sl still emits those. that split is exactly what
made this case break: library.sl's complete @Bird__$ctor calls @Bird__$ctor__impl, and
the body of that impl is HERE, in another object. the sibling has to `declare` an impl
it does not define. it did not, so library.ll referenced a value it never defined —
invalid IR that slidsc emitted happily, both TUs compiled clean, and only llc caught.

a ctor/dtor is a method with restrictions, so `_(){}` gets this freedom for the same
reason `void Bird:chirp()` does. the hooks below are deliberately in the BLOCK re-open
form and the method in the EXTERNAL form: same file, both spellings, both non-sibling.
*/

import library;

Bird() {
    _() {
        __println("Bird:ctor: " + a + " " + b);
    }
    ~() {
        __println("Bird:dtor: " + a + " " + b);
    }
}

void Bird:chirp() {
    __println("Bird:chirp: " + a + " " + b);
}
