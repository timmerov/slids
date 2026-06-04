/*
test new and delete.

new returns a reference or an iterator.
delete sets the pointer to nullptr;

    int^ ref = new int;
    int[] iter = new int[4];
    delete ref;
    delete iter;

placement new instantiates the variable at the given storage.

    intptr sz = 2 * sizeof(int);
    void^ rawp = new int8[sz];

    int[] intp = new(rawp) int[2];

    delete rawp;
*/

/*
claude says:

tbd.
*/

int32 main() {

    return 0;
}
