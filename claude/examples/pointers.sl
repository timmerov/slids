import string;

Base(int b_ = 1) { }
Base : Derived(int d_ = 2) { }

int32 main() {
    int x = 42;
    int^ ref = ^x;                  // reference: address of x
    ref^ = 7;                       // x is now 7

    int arr[4] = (1, 2, 3, 4);
    int[] it = ^arr[1];             // iterator
    ++it;
    intptr dist = it - ^arr[0];     // 2

    void^ raw = ref;                // implicit: drops type info
    int^ back = <int^> raw;         // explicit: adds it back

    Derived d;
    Base^ bp = ^d;                  // derived to base: implicit
    Derived^ dp = <Derived^> bp;    // base to derived: explicit

    int[] heap = new int[3];
    heap[0] = it^;
    int^ one = new int;
    one^ = 5;
    delete heap;                    // frees and sets heap to nullptr
    delete one;

    int[] p = ^arr[0];
    a = p++^;                       // reads arr[0], then advances p
    b = (++p)^;                     // advances p, then reads arr[2]

    int8[] buf = new int8[sizeof(Derived)];
    Derived^ placed = new(buf) Derived(5, 6);   // placement new
    placed^.~();                    // explicit dtor
    delete buf;

    println(String + x + " " + dist + " " + back^ + " " + dp^.d_ + " " + (heap == nullptr) + " " + a + " " + b);
    return 0;
}
