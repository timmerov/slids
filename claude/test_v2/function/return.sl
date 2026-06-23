/*
test returning non-primitive values -
arrays, tuples, classes.

function definition:

    Type fn() {
        Type obj;
        return obj;
    }

which desugars to:

    void fn_$desugar(Type^ ret) {
        alias obj = ret^;
        /* if class-like object */
        initialize obj;
        obj.ctor();
        /* fn body */
        /* obj dtor is never called in the function. */
    }

case 1: copy to an existing destination.
this is the fallback position.
in all cases, the calling site builds and tears down the object.

    Type obj;
    obj = fn();

the copy case desugars to:

    /* obj init, ctor, dtor called normally. */
    Type obj;
    {
        alloca $temp;
        fn_$desugar(^$temp);
        obj <-- $temp;
        $temp.dtor();
    }

case 2: build a new local variable in place.
the local variable type must match the return type exactly enough.
ie no widening or type conversion.
otherwise fall back to case 1.
in this case, the object is built within the function.
but is torn down by the calling site.

    Type obj = fn();

the build new case desugars to:

    alloca obj;
    /* obj init and ctor are called by the function. */
    fn_$desugar(^obj);
    /* obj dtor is called at end of scope. */

case 3: overwrite an existing target.
the target must be plain old data.
the target type must match the return type exactly enough.
ie no widening or type conversion.
some classes may qualify as plain old data.
i don't know what the class-is-pod criteria is.
for now let's limit to arrays and tuples with no leaf classes.

    Type pod;
    pod = fn();

desugars to:

    /* plain-old-data has no init, ctor, dtor. */
    Type pod;
    fn_$desugar(^pod);

in all cases, the desugared function is the same.
the only difference is how the call site handles the returned value.

pitfalls i think we need to avoid.
the return value does not need to be unique in the function.
but multiple return targets cannot overlap in the same scope.

    Class good() {
        if (cond) {
            Class a(1,2);
            return a;
        } else {
            Class b(3,4);
            return b;
        }
    }

    Class bad() {
        Class a(1,2);
        Class b(3,4);
        if (cond) {
            return a;
        } else {
            return b;
        }
    }

    Class bad() {
        Class a(1,2);
        if (cond) {
            return a;
        } else {
            Class b(3,4);
            return b;
        }
    }

notes:

what is the plain-old-data criteria for classes?
no move or assignment operators?

signatures to test with both variable declaration and existing variable.

    int[3] fn();
    (int,int) fn();
    (int[3], int[3]) fn();
    (int, int)[3] fn();

for classes, test only the in-place case.
the copy case is blocked by move/copy semantics for classes.

    Class fn();
    Class[3] fn();
    (Class, Class) fn();
*/

/*
claude says:

tbd
*/

int32 main() {

    return 0;
}
