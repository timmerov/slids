/*
test classes defined in local (nested) scopes.

classes may be defined in any code body.
shadowing of a class name is allowed.
the shadowed class might be no longer reachable.
*/

/*
claude says:

a class may be defined in any function body, and in any nested block within one
(if / else / loop / switch case). each scope registers its own classes before
resolving its statements, so a use may precede the definition in that scope. a
local class is lexically scoped — its name and members live in the block's frame
and drop at scope exit. defining one shadows any same-named class in an enclosing
scope, local or file-scope (innermost wins); the shadowed class becomes
unreachable — there is no syntax to name it (`::` starts at GLOBAL scope, where a
local class isn't; see todo.txt).

two same-named local classes (main's Function vs shadow's Function) are kept
distinct by their defining-scope id (def_id), NOT by mangling the name: the name
stays bare everywhere a human or a map sees it (diagnostics, ##type), and only the
ctor/dtor/sizeof LLVM symbols are disambiguated, minted from the id at codegen. a
local class is a FULL class — members (alias/const/enum, qualified by its name), a
hook-class field whose ctor/dtor run, sizeof, new/delete — and its ctor/dtor lift
to module-level functions exactly like a file-scope class's.
*/

/* a file-scope class, shadowed by a same-named LOCAL class in main. */
Shadowed(int x_) {
    _() { __println("FILE:Shadowed: " + x_); }
    ~() { }
}

int32 main() {

    Function obj(10);
    foo();
    shadow();

    Function(int x_ = -1) {
        _() {
            __println("main:Function:ctor: " + x_);
        }
        ~() {
            __println("main:Function:dtor: " + x_);
        }
    }

    void foo() {
        NestedFunction obj(20);
        NestedFunction(int x_ = -1) {
            _() {
                __println("foo:NestedFunction:ctor: " + x_);
            }
            ~() {
                __println("foo:NestedFunction:dtor: " + x_);
            }
        }
    }

    void shadow() {
        Function obj(30);
        Function(int x_ = -1) {
            _() {
                __println("shadow:Function:ctor: " + x_);
            }
            ~() {
                __println("shadow:Function:dtor: " + x_);
            }
        }
    }

    {
        Block obj(40);
        Block(int x_ = -1) {
            _() {
                __println("main:Block:ctor: " + x_);
            }
            ~() {
                __println("main:Block:dtor: " + x_);
            }
        }
    }

    b = true;
    if (b) {
        If obj(50);
        If(int x_ = -1) {
            _() {
                __println("main:If:ctor: " + x_);
            }
            ~() {
                __println("main:If:dtor: " + x_);
            }
        }
    }
    if (!b) { } else {
        Else obj(60);
        Else(int x_ = -1) {
            _() {
                __println("main:Else:ctor: " + x_);
            }
            ~() {
                __println("main:Else:dtor: " + x_);
            }
        }
    }

    while (b) {
        While obj(70);
        While(int x_ = -1) {
            _() {
                __println("main:While:ctor: " + x_);
            }
            ~() {
                __println("main:While:dtor: " + x_);
            }
        }
        break;
    }

    while {
        WhilePost obj(80);
        WhilePost(int x_ = -1) {
            _() {
                __println("main:WhilePost:ctor: " + x_);
            }
            ~() {
                __println("main:WhilePost:dtor: " + x_);
            }
        }
        break;
    } (b);

    for (i : 0..1) {
        For obj(90);
        For(int x_ = -1) {
            _() {
                __println("main:For:ctor: " + x_);
            }
            ~() {
                __println("main:For:dtor: " + x_);
            }
        }
    }

    switch (37) {
    case 37:
        Switch obj(100);
        Switch(int x_ = -1) {
            _() {
                __println("main:Switch:ctor: " + x_);
            }
            ~() {
                __println("main:Switch:dtor: " + x_);
            }
        }
        break;
    }

    // transitive lifecycle: a local class with a hook-class field runs the
    // field's ctor/dtor (the field's hooks fire even though Outer has none).
    {
        Inner(int v_) {
            _() { __println("Inner:ctor: " + v_); }
            ~() { __println("Inner:dtor: " + v_); }
        }
        Outer(Inner i_) { }
        Outer o;
        __println("outer.i = " + o.i_.v_);
    }

    // a local class shadows a same-named FILE-SCOPE class (innermost wins; the
    // file-scope Shadowed is never reached).
    {
        Shadowed(int x_) {
            _() { __println("LOCAL:Shadowed: " + x_); }
            ~() { }
        }
        Shadowed s(8);
    }

    // a local class carries members (alias / const / enum), qualified by its name.
    {
        Geo(int x_) {
            alias Real = float;
            const Real kE = 2.0;
            enum int Dir (kN, kS);
        }
        Geo:Real r = 1.25;
        __println(##type(r) + " e=" + Geo:kE + " S=" + Geo:Dir:kS);
    }

    // sizeof + new / delete of a local class.
    {
        Heap(int x_) {
            _() { __println("Heap:ctor: " + x_); }
            ~() { __println("Heap:dtor: " + x_); }
        }
        __println("sizeof Heap = " + sizeof(Heap));
        Heap^ hp = new Heap(11);
        delete hp;
    }

    return 0;
}

/* compile errors. */

// two same-named classes in one body collide.
//-EXPECT-ERROR: Duplicate definition of class 'Dup'
//void dupbody() {
//    Dup(int x_) { _(){} ~(){} }
//    Dup(int y_) { _(){} ~(){} }
//}

// a local class is not a closure: its ctor/dtor cannot read an enclosing local.
//-EXPECT-ERROR: Unresolved identifier 'outer'
//void capbody() {
//    int outer = 7;
//    Cap(int x_) { _(){ __println("" + outer); } ~(){} }
//    Cap c(1);
//}
