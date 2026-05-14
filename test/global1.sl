/*
develop globals.

keyword 'global'.

the long form is slids-like syntax.
keyword optional-name declaration-tuple code-block
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

like namespaces, globals are accessed using the name of
the global namespace colon global variable name.

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
but the author may make it eager for globals where order
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
        /* reach goal: compile error */
        // parts:wheels_ = 37;

        before_globals();
        {
            global;
            /* ctor is called */
            parts:doors_ = 17;

            /* reach goal: compile error: double instantiation. */
            // global;
        }
        freed_globals();

        /* reach goal: compile error */
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
instantiate global, then the compile with automagically
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

/* static-allocated global - no ctor/dtor */
global simple(
    garage_ = false
) { }

/* two declarations stacking in the parts namespace */
global parts(
    wheels_ = 4,
    doors_ = 2
) { }

global parts(
    color_ = 7
) { }

/* lazy global - ctor runs on first access */
global lazy(
    counter_ = 0
) {
    _() {
        counter_ = 100;
        __println("lazy:ctor");
    }
    ~() {
        counter_ = 0;
        __println("lazy:dtor");
    }
}

/* short-form bare decl at file scope - anonymous namespace */
int x_ = 42;
global int y_ = 37;

/* class with internal globals */
Box() {
    global (count_ = 5) { }
    global lid(open_ = false) { }
    _() {
        __println("Box:ctor");
    }
    ~() {
        __println("Box:dtor");
    }
}

/* function-internal global - persists across calls */
void foo() {
    global int fcount_ = 0;
    fcount_ = fcount_ + 1;
    __println("foo fcount: " + fcount_);
}

/* class with a lazy internal namespace global - exercises sentinel gate + */
/* ctor/dtor when the lazy slid lives under a class's namespace */
Crate() {
    global cargo(weight_ = 0) {
        _() { __println("[cargo ctor]"); }
        ~() { __println("[cargo dtor]"); }
    }
    _() { }
    ~() { }
}

/* function with a long-form internal global - anonymous, so the namespace */
/* is just `taggify` and bare `count_` resolves to taggify:count_ */
void taggify() {
    global (
        count_ = 0
    ) {
        _() { __println("[taggify ctor]"); }
        ~() { __println("[taggify dtor]"); }
    }
    count_ = count_ + 1;
    __println("tag #" + count_);
}

/* method with a function-internal global - namespace is `Logger:note`, */
/* visible only inside that method body */
Logger() {
    _() { __println("[Logger ctor]"); }
    ~() { __println("[Logger dtor]"); }
    void note(int n) {
        global int seen_ = 0;
        seen_ = seen_ + n;
        __println("logger seen: " + seen_);
    }
}

/* reverse-construction-order: pool's ctor touches buffer, so buffer */
/* registers after pool. dtor_all walks LIFO -> buffer dtor fires before */
/* pool dtor, i.e. pool outlives buffer. */
global pool(
    items_ = 0
) {
    _() {
        __println("[pool ctor]");
        buffer:slots_ = 16;
    }
    ~() { __println("[pool dtor]"); }
}

global buffer(
    slots_ = 0
) {
    _() { __println("[buffer ctor]"); }
    ~() { __println("[buffer dtor]"); }
}

/*
remaining positive tests:
global type inferenece
*/

int32 main() {
    global;

    /* static globals */
    __println("garage: " + simple:garage_);
    simple:garage_ = true;
    __println("garage: " + simple:garage_);

    /* stacked-namespace globals */
    __println("doors: " + parts:doors_);
    __println("color: " + parts:color_);

    /* lazy global - access triggers ctor, prints 100 */
    __println("lazy: " + lazy:counter_);

    /* class-internal globals */
    __println("box count: " + Box:count_);
    __println("box lid open: " + Box:lid:open_);

    /* shadowing - bare x_ is local, ::x_ is the unnamed-namespace global */
    int x_ = 37;
    __println("local x_: " + x_);
    __println("global x_: " + ::x_);

    /* function-internal global */
    foo();
    foo();

    /* class-internal lazy global - first access triggers cargo ctor */
    Crate:cargo:weight_ = 7;
    __println("cargo weight: " + Crate:cargo:weight_);

    /* function-internal long-form global - tagger ctor on first taggify */
    taggify();
    taggify();

    /* method-internal global - persists across method calls on Logger */
    Logger lg;
    lg.note(3);
    lg.note(5);

    /* reverse-construction-order: touching pool runs its ctor, which */
    /* touches buffer mid-construction. expected dtor order at scope exit: */
    /* buffer dtor first, then pool dtor. */
    pool:items_ = 1;
    __println("buffer slots: " + buffer:slots_);

    return 0;
}

/* === decorated negative tests === */
/* each marker is followed by a //-block. the runner uncomments one block */
/* at a time and asserts the substring appears in slidsc's stderr. */

/* field-name collision in a stacked namespace: parts already has wheels_. */
//-EXPECT-ERROR: redeclares field
//global parts(
//    wheels_ = 99
//) { }

/* field-name collision in the unnamed namespace: x_ already declared above. */
//-EXPECT-ERROR: redeclares field
//global int x_ = 9;

/* ctor without dtor violates the pair rule. */
//-EXPECT-ERROR: ctor but no dtor
//global lonely(
//    a_ = 0
//) {
//    _() { }
//}

/* dtor without ctor violates the pair rule. */
//-EXPECT-ERROR: dtor but no ctor
//global solo(
//    a_ = 0
//) {
//    ~() { }
//}

/* function-local globals are not visible from outside the function. */
//-EXPECT-ERROR: not visible outside function
//int32 leaky() {
//    foo:fcount_ = 99;
//    return 0;
//}

/* :: must name something declared in the unnamed namespace. */
//-EXPECT-ERROR: not declared in the unnamed
//int32 stray() {
//    return ::nope_;
//}

/* global field initializers must be foldable constants. */
//-EXPECT-ERROR: not a foldable constant
//int32 not_fold() {
//    return 7;
//}
//global bad(
//    a_ = not_fold()
//) { }

/* global; is only allowed in main. */
//-EXPECT-ERROR: only allowed in `main`
//int32 elsewhere() {
//    global;
//    return 0;
//}

/* reach-goal negatives that require their own main are in global_reach.sl: */
/*   - double `global;` in main */
/*   - access in main outside the `global;` scope */
