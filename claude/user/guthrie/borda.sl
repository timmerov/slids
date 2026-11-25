/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
borda count electoral method.
*/

import slids.string;
import slids.vector;
import borda;
import method;
import runner;
import settings;
alias slids;

/*
borda count 0 for first, 1 for second, ...
lowest total wins.
*/
ElectoralMethod : BordaMethod() {

    _() { }
    virtual ~() { }
    virtual void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;

        /* initialize number of wins for each candidate. */
        Vector<int> counts;
        counts.reserve(ncandidates);
        counts.resize(ncandidates);
        for (i : 0..ncandidates) {
            counts[i] = 0;
        }

        /* for each voter bloc. */
        for (it : bloc_map^) {
            for (i : 0..ncandidates) {
                int which = it^.rankings_[i];
                counts[which] += i * it^.bloc_.size_;
            }
        }

        /* find the lowest count. */
        winner_ = -1;
        int min = 0x7FFFFFFF;
        for (i : 0..ncandidates) {
            int count = counts[i];
            if (min > count ) {
                winner_ = i;
                min = count;
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
}
