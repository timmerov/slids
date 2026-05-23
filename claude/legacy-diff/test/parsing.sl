/*
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
    _() {
        __println("created ValueBinary");
    }
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
        __println("binary addition " + a^.value_ + " + " + b^.value_ + " = " + value_);
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
    _() {
        __println("created ValuePlusEquals");
    }
    ~() {}

    op=(ValuePlusEquals^ rhs) {
        value_ = rhs^.value_;
        __println("assignment to ValuePlusEquals " + value_);
    }

    op=(int x) {
        value_ = x;
        __println("assignment to int " + value_);
    }

    op+=(ValuePlusEquals^ rhs) {
        int left = value_;
        value_ += rhs^.value_;
        __println("plus equals ValuePlusEquals " + left + " += " + rhs^.value_ + " = " + value_);
    }

    op+=(int x) {
        int left = value_;
        value_ += x;
        __println("plus equals int " + left + " += " + x + " = " + value_);
    }
}

/*
ValueBoth defines both.
*/
ValueBoth(
    int value_ = 0
) {
    _() {
        __println("created ValueBoth");
    }
    ~() {}

    op=(ValueBoth^ rhs) {
        value_ = rhs^.value_;
        __println("assignment to ValueBoth " + value_);
    }

    op=(int x) {
        value_ = x;
        __println("assignment to int " + value_);
    }

    op+(ValueBoth^ a, ValueBoth^ b) {
        value_ = a^.value_ + b^.value_;
        __println("binary addition " + a^.value_ + " + " + b^.value_ + " = " + value_);
    }

    op+=(ValueBoth^ rhs) {
        int left = value_;
        value_ += rhs^.value_;
        __println("plus equals ValueBoth " + left + " += " + rhs^.value_ + " = " + value_);
    }

    op+=(int x) {
        int left = value_;
        value_ += x;
        __println("plus equals int " + left + " += " + x + " = " + value_);
    }
}

int32 main() {
    __println("part 1 assignments:");
    /* assign to int. */
    ValueBinary v0 = 10;
    /* assign to Value. */
    ValueBinary v1 = v0;

    __println("part 2 binary addition:");
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

    __println("part 3 explicit temporary in expression:");
    /*
    create a temp. assign int 20.
    create a temp. binary addition 0 + 20 = 20.
    create a temp. assign int 30.
    create a temp. binary addition = 50.
    assign value 50.
    */
    ValueBinary v3 = ValueBinary + 20 + 30;

    __println("part 3 plus equals:");
    /*
    create.
    assign int 40;
    plus equals 40 + 50 = 90.
    */
    ValuePlusEquals v4 = 40;
    v4 += 50;

    __println("part 4 plus equals fallback:");
    /*
    create. assign int 60.
    create. assign int 70.
    create. assign value 60.
    create. plus equals value 60 + 70 = 130.
    create. assign value 130.
    */
    ValuePlusEquals v5 = 60;
    ValuePlusEquals v6 = 70;
    ValuePlusEquals v7 = v5 + v6;

    __println("part 5 explicit temporary with += fallback.");
    /*
    create.
    create a temp.
    plus equals 80.
    plus equals 90.
    assign value 170.
    */
    ValuePlusEquals v8 = ValuePlusEquals + 80 + 90;

    __println("part 6 class defines both + and +=. ");
    /*
    create.
    create a temp.
    plus equals 100.
    plus equals 110.
    assign value 210.
    */
    ValueBoth v9 = ValueBoth + 100 + 110;

    __println("part 7 type inference with += fall-forward ");
    /*
    create.
    create a temp.
    plus equals 120.
    plus equals 130.
    assign value 250.
    */
    v10 = ValueBoth + 120 + 130;

    return 0;
}
