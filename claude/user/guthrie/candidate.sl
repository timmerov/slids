/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
candidate classes.
*/

import slids.string;
import candidate;
import runner;

Rankings() {
    /* candidate names in ranked order. */
    slids:String to_string() {
        slids:String s;
        for (which : self) {
            s += g_runner.candidates_[which].name_;
        }
        return s;
    }
}

Candidate() {
    /* for sorting */
    bool const op<(Candidate^ other) {
        return (position_ < other^.position_);
    }
}
