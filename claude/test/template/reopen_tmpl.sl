/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
test reopening class templates.
*/

/*
claude says:

a class TEMPLATE re-opens through every spelling a plain class does — the same-scope
block, the namespace block, the qualified path, and `alias Ns;` — and the re-open's
template list is the binder surface every time: it must be spelled, its COUNT must
match, and its names bind BY POSITION (a re-open may rename).

THE QUALIFIED HEAD. `Ns:Box<T>() { }` used to parse as a NEW file-scope template
`Box<T>` deriving from the namespace Ns — grammar's looksLikeQualifiedScopeDef wanted
`( ) {` right after the last segment and never skipped the `<..>` group, and nothing
reported the stray template because nothing instantiated it. the lookahead now skips
the group; relocation moves the opening into the namespace opening that HOLDS the
template (a namespace opened in several files would otherwise take it into an opening
registered before the template exists).

THE WALK. every qualifier step is a MEMBER step or a BASE step (reopen_cls.sl owns the
rule); the last segment's base step reaches a template deriving from a plain class
(`Tb:Plain:Dbox<T>()` with `Plain : Dbox<T>`).

`alias Ns;` is the same scope: a bare re-open-shaped opening (no fields, no base) whose
name is a class in an aliased namespace becomes the explicit `Ns:X` form before
relocation (qualifyAliasedReopens); a class in two aliased namespaces must be qualified.

A HEADER TEMPLATE is defined by its module's source file: a consumer re-open is rejected
in every spelling — qualified and through `alias slids;` alike.

NOT HERE: a non-empty re-open list (`Ns:Box<T>(alias ..)`, `(...)`) still parses as
inheritance from Ns — it belongs to the field-alias re-open work.
*/

import string;
alias slids;
import vector;

/* the qualified re-open, a RENAMED qualified re-open (U binds positionally to T), and
   the namespace-block re-open — three openings, one template. */
Tq { Qbox<T>(T v_) { } }
Tq:Qbox<T>() { T twice() { return v_ + v_; } }
Tq:Qbox<U>() { U thrice() { return v_ * 3; } }
Tq { Qbox<T>() { T plus1() { return v_ + 1; } } }

/* `alias Ns;` puts Ns in the same scope: a bare opening re-opens Ns's template. */
Ta { Abox<T>(T v_) { } }
alias Ta;
Abox<T>() { T inc() { return v_ + 1; } }

/* a nested namespace path. */
To { Ti { Nbox<T>(T v_) { } } }
To:Ti:Nbox<T>() { T neg() { return 0 - v_; } }

/* the last segment's BASE step: Dbox<T> derives from the plain class Plain. */
Tb { Plain(int p_) { } Plain:Dbox<T>(T d_) { } }
Tb:Plain:Dbox<T>() { T sum() { return p_ + d_; } }

int32 main() {
    Tq:Qbox<int> q = (7);
    println(String + "qbox = " + q.twice() + " " + q.thrice() + " " + q.plus1());  // 14 21 8
    Tq:Qbox<float32> qf = (1.5);
    println(String + "qbox<float32> = " + qf.twice());                              // 3
    Abox<int> a = (6);
    println(String + "abox = " + a.inc());                                          // 7
    To:Ti:Nbox<int> n = (5);
    println(String + "nbox = " + n.neg());                                          // -5
    Tb:Dbox<int> d = (1, 2);
    println(String + "dbox = " + d.sum());                                          // 3
    return 0;
}

/* a qualified re-open must spell the template list... */
//-EXPECT-ERROR: a re-open of 'Qbox' must repeat its template list
//Tq:Qbox() { int one() { return 1; } }

/* ...with the primary's COUNT. */
//-EXPECT-ERROR: A re-open's template list must match class template 'Qbox's.
//Tq:Qbox<T, U>() { }

/* a template in two aliased namespaces: the bare re-open must be qualified. */
//-EXPECT-ERROR: 'Twin' is a class in more than one aliased namespace
//Xa { Twin<T>(T v_) { } }
//Xb { Twin<T>(T w_) { } }
//alias Xa;
//alias Xb;
//Twin<T>() { }

/* a consumer may not re-open a HEADER template — qualified... */
//-EXPECT-ERROR: defined by its module's source file; 'Vector' cannot be re-opened here
//slids:Vector<T>() { T extra(); }

/* ...nor through `alias slids;`. */
//-EXPECT-ERROR: defined by its module's source file; 'Vector' cannot be re-opened here
//Vector<T>() { T extra(); }
