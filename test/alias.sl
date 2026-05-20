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

/* file-scope consts for the inferred-decl const-on-copy test in main. */
const float64 kc_a = 3.0;
const float64 kc_b = 1.0;

/*
template aliases — `alias Name<T1,...> = TypeExpr;`. The body keeps each
type-parameter identifier literal; the use site `Name<argT1,...>` substitutes
each identifier with its arg. File-scope template aliases propagate through
.slh imports (parser-side stack + Program field).
*/

alias Ptr<T> = T^;
alias Pair<A, B> = (A, B);
alias Wrap<T> = (char[], T^);

//-EXPECT-ERROR: is repeated in the alias template parameter list
//alias DupParam<T, T> = T^;

//-EXPECT-ERROR: is already declared in the same scope
//alias Ptr<U> = U[];

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

    /* template-alias use sites. Ptr<T> resolves T to the parsed argument,
       so Ptr<int> ≡ int^. Multi-arg and tuple-body shapes too. */
    int tx = 73;
    Ptr<int> tip = ^tx;
    __println("tip^=" + tip^);

    Pair<int, int> pr = (99, 137);
    __println("pr=(" + pr[0] + "," + pr[1] + ")");

    int wx = 13;
    Wrap<int> w = (##name(wx), ^wx);
    __println("w=(" + w[0] + "," + w[1]^ + ")");

    /* block-scoped template alias — visible only within this block. */
    {
        alias Box<T> = T;
        Box<int> qq = 17;
        __println("qq=" + qq);
    }

    /* inferred-decl from a const rhs: a copy yields a mutable lhs. The
       explicit form keeps whatever the author wrote. */
    copied = kc_a - kc_b;
    __println("copied " + ##type(copied));
    const float64 explicit_c = kc_a - kc_b;
    __println("explicit_c " + ##type(explicit_c));

    //-EXPECT-ERROR: requires type arguments
    //Ptr bare;

    //-EXPECT-ERROR: expects 1 type argument
    //Ptr<int, char[]> wrong = ^tx;

    return 0;
}
