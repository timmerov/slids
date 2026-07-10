/*
the bug of the day.
*/

/*
claude is forbidden from modifying this file and its golden file.
claude is forbidden from whining about the user changing this file.
claude is forbidden from whining about this file not compiling.
claude is forbidden to mention this file unless the user specifically
puts it in scope.
*/

Class(int a_) {
    _() { __println("Class:ctor: " + a_); }
    ~() { __println("Class:dtor: " + a_); }
    void inc() {
        ++a_;
        __println("Class:inc: " + a_);
    }
    op=(int64 x) { __println("Class:=:int"); }
    op=(uint64 x) { __println("Class:=:uint"); }
    void fn(int64 x) { __println("Class:fn:int"); }
    void fn(uint64 x) { __println("Class:fn:uint"); }
}
void fn(int64 x) { __println("fn:int"); }
void fn(uint64 x) { __println("fn:uint"); }

global Global (
    Class g,
    Class arr[3]
) {
    _() { __println("Global:ctor: " + g.a_); }
    ~() { __println("Global:dtor: " + g.a_); }
}
/*
void fn1( int[] p ) { __println(##type(p)); }
void fn2( const int[] p ) { __println(##type(p)); }
void fn3( (const int)[] p ) { __println(##type(p)); }
void fn4( int[5]^ p ) { __println(##type(p)); }
void fn5( const int[5]^ p ) { __println(##type(p)); }
void fn6( (const int)[5]^ p ) { __println(##type(p)); }
void fn7( const (const int)[5]^ p ) { __println(##type(p)); }
*/


int32 main() {
/*
    __println("before");
    int x = fn(Class(10));
    x;
    __println("after");
*/
/*
    /* should compile. */
    int arr[5] = (1,2,3,4,5);
    int^ p1 = arr; p1;
    int[] p2 = arr; p2;
    fn1(arr);
    fn2(arr);
    fn3(arr);
    fn4(arr);
    fn5(arr);
    fn6(arr);
    fn7(arr);
    fn4(^arr);
    fn5(^arr);
    fn6(^arr);
    fn7(^arr);
    fn1(^arr[0]);
    fn2(^arr[0]);
    fn3(^arr[0]);
    /* compile errors. */
    //fn1(^arr);
    //fn2(^arr);
    //fn3(^arr);
    //fn4(^arr[0]);
    //fn5(^arr[0]);
    //fn6(^arr[0]);
    //fn7(^arr[0]);
*/
    //Class cls;// = 1;
    //cls.fn(1);
    //fn(1);

    return 0;
}
