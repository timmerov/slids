/*
test the stringify macros.

    ##date      current date: Mmm DD YYYY
    ##time      current time: HH:MM:SS
    ##file      short filename
    ##line      stringified line number
    ##func      unmangled function name
    ##name(x)   stringified name of expression
    ##type(x)   stringified type of expression
    #x          desugars to 5-tuple.

#x desugars to a tuple with 5 elements.
these are the values at the site where #x is used.

    (##file, ##line, ##type(x), ##name(x), ^x)

macros and aliases.
##type of an type alias returns the underlying type.
##type of an function alias returns the underlying function name.

notes:
date and time are the time the .sl file is compiled.
not the date and time slidsc is compiled. v1 bug.

deferred:
##function_mangled
##pathname
##value(expr)
*/

int32 main() {

    // ##file — short filename, no directory path.
    __println(##file);                  // macros.sl

    // ##line — the line where the macro appears, as a string.
    __println(##line);                  // 34

    // ##func — the enclosing function's name, unmangled.
    __println(##func);                  // main

    // ##name(x) — reproduces the argument's lexed text verbatim. It is NOT
    // parsed and NOT type-checked: whitespace is dropped and the names need
    // not exist. So a complex postfix expression round-trips as-is.
    __println(##name(a + b));           // a+b
    __println(##name(obj.field[2]));    // obj.field[2]

    // ##type(x) — the operand's inferred type. The operand IS a real,
    // resolved expression.
    int   n    = 7;
    int64 big  = 7;
    bool  flag = true;
    __println(##type(n));               // int
    __println(##type(big));             // int64
    __println(##type(flag));            // bool
    __println(##type(n + 1));           // int

    // ##name reproduces a real local's spelling without reading it; the value
    // read happens through ##type above, so `n` is not flagged unused.
    __println(##name(n + 1));           // n+1

    // ##date / ##time — the moment THIS file is compiled (not slidsc's build
    // time). Their values are the compile timestamp, so they are not pinned in
    // macros.exp; here we confirm only that each lowers to a usable char[].
    char[] today = ##date;
    char[] now   = ##time;
    __println(##type(today));           // char[]
    __println(##type(now));             // char[]

    alias Integer = int;
    alias Whole   = Integer;     // chained alias -> fully resolved
    enum Status ( ok );
    __println("Integer type = " + ##type(Integer));   // int  (alias name -> underlying)
    __println("Whole type = " + ##type(Whole));        // int  (chain resolved)
    __println("Status type = " + ##type(Status));      // int  (enum name -> underlying)

    // a namespace-qualified type name resolves the same way; a qualified VALUE
    // keeps the value path (its const-qualified type).
    Geo { enum Dir ( kN ); const int kSize = 3; }
    __println("Geo:Dir type = " + ##type(Geo:Dir));    // int        (qualified enum type)
    __println("Geo:kSize type = " + ##type(Geo:kSize)); // const int  (qualified value)

    return 0;
}

/*
##type of a bare name that is neither a value nor a type (alias/enum) is rejected.
*/

//-EXPECT-ERROR: 'Splat' is not a value or an alias
//int32 neg_undefined() {
//    __println(##type(Splat));
//    return 0;
//}

//-EXPECT-ERROR: 'Ns' is a namespace, not a value or an alias
//Ns { const int kX = 1; }
//int32 neg_namespace() {
//    __println(##type(Ns));
//    return 0;
//}
