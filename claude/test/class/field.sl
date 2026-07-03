/*
test using class fields in statements and expressions.
test inferring field types from default values.

a field with no explicit type infers its type from its default value, like an
inferred local / param / const: a = 1 -> int, b = 3.14 -> float, c = 'q' -> char,
d = true -> bool. the WIDTH follows the value (9999999999 exceeds int32 -> int64).
a const-expr default folds FIRST, then infers (f = K). inferred and explicitly-typed
fields mix freely. a typeless field with NO default is an error (nothing to infer).
*/

/*
claude says:

a typeless field's type is inferred from its default value. construction is field-
init from a compatible tuple, so an inferred field's type IS its default literal's
preferred type. The slot is a kSlid LAYOUT slot, so inference patches it in a classify
pre-pass (classifyClassSignature) — after constfold folds the default to a literal,
before any method body / construction reads the field type — and re-interns the same
(name+def_id-keyed) handle. An inferred field is always PRIMITIVE (a const-expr
default can't be a class), so the resolve needs-ctor/dtor fixpoint, which ran with a
kNoType slot and contributed nothing, stays correct. Field DEFAULTS are applied at
construction: an omitted field takes its default (def below), a partial init tuple
overrides the lead and defaults the rest (part below).
*/

const int K = 5;

Class(
    a = 1,            // int
    b = 3.14,         // float
    c = 'q',          // char
    d = true,         // bool
    e = 9999999999,   // int64 (width follows the value)
    f = K,            // int  (const-expr default, folded before inference)
    int g = 7         // explicitly typed, default 7 — mixed with the inferred fields
) {
    void show() {
        // each field read by its BARE name — both as a ##type operand (the self-field
        // rewrite reaches ##type) and as a value.
        __println(##type(a) + " " + ##type(b) + " " + ##type(c) + " " + ##type(d)
                  + " " + ##type(e) + " " + ##type(f) + " " + ##type(g));
        __println("" + a + " " + b + " " + c + " " + d + " "
                  + e + " " + f + " " + g);
    }

    // bare fields used as EXPRESSIONS — arithmetic, a comparison condition, and
    // mixed with a local.
    int expr() {
        int s = a + g;          // a + g  (int + int)
        if (a < g) {            // bare field in a comparison
            s = s * g;
        }
        return s;
    }

    // spot-check: bare fields across assorted expression positions — as an array
    // index, under address-of (^field) with a write through the reference, in a
    // compound assignment, and bool/char fields in a logical condition.
    int more() {
        int arr[10];
        arr[g] = a * 2;         // field as an array index; field in arithmetic
        int^ p = ^a;            // address-of a bare field
        p^ = p^ + 100;          // write the field through the reference
        f += g;                 // compound assignment on a field
        int sum = arr[g] + a;
        if (d && c == 'q') {    // bool + char fields in a logical condition
            sum = sum + f;
        }
        return sum;
    }
}

int32 main() {

    // every field default-initialized.
    Class def;
    def.show();

    // a partial init tuple overrides the leading fields; the rest take defaults.
    Class part(10, 2.0);
    part.show();

    // fields used in expressions.
    __println("def.expr = " + def.expr());     // 56  (1+7=8; 1<7 -> 8*7)
    __println("part.expr = " + part.expr());   // 17  (10+7=17; 10<7 false)

    // a wider spot-check of fields-in-expressions (index, ^field, compound assign).
    __println("def.more = " + def.more());
    __println("part.more = " + part.more());

    // fields read as expressions from OUTSIDE the class (obj.field), incl. a write.
    // (compute into a local — a parenthesized `+` inside a print arg is a separate
    // pre-existing print-concatenation gap, not a field issue.)
    def.a = def.a + 1000;          // obj.field write, reading obj.field
    int outsum = def.a + def.g;    // obj.field + obj.field in arithmetic
    __println("def.outsum = " + outsum);

    return 0;
}

/* a typeless field with NO default has nothing to infer from. */
//-EXPECT-ERROR: Field 'x' needs an explicit type
//NoType(x) {
//    void p() { __println("" + x); }
//}
