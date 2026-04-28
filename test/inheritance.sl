/*
test file for inheritance.

multiple inheritance is defered.
virtual methods are defered.
*/

/* base class */
Animal(
    /* required field */
    char[] name_,
    /* optional field */
    int legs_ = 0
    /*
    there are no private fields in this example.
    if there were there would be an ellipsis here.
    and they would be initialized caller side by
    Animal:pinit().
    */
) {
    _() {
        __println("Animal:ctor");
    }
    ~() {
        __println("Animal:dtor");
    }

    void speak() {
        __println("Animal:speak: " + name_ + " says nothing.");
    }
}

/*
Cat inherits from Animal.
in memory, Animal fields are first.
followed by Cat fields.
the syntax should match.
Animal : Cat
*/
Animal : Cat(
    /* optional field. */
    int toys_ = 0
) {
    /* called after Animal:ctor */
    _() {
        __println("Cat:ctor");
    }
    /* called before Animal:dtor */
    ~() {
        __println("Cat:dtor");
    }

    /* shadows Animal:speak */
    void speak() {
        __println("Cat:speak: " + name_ + " meows.");
        Animal:speak();
    }
}

/* Dog inherits from Animal. */
Animal : Dog(
    /*
    required field.
    Animal:legs_ is now a required field
    when instantiating a Dog.
    */
    int sticks_
) {
    _() {
        __println("Dog:ctor");
    }
    ~() {
        __println("Dog:dtor");
    }

    void speak() {
        __println("Dog:speak: " + name_ + " barks.");
        Animal:speak();
    }
}

/*
reopen Dog.
don't need fully qualified name because
from a visibility point of view, Dog contains Animal.
block mode addition.
*/
Dog {
    void perform() {
        __println("Dog:perform: " + name_ + " sits.");
    }
}

/* bare addition. */
void Dog:perform2() {
    __println("Dog:perform2: " + name_ + " begs.");
}


int32 main() {
    __println("Hello, World!");

    /* snek has no legs. */
    Animal a("Snek");
    a.speak();

    /*
    princess donut has 0 legs and 0 toys.
    initialize Animal fields.
    call Animal:pinit or Animal:ctor.
    initialize Cat fields.
    call Cat:pinit or Cat:ctor.
    */
    Cat c("Princess Donut");
    c.speak();

    /* spot has 4 legs and 2 sticks. */
    Dog d("Spot", 4, 2);
    d.speak();
    d.perform();
    d.perform2();

    /*
    automatic in-cast pointer assignments.
    no cast needed from Cat to Animal
    because a Cat is always an Animal.
    */
    Animal^ paa = ^a;
    Animal^ pac = ^c;
    Animal^ pad = ^d;

    /*
    explicit out-cast pointer assignments.
    simple cast needed.
    do not need to go through <void^>
    not all Animals are Cats.
    but Animals and Cats are related.
    this is a smaller foot-gun than casting
    between unrelated types.
    */
    Cat^ pc = <Cat^> pac;
    Dog^ pd = <Dog^> pad;

    __println("Goodbye, World!");
    return 0;
}
