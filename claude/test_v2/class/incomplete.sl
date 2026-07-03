/*
test completing incomplete classes.

a trailing ellipsis means the class field tuple is incomplete.
more fields may be added later.

    Class(int incomplete, ...) {
        void method1() { }
    }

the incomplete class can be re-opened with or without more fields.
the trailing ellipsis means the class is still incomplete.

    Class(...) {
        void method2() { }
    }
    Class(int still_incomplete, ...) {
        void method3() { }
    }

the incomplete class is completed when it is re-opened without
the trailing ellipsis with or without more fields.

    Class(int completed_optional) {
        void method4() { }
    }

the completed class can be re-opened.
no new fields may be added.

    Class() {
        void method5() { }
    }

the incomplete class slids must be in the same scope.
they are combined and treated as if they were a single slid.

notes:

the above demonstrates adding members to an incomplete class code body.
the technique applies to all types of members that can be declared in a
class code body - including: alias, constant, class, enum, global, method, etc.

the above applies to all code scopes including: file, namespace/class,
and all run-time scopes.
but excludes switch-body.

currently only single file is supported - the size and layout of the
class are known.
this is not true for incomplete classes that span multiple files.

multi-file notes:

the header file declares the incomplete class.
the private fields are hidden from the public api.

header.slh:
    String(...) { /*api*/ }

the library file completes the class and implements the api.
only one source file completes the class.
the library file determines the size of the class.
and either exports the size directly or exports a function that
returns the size.
the library file exports a ctor for the class that initializes
the private fields and calls the private ctor (pctor).
the library file instantiates a class object more/less normally.
all fields initialized normally.
then it calls pctor instead of ctor.

string.sl:
    String(intptr size_, intptr cap_, char[] ptr_) {
        /*implementation*/
    }
    String s;

the user file may re-open the incomplete class to add local api.
the user file does not complete the class.
otherwise there will be link errors.
the user file follows a modified procedure for instantiating
an incomplete class.
the size of the class arrives at link time.
space is allocated.
public fields are initialized as per normal.
then the external ctor is called.

main.sl:
    import string;
    String(...) {
        /*author api*/
    }
    String s;

from the above we can observe the class does not need to be
completed in a source file.
*/

/*
claude says:

IMPLEMENTED single-file, reusing the same-scope RE-OPEN machinery
([[project_reopen_classes]]) with ONE stateful change. A trailing `...` in a class
field tuple sets Node.is_incomplete (grammar.cpp parseParamList, class field lists
only, ellipsis must be LAST — no leading/interior form). resolve's "a re-open adds no
fields" rule is now gated on ClassInfo.is_open: while OPEN a re-open APPENDS its fields
(moved onto info.pending_fields); a re-open whose tuple omits `...` CLOSES the class.
registerClassBody interns the primary's own fields THEN the pending ones through one
shared addField funnel, so the layout freezes in exactly ONE place. The type intern was
already deferred (a slotless handle re-interned with slots in Phase 2), and single-file
every re-open is seen before that freeze. classify / desugar / codegen are UNTOUCHED:
once interned it is an ordinary class — construction, field access, and sizeof all reuse
existing paths. No privacy single-file (every field is visible + positionally
initializable); defaults optional on every field; sizeof(Class) is the runtime
__$sizeof() call (misc/sizeof.sl); an empty completed class is 1 byte (the {i8} rule).

Multi-file (future) rides the same model: a TU that never sees the close keeps the type
open -> size deferred to link-time __$sizeof(). is_open / public-prefix are the seams.
*/

/* --- the running spec example: one class grown across five re-opens --- */

Class(int incomplete = 1, ...) {           /* declare INCOMPLETE: 1 field + `...` */
    void method1() { __println("method1"); }
}
Class(...) {                               /* re-open, no new fields, still incomplete */
    void method2() { __println("method2"); }
}
Class(int still_incomplete = 2, ...) {     /* append a field, still incomplete */
    void method3() { __println("method3"); }
}
Class(int completed_optional = 3) {        /* no `...` -> COMPLETE the class */
    void method4() { __println("method4"); }
}
Class() {                                  /* re-open a COMPLETED class: no new fields */
    void method5() { __println("method5"); }
}

/* --- an incomplete class WITH a lifecycle: the ctor is declared in the FIRST opening
   yet references `extra`, a field the COMPLETING opening adds — the openings combine
   into a single slid, so the earlier body sees the later field. --- */

Gadget(int id = 0, ...) {
    _() { __println("Gadget:ctor " + id + "/" + extra); }
    ~() { __println("Gadget:dtor " + id); }
}
Gadget(int extra = 7) {
    void show() { __println("Gadget " + id + "/" + extra); }
}

/* --- an empty class completed via `(...)` then `()`: 1 byte. --- */

Empty(...) { }
Empty() { }

/* --- incompleteness nests like any class: a NAMESPACE member and a FUNCTION-body
   local, each grown then completed in its own scope. --- */

Space {
    Widget(int w = 4, ...) { void w1() { __println("w1 w=" + w); } }
    Widget(int h = 5)      { void w2() { __println("w2 h=" + h); } }
}

void locals() {
    Local(int x = 9, ...) { void l1() { __println("l1 x=" + x); } }
    Local(int y = 8)      { void l2() { __println("l2 y=" + y); } }
    Local lo;
    lo.l1();
    lo.l2();
    __println("local=" + lo.x + "," + lo.y);
}

/* --- APPENDED-FIELD variety: a typeless (inferred) field, a defaultless field
   (zero-inits), and a field whose default names a const — all added by re-opens, so
   they exercise the pending-field path (type inference + default resolution with the
   class frame open). --- */

const int kSeed = 100;
Infer(int a = 1, ...) { }
Infer(b = 2, ...)     { }          /* typeless appended -> inferred int */
Infer(int c, ...)     { }          /* defaultless appended -> zero-inits */
Infer(int d = kSeed + 5) { }       /* default names a const; completes -> 105 */

/* --- an appended CLASS-typed field default-constructs (its ctor/dtor fire through
   the appended-field path). --- */

Cell(int v = 0) {
    _() { __println("Cell:ctor " + v); }
    ~() { __println("Cell:dtor " + v); }
}
Holder(int id = 0, ...) { }
Holder(Cell cell) { void show() { __println("holder " + id + " cell=" + cell.v); } }

/* --- a VIRTUAL incomplete class (the re-open's `_$vptr` is skipped when appending). --- */

Shape(int tag = 0, ...) { virtual void draw() { __println("draw tag=" + tag); } }
Shape(int extra = 9)    { void more() { __println("more " + extra); } }

/* --- non-field code-body members (const + enum) added across openings, alongside the
   field growth. --- */

Members(int a = 1, ...) { const int kK = 7; }
Members(int b = 2) {
    enum E ( kZero, kOne );
    void show() { __println("members " + a + "/" + b + " k=" + kK + " e=" + E:kOne); }
}

/* --- an incomplete DERIVED class: the base is field 0, own incomplete fields follow. --- */

Base(int b = 1) { void bm() { __println("bm " + b); } }
Base : Der(int d = 2, ...) { }
Base : Der(int e = 3)      { }

int32 main() {
    /* default construction fills every field from its default */
    Class c;
    c.method1();
    c.method2();
    c.method3();
    c.method4();
    c.method5();
    __println("defaults=" + c.incomplete + "," + c.still_incomplete + "," + c.completed_optional);

    /* full positional init (single-file has no privacy: every field is initializable) */
    Class cf(10, 20, 30);
    __println("full=" + cf.incomplete + "," + cf.still_incomplete + "," + cf.completed_optional);

    /* sizeof is the runtime __$sizeof() (three int fields) */
    __println("sizeof(Class)=" + sizeof(Class));

    /* a lifecycle incomplete class: the first opening's ctor sees the later field;
       the dtor fires at this block's exit */
    {
        Gadget gd(5);
        gd.show();
    }

    /* an empty completed incomplete class is 1 byte */
    __println("sizeof(Empty)=" + sizeof(Empty));

    /* namespace- and function-scoped incomplete classes */
    Space:Widget sw;
    sw.w1();
    sw.w2();
    locals();

    /* partial positional (first field set, rest default) and the `=` init-list form */
    Class cp(10);
    __println("partial=" + cp.incomplete + "," + cp.still_incomplete + "," + cp.completed_optional);
    Class ce = (11, 22, 33);
    __println("initlist=" + ce.incomplete + "," + ce.still_incomplete + "," + ce.completed_optional);

    /* appended fields: typeless (inferred), defaultless (zero-init), default-from-const */
    Infer inf;
    __println("infer=" + inf.a + "," + inf.b + "," + inf.c + "," + inf.d);

    /* an appended class-typed field default-constructs; its dtor fires at block exit */
    {
        Holder h;
        h.show();
    }

    /* a virtual incomplete class */
    Shape s;
    s.draw();
    s.more();

    /* const + qualified-enum members added across openings */
    Members m;
    m.show();

    /* an incomplete derived class: base method + own appended fields */
    Der der;
    der.bm();
    __println("der=" + der.d + "," + der.e);

    return 0;
}

/* --- file-scope NEGATIVES (each uncommented one at a time by the negative runner) --- */

/* a COMPLETE class adds no fields on re-open. */
//-EXPECT-ERROR: cannot add fields
//Done(int a = 1) { }
//Done(int b = 2) { }

/* a COMPLETE class cannot be re-opened as incomplete. */
//-EXPECT-ERROR: already complete
//Shut(int a = 1) { }
//Shut(...) { }

/* `...` is trailing-only — v2 has no leading form (unlike v1). */
//-EXPECT-ERROR: must be the last item
//Lead(..., int a = 1) { }

/* `...` cannot sit mid-list either. */
//-EXPECT-ERROR: must be the last item
//Mid(int a = 1, ..., int b = 2) { }

/* a field appended across openings collides with an earlier field. */
//-EXPECT-ERROR: Duplicate field
//Dup(int a = 1, ...) { }
//Dup(int a = 2) { }

/* a typeless appended field with no default has nothing to infer from. */
//-EXPECT-ERROR: needs an explicit type
//Untyped(int a = 1, ...) { }
//Untyped(b) { }
