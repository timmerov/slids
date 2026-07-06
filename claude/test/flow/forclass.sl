/*
test for over a class.


for the purposes of shadowing variables, there are 3 scopes counting the
enclosing scope:
normal local variable shadowing rules for scopes apply to these scopes.

    |--enclosing---------------|
    { for (var : class) {body} }
                        |body|
          |--loop-var--------|
*/

int32 main() {

    return 0;
}
