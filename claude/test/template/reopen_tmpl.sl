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

CONSTRAINED LISTS: `<K=Rankings, V>` parses on every spelling — any parameter may carry
a function-arm constraint form, binders excluded. With fields it is a specialization
ARM (special.sl). A field-less one is a CONSTRAINED RE-OPEN, rule (b): a plain re-open
joins the catch-all arm of its arity; one spelling an arm's constraints EXACTLY joins
that arm (an incomplete specialization closes this way); any other FILTERS — it joins
every flavor, of any arm of its arity, whose arguments it admits
(TemplateInfo::filter_reopens on the family head, cloned after the arm's own openings
and re-spelled to the arm's names). A filter adds aliases, methods, operators — never
fields, never a `...`; a member it duplicates is caught when the flavor builds.
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

/* CONSTRAINED re-opens. Cbox has a catch-all and an INCOMPLETE int specialization;
   re-opens spelling `T=int` exactly join that arm (the first closes it with a field);
   the others FILTER across arms. */
alias Ints = <int|int8|int16>;
Cbox<T>(T a_) { int kind() { return 0; } }
Cbox<T=int>(int z_, ...) { int kind() { return 1; } }
Cbox<T=int>(int w_) { int sum() { return z_ + w_; } }
Cbox<T=int>() { int tri() { return z_ * 3; } }
Cbox<T=Ints>() { int fam() { return 7; } }                      // spans both arms
Cbox<U=<int8|bool>>(alias first = a_) { int pick() { return 8; } }   // renamed
Cbox<T=bool>() { bool op==(int v) { return v == 1; } }        // an operator

/* guthrie's shape: a filter of aliases, both parameters constrained, reached through
   `alias Ns;`. */
Gm { Gnode<Key, Value>(Key key_, Value value_) { } }
alias Gm;
Gnode<Key=int, Value=bool>(alias rank_ = key_, alias flag_ = value_) { }

/* a filter may implement a virtual the arm declared. */
Vbox<T>(T a_) { virtual int v(); }
Vbox<T=int>() { virtual int v() { return 50 + a_; } }
Vbox<T=bool>() { virtual int v() { return 60; } }

/* the qualified spelling filters too. */
Tq:Qbox<T=int>() { T four() { return 4; } }

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

    Cbox<int> c1 = (2, 3);
    Cbox<int8> c8 = (4);
    Cbox<bool> cb = (true);
    println(String + "cbox<int> = " + c1.kind() + " " + c1.sum() + " " + c1.tri()
        + " " + c1.fam());                                                          // 1 5 6 7
    println(String + "cbox<int8> = " + c8.kind() + " " + c8.fam() + " " + c8.pick()
        + " " + c8.first);                                                          // 0 7 8 4
    println(String + "cbox<bool> = " + cb.pick() + " " + cb.first + " " + (cb == 1));  // 8 true true
    Gnode<int, bool> g = (5, true);
    println(String + "gnode = " + g.rank_ + " " + g.flag_);                          // 5 true
    Vbox<int> vi = (1);
    Vbox<bool> vb = (false);
    println(String + "vbox = " + vi.v() + " " + vb.v());                             // 51 60
    println(String + "qbox four = " + q.four());                                     // 4
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

/* a filter reaches only the flavors it admits. */
//-EXPECT-ERROR: Class 'Cbox<int16>' has no method 'pick'
//int32 use_pick() { Cbox<int16> c = (1); return c.pick(); }

/* a filter's member may not duplicate the arm's (caught as the flavor builds). */
//-EXPECT-ERROR: Duplicate definition of 'kind'
//Cbox<T=int16>() { int kind() { return 9; } }
//int32 use_dup() { Cbox<int16> c = (1); return c.kind(); }

/* a filter adds members, not layout: no trailing `...`. */
//-EXPECT-ERROR: A constrained re-open of 'Cbox' cannot carry a trailing '...'
//Cbox<T=Ints>(...) { }

/* a filter's alias must name a field of some arm of its arity. */
//-EXPECT-ERROR: Class template 'Cbox' has no field 'zz'
//Cbox<T=Ints>(alias q = zz) { }

/* a constrained re-open needs its template first. */
//-EXPECT-ERROR: A constrained re-open needs the class template 'Cnone' declared before it.
//Cnone<T=int>() { }

/* its list must match an arm's COUNT. */
//-EXPECT-ERROR: A re-open's template list must match class template 'Cbox's.
//Cbox<T, U=int>() { }
