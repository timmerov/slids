/*
test malformed comments.
*/

/* error */
/\
/

/* error */
/\
*

/* error */
*\
/

/* error */
\ [white space]

int32 main() {
    return 0;
}

// error expected
/*
