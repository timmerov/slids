/*
test pre/post-increment/decrement.
aka ppid.

expressions are composed of phrases.
pre/post-increment/decrement operations are extracted from their phrase.
pre-increment/decrement is evaluated before the phrase.
post-increment/decrement is evaluated after the phrase.

what is a phrase?

an expression statement is a phrase.
expressions statements may have sub-phrases.

every comma separated slot of a tuple clause is a separate phrase.
phrases are evaluated left to right.
this includes:
parameter to functions and methods,
for initialization tuple clause slot,
tuple literal slot,
tuple destructure slot,
if/while/for condition.

the rhs of && and || operations is a sub-phrase.
this ensures a short-circuited ppid is not evaluated.

Canonical examples:
x = p++^;             // p incremented AFTER x is assigned
x = (++p)^ + (++p)^;  // p incremented twice BEFORE the assignment
x = (cond && b++);    // b incremented iff cond is true
x = foo(a++, ++a);    // foo(1, 3) — args are separate phrases

desugaring does not duplicate ppid evaluation.
side effects are extracted from the lvalue.
p++^ += 1;  -->  p^ = p^ + 1; p++;
++(arr[k++]) = 5;  -->  ++arr[k]; arr[k] = 5; k++;
row[++k] += (10,20)  -->  ++k; row[k][0] += 10; row[k][1] += 20;
*/

int32 show2(int32 a, int32 b) {
    __println("show " + a + " " + b);
    return 0;
}

int32 main() {
    int32 a = 1;
    int32 x = a++;            // post: x reads old a, a bumps after
    __println("x= " + x + " a= " + a);

    int32 b = 1;
    int32 y = ++b;            // pre: b bumps first, y reads new b
    __println("y= " + y + " b= " + b);

    int32 c = 1;
    int32 z = (++c) + (++c);  // both pre: c bumps twice up front, reads see 3
    __println("z= " + z + " c= " + c);

    int32 d = 1;
    int32 w = d++ + d++;      // both post: reads see 1, two bumps after
    __println("w= " + w + " d= " + d);

    int32 s = 5;
    s = s++;                  // the statement is the phrase: store 5, bump after -> 6
    __println("s= " + s);

    int32 t = 5;
    t = t++ + t++;            // store 10, two post-bumps after -> 12
    __println("t= " + t);

    int32 e = 5;
    e--;                      // bare inc/dec statement
    __println("e= " + e);

    int32 g = 1;
    show2(g++, ++g);          // each arg is its own phrase -> show 1 3
    __println("g= " + g);

    int32 h = 0;
    bool ct = true;
    bool r1 = (ct && h++);    // rhs reached -> h bumps
    __println("h= " + h);
    __println("r1= " + r1);

    int32 i = 0;
    bool cf = false;
    bool r2 = (cf && i++);    // short-circuited -> i unchanged
    __println("i= " + i);
    __println("r2= " + r2);

    float32 f = 1.0;
    f++;                      // float scalar steps by 1.0
    __println("f= " + f);

    /* an inc on an ALIASED-type variable keeps the alias transparent: the inc
       expression is a binary OPERAND, so its OWN type drives the common-type rule.
       (A spelling round-trip would clobber 'Counter' to an unknown slid, which has
       no common type with int.) */
    alias Counter = int32;
    Counter cnt = 41;
    int32 sum = (cnt++) + 100;            // post-inc value 41 -> 141; cnt becomes 42
    __println("alias inc= " + sum);       // 141
    __println("alias var= " + cnt);       // 42

    //-EXPECT-ERROR: Constant 'K' cannot be incremented
    //const int32 K = 5;
    //K++;

    //-EXPECT-ERROR: Operator '++' is not defined on type 'bool'
    //bool bb = true;
    //bb++;

    //-EXPECT-ERROR: The operand of '++' must be a variable
    //int32 lv = 5++;

    //-EXPECT-ERROR: is a function, not a variable
    //main++;

    return 0;
}
