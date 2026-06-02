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

notes:
date and time are the time the .sl file is compiled.
not the date and time slidsc is compiled. v1 bug.

deferred:
##function_mangled
##pathname
##value(expr)
*/

/*
claude says:

tbd
*/

int32 main() {

    return 0;
}
