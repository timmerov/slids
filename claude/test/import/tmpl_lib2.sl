/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
no explicit canon.
*/

/*
claude says:

test imported templates defined in another source file.
this is the second-level template source: it compiles LAST of all (its
demands come from tmpl_lib.sl's own compile).
*/

import tmpl_lib2;

Wrap2<T>() {
    T dub() { return w_ + w_; }
}

/* the leaked overload's body — declared in this module's header, so external and
   linked into every binary here. 3 is neither library.sl's 1 (int) nor its 2
   (int64), so whichever one a call reaches is unambiguous in the golden. */
int pick(int8 x) {
    x;
    return 3;
}
