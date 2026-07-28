/*
test refined classes.

classes may be refined in a run-time scope.
they can be temporarily re-written.
methods and operators can be changed.
syntactically it looks like re-opening.
re-opening happens in the same scope.
refining happens in a different runtime scope.

this is a bit of an illusion.
an entirely new derived class is created that shadows
the refined methods.
this gives the appearance of re-writing the class.
and makes the syntax friendly to humans.
the closed form: Class() { ... }
and external form: void Class:method() { ... }
are valid.

    Class(int x) { }
    Class cls;
    void fn2() {
        Class() { alias Integer = int; }
        void Class:method() { }
        cls.method();
    }

this desugars to:

    Class(int x) { }
    Class cls;
    void fn2() {
        Class : $Class() {
            void method() { }
        }
        (<Class:$Class^> ^cls)^.method();
    }

note:

refinements apply to the entire scope - not just to the
to statements after the refinement.
that's a bit odd syntactically for a redefinition to apply
*before* the method is redefined.
but it's consistent with how classes are handled elsewhere.
ie you don't have to forward declare methods.
you don't have to forward declare refined methods.

attempting to refine ctor/dtor or a virtual method is a
compile error.
attempting to add fields is a compile error.
*/

/*
claude says:

THE USURPER. re-opening a class from a RUN-TIME scope that is not its own does not
join the class — it mints an anonymous zero-field DERIVED class `$Class : Class` in
that scope (resolve's mintUsurpers, the ONE find-or-create funnel both trigger forms
share, so every opening in a scope merges into one usurper). refined members SHADOW
the base's through the ordinary most-derived-frame rule (declaredMemberOverloads stops
at the first frame carrying the name) — no new lookup rule.

TWO SPELLINGS, ONE FUNNEL. the BLOCK form `Class() { }` is folded into the usurper by
mintUsurpers; the EXTERNAL form `void Class:m() { }` relocates into it as an ordinary
local sibling, because collectScopeOpenings answers the refined name for a usurper.
minting runs BEFORE relocation, so the walk finds its target with no special-casing. a
field-BEARING `Class(int y) { }` is not a re-open in ANY scope — it declares a new local
class that merely shadows, and stays one.

TWO NAMES. the usurper's ENTRY registers under the REFINED name, so resolveName's
innermost-first walk finds it for the whole scope — that shadowing IS the usurpation,
and it costs nothing. but its own SPELLING is `$Class`, because only a kSlid handle
carries a def_id: every composite built over it (`Data[5]`, `Data^`, `Data[]`) interns
by pointee TypeRef, and sharing the refined name would make the two types
indistinguishable at the leaf and the whole retarget a silent no-op. two consequences
fall out: `Class:` inside a refined member means the BASE (current_base_name), and
diagnostics say `$Class` when they mean the refinement.

THE BASE IS PRE-RESOLVED. the usurper is registered under the very name it derives
from, so resolving its base SPELLING would find the usurper itself. its `_$base` param
carries the refined class's already-resolved HANDLE instead, and the two places that
would re-resolve it (registerClassBody's field-type pass, pushBaseChain) are guarded by
is_usurper.

THE RETARGET. an instance declared OUTSIDE the scope keeps its own type, so resolve
stamps each use with the usurper-mapped type (usurped_type; only resolve knows the
scope, only classify knows types). the map is RECURSIVE THROUGH THE TYPE CONSTRUCTORS
— `Data`, `Data^`, `Data[]` and `Data[5]` all retarget — which is what carries a
container declared outside into a template, and it is CHAIN-CLOSED (every ancestor of
the usurper maps to it) so a nested refinement wins in one lookup. sound because the
usurper adds no fields: reading the same storage as the derived type is a free offset-0
view.

TEMPLATES COMPOSE FOR FREE. `bubblesort(array, 5)` inside the scope infers `T = $Data`
and instantiates a SECOND flavor whose body finds the refined `less_than`; outside it
binds `T = Data`. no mechanism — the refinement is a real type, so ordinary inference
does the work. this is the whole motivation: retroactive, scoped conformance without an
orphan rule. a PLAIN function is never re-instantiated (`print` keeps `Data`), so the
generic is the only thing that crosses the boundary.

WHAT IT COSTS ELSEWHERE. two rules the refinement leans on had to be made real:
- an ARRAY of the usurper passed to a `Data[]` param decays to `$Data[]` and demotes —
  cast.sl allows a derived ITERATOR to demote to an ancestor iff the two are the SAME
  SIZE, which was stated but not enforced. now gated (classChainSameSize: no step down
  the chain adds a field), and the array->element decay rides the same gate.
- `rhs^.x_` where `x_` is a BASE field: explicit `obj.field` never walked the base
  chain, so an inherited field was reachable only from inside the class. classify now
  splices the `_$base` hops, the same chain a bare field name lowers to in a method.

REJECTED (canon): a ctor/dtor, and refining a method the base declared VIRTUAL — the
usurper's lifecycle would run for instances declared in the scope but not for the ones
retargeted into it, and a virtual override cannot reach an existing instance's vptr.
fields cannot be added at all: there is no field-bearing re-open spelling, and a second
opening that tries lands on the ordinary "a re-open cannot add fields". refinement is
RUN-TIME-scope only — the same non-local target in a file / namespace / class body
still errors per-segment.
*/

import string;

Data(int x_) {
    bool less_than(Data^ rhs) {
        return (x_ < rhs^.x_);
    }
}

void bubblesort<T>(mutable T[] data, int sz) {
    bool again = false;
    while {
        again = false;
        for (i : 1..sz) {
            if (data[i].less_than(data[i-1])) {
                data[i] <--> data[i-1];
                again = true;
            }
        }
    } (again);
}

void print(Data[] data, int sz) {
    String s = "data = [";
    for (i : 0..sz) {
        s = s + " " + data[i].x_;
    }
    s += " ]";
    println(s);
}

int32 main() {

    Data array[5] = (3,2,5,1,4);
    bubblesort(array, 5);
    print(array, 5);

    /* re-define less_than to be greater-than. */
    {
        bool Data:less_than(Data^ rhs) {
            return (x_ > rhs^.x_);
        }
        bubblesort(array, 5);
        print(array, 5);
    }

    /* the BLOCK form, and the ENTIRE-SCOPE rule: the refinement is in force for the
       whole block, so the sort ABOVE it already sees it. sorts ascending again. */
    {
        bubblesort(array, 5);
        print(array, 5);
        Data() {
            bool less_than(Data^ rhs) {
                return (x_ < rhs^.x_);
            }
        }
    }

    /* a refinement may ADD members, not only replace them, and the NAME is usurped:
       a Data declared in the scope IS the usurper, so it has the added method — and
       reaches the base field through the `_$base` sub-object. */
    {
        Data() {
            int twice() { return x_ * 2; }
        }
        Data d = (21);
        println(String + "twice = " + d.twice());        // 42
        println(String + "field = " + d.x_);             // 21
    }

    /* operators refine like any other member (canon: methods AND operators). */
    {
        Data() {
            bool op==(Data^ rhs) {
                return (x_ != rhs^.x_);                  // deliberately inverted
            }
        }
        Data p = (5);
        Data q = (5);
        println(String + "inverted == = " + (p == q));   // false
    }

    /* NESTED refinement: the inner scope refines what the name means THERE, so its
       usurper derives from the outer one and wins for the whole inner block. */
    {
        bool Data:less_than(Data^ rhs) {
            return (x_ > rhs^.x_);
        }
        {
            bool Data:less_than(Data^ rhs) {
                return (x_ < rhs^.x_);
            }
            bubblesort(array, 5);
            print(array, 5);                             // inner: ascending
        }
        bubblesort(array, 5);
        print(array, 5);                                 // outer: descending
    }

    /* a SIBLING scope never sees any of it — main's own scope binds the original. */
    bubblesort(array, 5);
    print(array, 5);                                     // ascending

    return 0;
}

/* a refinement cannot define a CONSTRUCTOR: the usurper's lifecycle would run for an
   instance declared in the scope but not for one retargeted into it. */
//-EXPECT-ERROR: A refinement cannot define a constructor for 'Ctd'
//Ctd(int c_) { }
//int32 neg_ctor() {
//    Ctd() { _() { } }
//    Ctd v = (1);
//    return v.x_;
//}

/* the destructor half of the same rule. */
//-EXPECT-ERROR: A refinement cannot define a destructor for 'Dtd'
//Dtd(int d_) { }
//int32 neg_dtor() {
//    Dtd:~() { }
//    Dtd v = (1);
//    return v.d_;
//}

/* refining a VIRTUAL method is rejected: an instance created before the scope was
   entered carries the base's vptr, which no override can reach. */
//-EXPECT-ERROR: A refinement cannot refine the virtual method 'vm'
//Vrd(int v_) { virtual int vm() { return v_; } }
//int32 neg_virtual() {
//    int Vrd:vm() { return 0; }
//    Vrd v = (1);
//    return v.vm();
//}

/* nor may a refinement ADD a virtual method — the usurper's vtable would be
   unreachable from the very instances the refinement is meant to cover. */
//-EXPECT-ERROR: A refinement cannot add a virtual method ('nv')
//Vad(int v_) { }
//int32 neg_add_virtual() {
//    Vad() { virtual int nv() { return 1; } }
//    Vad v = (1);
//    return v.nv();
//}

/* a refinement adds no FIELDS — that is what makes the usurper layout-identical and
   the retarget free. the first opening mints it; a field-bearing second opening is an
   ordinary re-open of the usurper and lands on the re-open field rule. */
//-EXPECT-ERROR: a re-open cannot add fields
//Fad(int f_) { }
//int32 neg_field() {
//    Fad() { int m() { return f_; } }
//    Fad(int extra_) { }
//    Fad v = (1);
//    return v.m();
//}
