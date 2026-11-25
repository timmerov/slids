/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
candidate satisfaction electoral method.
*/

import slids.string;
import slids.vector;
import candidate_method;
import method;
import runner;
import settings;
alias slids;

/*
check if the winning candidate maximizes the total
satisfaction of all candidates.
not voters.
*/
ElectoralMethod : CandidateMethod() {

    _() { }
    virtual ~() { }
    virtual void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;

        winner_ = -1;
        float64 max = -1e99;

        for (i : 0..ncandidates) {
            a = ^candidates^[i];
            float64 utility = 0.0;
            for (k : 0..ncandidates) {
                if (k == i) {
                    continue;
                }
                b = ^candidates^[k];
                utility += a^.position_.utility(b^.position_);
            }
            if (utility > max) {
                winner_ = i;
                max = utility;
            }
        }

        /* accumulate the utility. */
        candidate = ^candidates^[winner_];
        total_utility_ += candidate^.utility_;

        /* accumulate correlations. */
        if (winner_ == g_runner.guthrie_.winner_) {
            ++is_winner_;
        }
    }

    virtual void print_winner() {
        /* nothing to print. */
    }

    virtual void print_satisfaction() {
        /* nothing to print. */
    }

    virtual void print_correlation() {
        /* nothing to print. */
    }
}
