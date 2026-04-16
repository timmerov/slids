/*
new rules:

no more naked op overloads.
op overloads must be in the class definition.
the return value of _ and all op overloads is self.
that's implied.
we don't need it to be part of the syntax.
as per these examples.

assignment is a statement, not an expression.
x = y = 0;
is not allowed.
if (x = 0) {}
is not allowed.

type conversion syntax:
s = (String="x=") + x;
and yes i know i'm overloading the meaning of =.
but not by much.
the parentheses are necessary because = has lower
precedence than +.
otherwise we'd have:
s = String=("x=" + x);
and this will do VERY bad things.
String="x="
means create an unnamed temp object of type String
with value "x=".
which is a heck of a lot like an assignment.
except it's a valid expression.

note:
to be noted, not implemented now. maybe in the future.
type conversion implies we should change the syntax for
built-in types.
int=3.14;
(float=5) / 100.0;
let me cogitate on that.
i like the pointer casting syntax.

note:
to be noted, not implemented now. maybe in the future.
forbid shadowing type names.
String String = "bad idea.";

temporary variables are interchangeable
- that's probably not the right word.
in other words:
temp2 = temp1 + int
can be replaced with:
temp1 += int
and often should be.
sometimes creating and destroying a temp is expensive.
for example, the new and delete calls in String.
when the compiler has code generation options...
it should choose the path that uses the fewest temp
instantiations of non-built-in classes.

during assignments, the declaree can assume the
identity of a temporary variable.
Value a = b + c;
evaluating b + c creates a temporary Value.
Value a should be able to assume the identity of the
temporary without copy or move semantics.

advanced feature:
to be noted, not implemented now. maybe in the future.
we would like to be able to re-use temporary objects.
if a temporary String is created to evaluate an expression...
and another expression in the same scope needs a temporary String,
then the first String can be __reset and used.
this means the scope of a temporary object goes beyond the scope
of the expression where it's used.
probably extends to the end of the current code block.
i'm trying to speed up string manipulations here.
if a temp String has memory allocated for its storage, it would be
silly to delete that memory and allocate new memory for a new temp String.
let's assume each statement requires a temp String.
a = b + c;
d = e + f;
both statements should be able to use the same temp String.
which will be destructed at the end of the code block.

ensure slids_reference.md is up to date.
*/

/*
ValueBinary defines:
a binary Value + Value operator.
and an = int operator.
so to do:
    Value + int
the int must be assigned to a Value
    Value + Value
then we can do the binary +.
*/
ValueBinary(
    int value_ = 0
) {
    _() {}
    ~() {}

    op=(ValueBinary^ rhs) {
        value_ = rhs^.value_;
        __println("assignment to ValueBinary " + value_);
    }

    op=(int x) {
        value_ = x;
        __println("assignment to int " + value_);
    }

    op+(ValueBinary^ a, ValueBinary^ b) {
        value_ = a^.value_ + b^.value_;
        __println("binary addition " + value_);
    }
}

/*
ValuePlusEquals defines:
    = int
    += value
    += int
so to evaluate:
    temp Value + int
the += rule is invoked directly.
the result and the lhs are the same object.
to evalue:
    named Value + int
must do:
    temp Value = named Value
    temp Value += int
*/
ValuePlusEquals(
    int value_ = 0
) {
    _() {}
    ~() {}

    op=(ValuePlusEquals^ rhs) {
        value_ = rhs^.value_;
        __println("assignment to ValueBinary " + value_);
    }

    op=(int x) {
        value_ = x;
        __println("assignment to int " + value_);
    }

    op+=(ValuePlusEquals^ rhs) {
        value_ += rhs^.value_;
        __println("plus equals " + value_);
    }

    op+=(int x) {
        value_ += x;
        __println("plus equals " + value_);
    }
}

int32 main() {
    /* assign to int. */
    ValueBinary v0 = 10;
    /* assign to Value. */
    ValueBinary v1 = v0;
    /* binary addition. */
    /*
    not sure what happens here.
    naively, there should be a temp Value
    holding the result of a+b.
    which then gets copied to v2.
    otoh, an optimization would be to
    skip the assignment and store a+b
    directly in v2.
    otgh, the declaration assignment to a temp
    of the same type could steal the identity of the temp.
    no copy needed.
    */
    ValueBinary v2 = v0 + v1;

    /*
    create a temp. assign int 20.
    create a temp. binary addition = 20.
    create a temp. assign int 30.
    create a temp. binary addition = 50.
    assign value 50.
    */
    ValueBinary v3 = ValueBinary + 20 + 30;

    /*
    create a temp.
    plus equals 40.
    plus equals 90.
    assign value 90.
    */
    ValuePlusEquals v4 = ValuePlusEquals + 40 + 50;

    return 0;
}
