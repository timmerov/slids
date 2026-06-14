/*
the bug of the day.
*/

/* under no circumstances should this compile. */
int foo( (int,int) tpl ) {
   return tpl[0] + tpl[1];
}

int32 main() {

    tpl = (1,2);
    x = foo(tpl);
    __println(x);

    return 0;
}
