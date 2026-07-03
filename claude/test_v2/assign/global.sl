/*
test implementation of global variables.

the long form is slids-like syntax.

    global name ( decls ) { code }

the name is optional.
the declaration list may be empty.
the code block may be empty.
global slids stack with others with the same name.
global variables may be added at will.

there is no need for incomplete/closing syntax.

the recommended naming convention is tbd.
currently, going with a single lowercase word.

ctor/dtor appear together or not at all.
they are called at begin/end of scope.
the design is for ctor/dtor to be called once each.
but see below for caveat.
global slids without ctor/dtor are statically allocated.
global slids with ctor/dtor are lazy constructed.
a sentinel is invisibly added
the first time any variable of the global slid is accessed,
the sentinel is set, the ctor is called, and the dtor is
registered. see below.

as with class fields, global variables are initialized
to a foldable constant expression.
the type of a global variable may be inferred from its initializer.

like namespaces, globals are accessed using the name of
the global namespace as a qualifier.

name collisions within a single name space are detected
at compile time for intra- file collisions and at link
time for inter- file collisions.

globals declared within a class inherit the class name
as its namespace.
they are visible outside the class.

globals declared within a function or method are only
visible within that scope.

global dtors are called in the reverse order the global
ctors were called at runtime.
the contract is lazy instantiation.
the author may make construction eager for globals where order
matters ie if global.a_ must exist before global.b_ can
be instantiated, then the author "touches" a_ then b_.
this ensures b_'s dtor will be called before a_'s dtor.

examples:

static allocated global:

    global simple(
        garage_ = false
    ) {
    }

lazy allocated group of globals that share the same sentinel:
the ctor will be called on first access to any of the
declared variables.

    global parts(
        wheels_ = 0,
        doors_ = 0,
        seats_ = 0,
        Contents^ trunk_ = nullptr
    ) {
        _() {
            trunk_ = new Contents;
        }
        ~() {
            delete trunk_;
        }
    }

global variables within a class:

    Contents() {
        global (count_ = 0) { }
        global friend(spare_ = true) { }
        _() { }
        ~() { }
    }

    int main() {
        n = Contents:count_;
        sp = Contents:friend:spare_;
        return 0;
    }

global variable within a function or method:

    void foo() {
        global bool inited_ = false;
        if (inited_ == false) {
            inited_  = true;
        }
    }

    int main() {
        /* compile error. out of scope. */
        // inited_ = false;
        // foo:inited_ = true;
        return 0;
    }

globals may be shadowed by local variables.
use :: to access the global variable.

    global int x_ = 42;
    int32 main() {
        int x_ = 37;
        print(x_);    // prints 37
        print(::x_);  // prints 42
    }

global scope exists inside main code body.
typical usage:

    int32 main() {
        global;
        simple:garage_ = 42;
        return 0;
    }

customized usage:

    int32 main() {
        /* compile error: outside global scope. */
        // parts:wheels_ = 37;

        /* before globals. */
        {
            global;
            /* ctor is called */
            parts:doors_ = 17;

            /* compile error: double instantiation. */
            // global;
        }
        /* after globals. */

        /* compile error: outside global scope. */
        // parts:seats_ = 24;

        return 0;
    }

blech! right?
there are short forms that desugar to the long form.
in words, a bare global variable declared at any scope
goes in the unnamed global name space and is
static initialized.

at global scope:

    int x_ = 0; -->
    global int x_ = 0;  -->
    global (int x_ = 0) { }

within any code block:

    global int x_ = 0;  -->
    global (int x_ = 0) { }

if the special slid function main does not explicitly
instantiate global, then the compiler will automagically
insert it for you.

    int main() {
        return 0;
    }

desugars to:

    int main() {
        global;
        return 0;
    }

for normal usage, global management is invisible.

    global x_ = 0;
    int main() {
        return x_;
    }

notes:

globals declared in a .sl source file are local to the file.
globals declared in a .slh header file are visible to all other
linked files.

currently only compiling a single file is supported.

future work supports declaring globals within templates.
templates are compiled during the pre-link phase.
so they can't easily defer dtor registration et al
to the pre-link phase.

future work supports threads:
reference globals in the main process thread:
    global.default
reference globals specific to each thread:
    global.self
global desugars to global.self, not global.default.
despite the name.
the technique also applies to new.
allocate memory in a lock protected heap:
    new.default
allocate memory in an unprotected heap:
    new.self
again oddly, the default is .self, not .default.
global.default and global.self auto added to main.

under the hood:
main instantiates global.
global ctor has nothing to do other than maybe
initialize the variables.
global dtor calls some __$global_dtor_all function.
every compiled file adds some code tbd to a file's
instantiation output file - possibly the same file
used to instantiate templates - tbd.
the pre-linker aggregates the inst files.
checks for collisions.
creates space for all possible dtors.
when a ctor is called, and there is a non-empty dtor,
then the dtor is added to the pre-linker's dtor list.
then builds the dtor_all function that calls
all of the registered global dtor's in reverse order.
*/

/*
claude says:

STAGE 1 (file-scope scalar globals) — exercised below:
  - the `global` SHORT form, explicit type AND inferred-from-initializer
  - read, write, compound update
  - the `::` unnamed-global qualifier, and a local variable SHADOWING a global
  - NAMED groups (`global name(decls){}`) reached qualified as `name:member`
  - STACKING: a group re-declared merges into the one namespace
  - int / bool / float64 scalars

STAGE 2 (lazy groups) — exercised below:
  - a group WITH a ctor/dtor is LAZILY constructed on FIRST access (a sentinel gate);
    the ctor runs exactly once, no matter how many times a member is touched
  - a lazy group that is NEVER accessed is neither constructed nor destroyed
  - dtors run at program exit in REVERSE construction order (a ctor that touches
    another group nests it, so the inner group is torn down first)
  - the ctor/dtor pairing rule (both or neither) is enforced (a negative, elsewhere)

NESTING — a global is a scope member like a const/alias/enum: it takes the enclosing
scope's namespace, with no bespoke logic (the same registerScopeNames path). Exercised:
  - a global in a NAMESPACE, reached `Ns:member`
  - a global in a CLASS, reached `Class:member` (visible outside the class)
  (Both static and lazy nest; a lazy class group is `Crate:cargo` in the v1 reference.)

THE `global;` SCOPE STATEMENT — opens the global lifetime for its enclosing scope; at
that scope's exit the lazy-global dtor registry runs. Auto-inserted at the top of `main`
when omitted (so the lifetime spans all of main); may be spelled explicitly (below), and
placed in a nested block to scope teardown earlier. It may appear only in `main`.

FUNCTION / METHOD-INTERNAL globals — a block-scope `global [Type] name = init;` is a
SCOPED STATIC: one persistent storage cell, static-initialized, reached BARE inside its
function/method body and invisible outside (a function is not a namespace). Two bodies'
same-named internal globals are independent. Exercised below (`tick`, `Counter:bump`).
A block-scope GROUP works the same way with no special casing — a group is a namespace,
and namespaces nest in functions, so `global (…){…}` / `global name(…){…}` inside a
function gives function-internal statics with a shared lazy lifetime (`taggify` below).

BARE form — at namespace / file scope (where there are no statements) the `global`
keyword is optional: a plain var-decl IS a global (`int x = 0;` desugars to `global int
x = 0;`), typed or inferred, nesting into its scope's namespace like the keyword form.
In a CODE BLOCK the keyword stays required (no keyword = a local). Exercised below
(`bare_`, `loose_`, `Space:height_`).

ANONYMOUS group — `global (a=…, b=…) {…}`: its members promote into the ENCLOSING
scope (bare / `::` at file scope, `Enclosing:member` in a namespace/class), NOT under a
group name. It is the canonical long form the short/bare spellings desugar to. Resolve
dissolves it into bare members, so no group qualifier exists. A STATIC anon group (empty
body) is just N bare globals (`anon_p_`/`anon_q_`, `Space:depth_`). A LAZY anon group
(with `_()`/`~()`) also dissolves — its members go bare and its ctor/dtor move into a
GENERATED plain namespace of the enclosing scope (whose bodies resolve the members bare
up the frame chain), rejoined into one shared lazy lifetime; construction/teardown
behave exactly like a named lazy group. Works at file / namespace / CLASS scope alike
(`tank_` at file scope, `Bin:fill_` in a class), LIFO with the named groups.

COMPOUND globals (array / tuple / class, and any aggregate containing a class) are
LAZY — the all-compound-lazy policy: storage is a zero-init `@`-global, and codegen
synthesizes a ctor (fills the array/tuple init or the class construction args + field
defaults, then fires the ctors on first touch) and, for a class-containing type, a dtor
(at program exit, LIFO). This reuses the whole lazy-group machinery (sentinel / touch /
registry). Scalars keep their static constant init. Construction with args is ordinary
field-fill (there are no separate ctor parameters), so `global Widget w = Widget(42)`
works exactly like the local form. Exercised below (`grid_`, `combo_`, `w_`).

Globals are now complete across every scope (file / namespace / class / function) and
every type (scalar / array / tuple / class). Nothing is deferred.
*/

/* SHORT form — explicit type and inferred type, across scalar kinds. */
global int shots_ = 42;
global tally_ = 0;            /* type inferred: int */
global bool ready_ = false;
global float64 ratio_ = 2.5;

/* SCALAR-KIND breadth + a const-substituted init: a const folds into a global's
   initializer (globals themselves are never substituted, so `shots_ + 1` below is
   the negative), and float32 / int64 / char globals ride the same static path. */
const int kBonus = 5;
global int fromconst_ = kBonus + 3;   /* const substitutes -> folds to 8 */
global float32 f32_ = 1.5;
global int64 big_ = 5000000000;
global char ch_ = 'A';

/* ENUM-typed global — an enum value is a foldable constant, so it initializes a
   static global just like any scalar. */
enum Hue ( kRed = 0, kGreen = 1, kBlue = 2 );
global Hue hue_ = Hue:kGreen;

/* BARE form — at file scope the `global` keyword is optional: a plain var-decl IS a
   global (`int x = 0;` desugars to `global int x = 0;`). Typed and inferred. */
int bare_ = 100;
loose_ = 7;                  /* bare + inferred type: int */

/* ANONYMOUS group (empty body) — the canonical long form: its members promote into
   the ENCLOSING scope, reached bare / via `::` at file scope (no group qualifier). */
global (
    anon_p_ = 11,
    anon_q_ = 22
) { }

/* COMPOUND globals (array / tuple / class) are LAZY: zero-init storage + a synthesized
   ctor that fills them on first touch (and, for a class, a synthesized dtor at exit). */
global int grid_[3] = (10, 20, 30);       /* array, constant init */
global (int, bool) combo_ = (7, true);    /* tuple, constant init */

/* NAMED group — members reached as `garage:<member>`. */
global garage(
    cars_ = 2,
    open_ = true
) { }

/* STACKING — `garage` re-declared; both openings share one namespace. */
global garage(
    lifts_ = 1
) { }

/* LAZY group — the ctor runs on FIRST access, the dtor at program exit. */
global depot(
    stock_ = 0
) {
    _() { stock_ = 50; __println("depot:ctor"); }
    ~() { __println("depot:dtor"); }
}

/* A lazy group NEVER accessed: neither its ctor nor its dtor may run. */
global unused(
    z_ = 0
) {
    _() { __println("unused:ctor"); }
    ~() { __println("unused:dtor"); }
}

/* Reverse construction order: `front`'s ctor touches `back`, so `back` is built
   DURING front's ctor -> back's dtor runs BEFORE front's dtor (LIFO teardown). */
global front(
    a_ = 0
) {
    _() { __println("front:ctor"); back:b_ = 9; }
    ~() { __println("front:dtor"); }
}
global back(
    b_ = 0
) {
    _() { __println("back:ctor"); }
    ~() { __println("back:dtor"); }
}

/* DEEPER reverse-order teardown (beyond front->back's two levels): a 3-level lazy
   chain. lvl1's ctor touches lvl2, whose ctor touches lvl3 — so construction is
   lvl1 -> lvl2 -> lvl3 and teardown is lvl3 -> lvl2 -> lvl1 (LIFO). */
global lvl1( q_ = 0 ) { _() { __println("lvl1:ctor"); lvl2:q_ = 1; } ~() { __println("lvl1:dtor"); } }
global lvl2( q_ = 0 ) { _() { __println("lvl2:ctor"); lvl3:q_ = 1; } ~() { __println("lvl2:dtor"); } }
global lvl3( q_ = 0 ) { _() { __println("lvl3:ctor"); } ~() { __println("lvl3:dtor"); } }

/* LAZY ANONYMOUS group — a nameless lazy singleton: its member is BARE (no group
   qualifier), constructed on first access, torn down at exit (LIFO with the rest). */
global (
    tank_ = 0
) {
    _() { tank_ = 5; __println("tank:ctor"); }
    ~() { __println("tank:dtor"); }
}

/* NESTING — a global slots into a namespace / class exactly like a const. */
Space {
    global int width_ = 8;               /* reached as Space:width_ */
    int height_ = 5;                     /* BARE (no keyword) in a namespace: Space:height_ */
    global ( depth_ = 3 ) { }            /* ANON group in a namespace: Space:depth_ */
}
Bin() {
    global int cap_ = 4;                  /* class-namespaced: reached as Bin:cap_ */
    /* LAZY anon group in a class: bare member `Bin:fill_`, lazy ctor/dtor */
    global ( fill_ = 0 ) {
        _() { fill_ = 6; __println("bin:ctor"); }
        ~() { __println("bin:dtor"); }
    }
    _() {}
    ~() {}
}

/* FUNCTION-INTERNAL static — a scoped static: ONE persistent cell, visible only
   inside `tick` (invisible outside; the "is not a namespace" negative below). */
void tick() {
    global int ticks_ = 0;
    ticks_ = ticks_ + 1;
    __println("tick=" + ticks_);
}

/* CLASS-by-value global — a "global slid": lazily constructed (ctor on first access),
   destructed at program exit (LIFO with the other lazy groups). Construction fills
   fields positionally from the args and defaults the rest — here `id_` from the arg,
   `tag_` from its default (construction is field-fill; there are no separate ctor
   parameters), exactly as for a local `Widget w = Widget(42)`. */
Widget(int id_ = 100, int tag_ = 7) {
    _() { __println("widget:ctor"); }
    ~() { __println("widget:dtor"); }
}
global Widget w_ = Widget(42);

/* POINTER-typed global — a static null init (a pointer is a foldable constant, so
   it stays static, not lazy). Read + null-compared in main. */
global Widget^ wp_ = nullptr;

/* METHOD-INTERNAL static — same, scoped to the method body. */
Counter() {
    void bump() {
        global int hits_ = 0;
        hits_ = hits_ + 1;
        __println("hits=" + hits_);
    }
    _() {}
    ~() {}
}

/* BLOCK-SCOPE group — a group declared inside a function. A group is a namespace, and
   namespaces nest in functions, so this needs no special casing: `count_` is a
   function-internal static with a shared LAZY lifetime (ctor on first call, dtor at
   program exit, LIFO). Reached bare in the function, invisible outside. */
void taggify() {
    global ( count_ = 0 ) {
        _() { __println("tag:ctor"); }
        ~() { __println("tag:dtor"); }
    }
    count_ = count_ + 1;
    __println("tag #" + count_);
}

int32 main() {
    /* NEGATIVE (sits BEFORE `global;`, so uncommented it accesses a global outside
       the open scope — the explicit `global;` below suppresses the auto-insert). */
    //-EXPECT-ERROR: outside the 'global;' scope
    //shots_ = 99;

    /* `global;` opens the global lifetime for main's scope (the lazy dtor registry
       runs at main's exit). It is auto-inserted here when omitted; spelling it is
       optional and behaves identically. It may appear only in `main`. */
    global;

    /* NEGATIVE: a SECOND `global;` while one is already open — double instantiation. */
    //-EXPECT-ERROR: second 'global;'
    //global;

    /* short-form: read, write, compound update */
    __println("shots=" + shots_);            // 42
    shots_ = shots_ + 1;
    __println("shots=" + shots_);            // 43

    tally_ = tally_ + 5;
    __println("tally=" + tally_);            // 5

    __println("ready=" + ready_);            // false
    ready_ = true;
    __println("ready=" + ready_);            // true

    __println("ratio=" + ratio_);            // 2.5

    /* bare (keyword-less) file-scope globals */
    __println("bare=" + bare_);              // 100
    bare_ = bare_ + 1;
    __println("bare=" + bare_);              // 101
    __println("loose=" + loose_);            // 7

    /* anonymous group: its members are bare, like standalone globals */
    __println("p=" + anon_p_);               // 11
    anon_p_ = anon_p_ + 1;
    __println("p=" + anon_p_);               // 12
    __println("q=" + anon_q_);               // 22

    /* compound globals (lazy): array (read + element write), tuple, class-by-value */
    __println("grid=" + grid_[0] + "," + grid_[1] + "," + grid_[2]);   // 10,20,30
    grid_[1] = 99;
    __println("grid1=" + grid_[1]);          // 99
    __println("combo=" + combo_[0] + "," + combo_[1]);                 // 7,true
    __println("wid=" + w_.id_ + "," + w_.tag_);   // widget:ctor, then 42,7 (arg + default)

    /* scalar-kind breadth + const-into-global; and a pointer global (null) */
    __println("fromconst=" + fromconst_);    // 8 (kBonus + 3, const substituted)
    __println("f32=" + f32_);                // 1.5
    __println("big=" + big_);                // 5000000000
    __println("ch=" + ch_);                  // A
    __println("hue=" + hue_);                // 1 (Hue:kGreen)
    if (wp_ == nullptr) { __println("wp null"); }   // wp null

    /* shadowing: a local `shots_` hides the global; `::shots_` reaches it */
    {
        int shots_ = 7;
        __println("local=" + shots_);        // 7
        __println("global=" + ::shots_);     // 43
    }

    /* named group: qualified read + write, incl. the stacked member */
    __println("cars=" + garage:cars_);       // 2
    __println("open=" + garage:open_);       // true
    __println("lifts=" + garage:lifts_);     // 1
    garage:cars_ = garage:cars_ + 10;
    __println("cars=" + garage:cars_);       // 12

    /* --- Stage 2: lazy groups --- */

    /* first access constructs `depot`; a repeat access does NOT re-run its ctor */
    __println("depot before");
    __println("stock=" + depot:stock_);      // depot:ctor, then 50
    __println("stock=" + depot:stock_);      // 50 (ctor already ran)

    /* reverse-order teardown: touching `front` builds `back` mid-ctor */
    front:a_ = 1;                            // front:ctor, then back:ctor
    __println("back=" + back:b_);            // 9

    /* lazy ANONYMOUS group: bare member, lazy ctor on first access */
    __println("tank=" + tank_);              // tank:ctor, then 5

    /* `unused` is never touched -> no unused:ctor / unused:dtor appears */

    /* --- nesting: a global slots into a namespace / class like a const --- */
    __println("width=" + Space:width_);      // 8
    Space:width_ = 12;
    __println("width=" + Space:width_);      // 12
    __println("height=" + Space:height_);    // 5 (bare namespace global)
    __println("depth=" + Space:depth_);      // 3 (anon group in a namespace)
    __println("cap=" + Bin:cap_);            // 4
    __println("fill=" + Bin:fill_);          // bin:ctor, then 6 (lazy anon in a class)

    /* function- and method-internal statics: one persistent cell each, per scope */
    tick();                                  // tick=1
    tick();                                  // tick=2
    Counter cnt;
    cnt.bump();                              // hits=1
    cnt.bump();                              // hits=2

    /* block-scope group: function-internal statics, shared lazy lifetime */
    taggify();                               // tag:ctor, then tag #1
    taggify();                               // tag #2

    /* 3-level lazy chain: touching lvl1 constructs lvl1 -> lvl2 -> lvl3 */
    lvl1:q_ = 9;                             // lvl1:ctor, lvl2:ctor, lvl3:ctor

    /* at exit, dtors run LIFO by construction order:
       lvl3, lvl2, lvl1, tag, bin, tank, back, front, depot, widget */
    return 0;
}

/* --- file-scope NEGATIVES (each uncommented one at a time by the negative runner) --- */

/* `global;` is only legal in `main`. */
//-EXPECT-ERROR: may appear only in 'main'
//void elsewhere() { global; }

/* a global constructor without a matching destructor (the pairing rule). */
//-EXPECT-ERROR: requires a matching destructor
//global lonely(a_ = 0) { _() {} }

/* the ANON-group form of the same pairing violation (name_tok is unset for an anon
   group, so the caret must fall back to the `global` keyword, not tokens[-1]). */
//-EXPECT-ERROR: requires a matching destructor
//global ( a_ = 0 ) { _() {} }

/* a global destructor without a matching constructor (the mirror direction). */
//-EXPECT-ERROR: requires a matching constructor
//global orphan(a_ = 0) { ~() {} }

/* a global group ctor/dtor takes no parameters. */
//-EXPECT-ERROR: takes no parameters
//global withparam(a_ = 0) { _(x) {} ~() {} }

/* a global group body holds only the ctor/dtor — no other members. */
//-EXPECT-ERROR: holds only the constructor
//global badbody(a_ = 0) { int junk_ = 0; }

/* duplicate constructor / duplicate destructor within one group. */
//-EXPECT-ERROR: Duplicate constructor
//global twoctor(a_ = 0) { _() {} _() {} ~() {} }
//-EXPECT-ERROR: Duplicate destructor
//global twodtor(a_ = 0) { _() {} ~() {} ~() {} }

/* an intra-file global name collision (here re-declaring `shots_`). */
//-EXPECT-ERROR: Duplicate declaration
//global int shots_ = 99;

/* a global's scalar initializer must fit its type. */
//-EXPECT-ERROR: does not fit
//global int8 toobig_ = 999;

/* a global's scalar initializer must be a constant — `shots_` is a global (never
   substituted), so `shots_ + 1` is not constant-foldable. */
//-EXPECT-ERROR: is not a constant expression
//global int nc_ = shots_ + 1;

/* a function-internal global is invisible outside its body: a function is not a
   namespace, so another body cannot qualify into it. */
//-EXPECT-ERROR: is not a namespace
//void withstatic() { global int s_ = 0; s_ = s_ + 1; }
//void reachit() { withstatic:s_ = 5; }
