/*
test imported templates defined in another source file.
this is the second-level template source: it compiles LAST of all (its
demands come from tmpl_lib.sl's own compile).
*/

import tmpl_lib2;

Wrap2<T>() {
    T dub() { return w_ + w_; }
}
