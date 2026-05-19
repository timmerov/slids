/*
develop alias syntax.

alias corresponds to c++ typedef and/or using.

syntax is pretty straightforward.

alias IntPtr = int^;
*/

alias IntPtr = int^;
alias IntHdl = IntPtr^;

Class(int x_ = 0) {
    void greet() {
        __println("Class:greet x=" + x_);
    }

    Hoisted(int y_ = 0) {
        void greet() {
            __println("Hoisted:greet y=" + y_);
        }
    }
}

alias ClassPtr = Class^;
alias ClassHdl = ClassPtr^;
alias Hoist = Class:Hoisted;

alias TuplePair = (char[], int);

Template<T>(T z_) {
    void greet() {
        __println("Template:greet z=" + z_);
    }
}

alias TemplateInt = Template<int>;

int32 main() {

    int x = 42;
    IntPtr iptr = ^x;
    IntHdl ihdl = ^iptr;
    __println("ihdl^^ = " + ihdl^^);

    Class cls(37);
    ClassPtr cptr = ^cls;
    ClassHdl chdl = ^cptr;
    chdl^^.greet();

    TuplePair tuple = (##name(x), x);
    __println("tuple=(" + tuple[0] + "," + tuple[1] + ")");

    Hoist hoist;
    hoist.greet();

    TemplateInt tpl;
    tpl.z_ = 24;
    tpl.greet();

    /* block-scoped aliases — declared inside a {} block, in scope until end of block. */
    alias Buf = int;
    Buf outer1 = 7;
    {
        alias Buf = (const char)[];   /* inner shadows the outer Buf only within this block */
        Buf inner = "shadowed";
        __println("inner=" + inner);
    }
    Buf outer2 = 11;                  /* outer Buf back in scope: int */
    __println("outer1=" + outer1 + " outer2=" + outer2);

    return 0;
}
