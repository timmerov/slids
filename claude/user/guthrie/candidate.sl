/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
candidate classes.
*/

import candidate;

Candidate() {
    /* for sorting */
    bool const op<(Candidate^ other) {
        return (position_ < other^.position_);
    }
}
