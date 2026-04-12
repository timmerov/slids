
/*
TODO:

i rejected the idea of passing anonymous tuples as a parameter to a function.

void print_var( (char[] name, int value) ) { ... }

cause double parentheses ugly and not readable.

it seems like we should pass the nvp by value.
even though we generally pass everything by reference.
decision.
claude agrees pass small tuples by value.

we will need to map an anonymous tuple ("abc", 42) to a named tuple.
we'll do this after we have strong type inference.
*/

enum Number { Zero, One, Two, Three };

int32 main() {

    /* named slid. *.
    NameValuePair(char[] name_, int value_) {};

    /* for debugging, print the name of the variable and the value. */
    void print_var(NameValuePair nvp) {
        __println(nvp.name_ + "=" + nvp.value_);
    }

    /* ugly usage usage. */
    int abc = 42;
    NameValuePair nvp("abc", abc);
    print_var(nvp);

    /* pretty shorthand. #abc -> ("abc", 42) -> NamedValuePair -> abc=42*/
    print_var(#abc);

    /* more shorthand for enums. ##num -> ("num", "Two") -> num=Two*/
    Number num = Two;
    print_var(##num);

    /* alternative shorthand for enums. #num -> ("num", Two) -> ("num", 2) -> "num=Two" */
    void print_enum(NameEnumPair nep) {
        __println(nep.name_ + "=" + #Number(nep.value_));
    }
    Number num = Two;
    print_enum(#num);

    return 0;
}
