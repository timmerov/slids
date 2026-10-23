/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

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

it pins a SECOND rule, with tmpl_test.sl as the other half: THE SAME SYMBOL FROM TWO
TUs. `Twin` below is a file-scope class this header does not declare, so it is private
to this TU — and tmpl_test.sl has its own private `Twin`, same name, DIFFERENT layout
and behavior. a file-scope class takes no def_id suffix, so both TUs mangle their
`Box<Twin>` members and their `tdiff<Twin^>` to the IDENTICAL strings, and both objects
go into the one `tmpl_test` binary. it works for exactly one reason: a template flavor
whose type-list names a TU-LOCAL type emits `define internal` in the only TU that can
spell it, so the two definitions never see each other. anything that promotes such a
flavor to an external symbol links one TU's call to the other TU's layout SILENTLY —
no diagnostic, no link error, just a wrong read. that is what this pair guards.

the `import tmpl_lib;` below also makes this NON-SCANNED file write a bird.sli (the
header's `Part<int> : PDer` base flavor is demanded on sight of the header, used or
not). nothing orders tmpl_lib.ll after bird.ll, so that demand is only ever honored
because the samples demand Part<int> too — harmless here, and it is not the local-type
flavors: those are excluded from the pool by construction.
*/

import string;

import library;

import+ tmpl_lib;
alias slids;

/* THIS TU's private `Twin` — not declared in any header. two fields, scaled `tv()`. */
Twin(int a_ = 5, int b_ = 6) {
    int tv() { return a_ * 100 + b_; }
}

/* declared in library.slh. the flavors below are this TU's alone. */
int bird_twin() {
    Twin t1;
    Box<Twin> bx;
    int r = bx.has();
    bx.p_ = ^t1;
    r = r * 10 + bx.has();
    Twin t2(7, 8);
    int d = 0;
    if (tdiff(^t1, ^t2)) { d = 1000; }
    return bx.p_^.tv() + r + d;
}

/* a header-declared global DEFINED in this NON-SIBLING source (library.sl declares it
   too, via the header, but does not define it). any one .sl may be the definer. */
int from_bird = 77;

Bird() {
    _() {
        println(String + "Bird:ctor: " + a + " " + b);
    }
    ~() {
        println(String + "Bird:dtor: " + a + " " + b);
    }
}

void Bird:chirp() {
    println(String + "Bird:chirp: " + a + " " + b);
}
