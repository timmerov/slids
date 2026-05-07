/*
test vexing parses for runs of ^.

rules:
  ^      single ^ is dereference, bitwise xor, address of.
  ^=     single ^ equals is bitwise xor compound assignment.
  ^^     double ^^ is binary logical xor, double dereference.
  ^^=    double ^^ equals is logical xor compound assignment.
  ^^^    run of 3+ ^ is a sequence of dereferences.

ambiguities are resolved by context.

lexer is greedy:
  ^^^= is always ^^^ =, never ^^ ^=, never ^ ^^=.
  the author must use whitespace to indicate intent.

negative cases sit before `return 0;` so slidsc actually codegens them.
the runner uncomments one at a time; positive cases above stay live.
*/

int32 main() {

    /* infix ^^ as logical xor */
    bool a = true;
    bool b = false;
    bool c = a ^^ b;
    __println("c = a ^^ b = " + c);

    /* type-suffix runs: ^, ^^, ^^^, ^^^^ */
    bool x = true;
    bool^ ptr = ^x;
    bool^^ hdl = ^ptr;
    bool^^^ hhh = ^hdl;
    bool^^^^ qqq = ^hhh;

    /* postfix ^^ as lvalue */
    hdl^^ = x;
    __println("hdl^^ as lvalue = " + x);

    /* postfix ^ then ^^= compound assign — `^` consumes greedily,
       `^^=` is one token */
    ptr^ ^^= x;

    /* postfix ^^ as rvalue followed by ; */
    bool y = hdl^^;
    __println("hdl^^ rvalue = " + y);

    /* postfix ^^ followed by ) */
    __println(hdl^^);

    /* postfix run-of-3: triple-deref of bool^^^ */
    bool z = hhh^^^;
    __println("hhh^^^ = " + z);

    /* postfix run-of-4: quad-deref of bool^^^^ */
    bool w = qqq^^^^;
    __println("qqq^^^^ = " + w);

    /* infix ^^ — operand-starter set: parenthesized, keyword literal,
       prefix-! */
    bool d = a ^^ (b);
    bool e = a ^^ true;
    bool f = a ^^ !b;
    __println("d e f = " + d + " " + e + " " + f);

    /* negative cases — uncommented one at a time by run_negatives.sh */

    //-EXPECT-ERROR: Expected expression, got '^^'
    //bool yy = ^^x;

    //-EXPECT-ERROR: Cannot dereference 'a' of type 'bool'
    //bool yy = a ^^ ^x;

    //-EXPECT-ERROR: arithmetic on references is not allowed
    //ptr ^^= x;

    //-EXPECT-ERROR: Cannot dereference a value of type 'bool'
    //ptr^^^= x;

    //-EXPECT-ERROR: arithmetic on references is not allowed
    //hdl^^= x;

    return 0;
}
