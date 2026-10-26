/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
algorithm library.

template implementations.
*/

import algorithm;

/* slids namespace */
slids {

/*
return the number of elements in an array.
*/
intptr countof<T=[N]>(T arg) {
    return N;
}

/*
return the size of a container class.
the class must define a size() method.
*/
intptr countof<T=ClassSet>(T arg) {
    return arg.size();
}

/*
quicksort an array or a container class.
a container class must be index-able.
ie it must implement the index operator: op[].
the contained type must be comparable by less-than.
ie a contained class must implement op<.
*/
void quicksort<T>(mutable T container) {

    /*
    the countof template works for arrays and classes
    that implement a size() method.
    */
    sz = countof<T>(container);
    subsort(0, sz-1);

    void subsort(intptr first, intptr last) {
        /* done */
        if (first >= last) {
            return;
        }

        /* grab the pivot. */
        pivot = container[first];

        /* loop. */
        limit = first;
        scan = last;
        while {
            /*
            small values stay in place.
            big values move to the end.
            */
            value = container[scan];
            if (pivot < value) {
                --scan;
            } else {
                container[limit] = value;
                ++limit;
                container[scan] = container[limit];
            }
        } (limit < scan);

        /* place the pivot at the correct place. */
        container[scan] = pivot;

        /* recurse. */
        subsort(first, scan-1);
        subsort(scan+1, last);
    }
}

} /* slids */

/* math namespace */
math {

/* min template */
T min<T>(T a, T b) {
    if (a < b) {
        return a;
    }
    return b;
}

/* max template */
T max<T>(T a, T b) {
    if (b < a) {
        return a;
    }
    return b;
}

/* clamp template */
T clamp<T>(T x, T lo, T hi) {
    if (x < lo) {
        return lo;
    }
    if (hi < x) {
        return hi;
    }
    return x;
}

} /* math */
