/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
anti-plurality electoral method.
*/

import slids.string;
import slids.vector;
import antiplurality;
import method;
import runner;
import settings;
alias slids;

/*
find the anti-plurality winner.
*/
ElectoralMethod : AntiPluralityMethod() {

    _() { }
    virtual ~() { }

    virtual void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;

        Vector<int> counts;
        counts.reserve(ncandidates);
        counts.resize(ncandidates);

        /* clear the counts. */
        for (i : 0..ncandidates) {
            counts[i] = 0;
        }

        /* sum the votes. */
        int pos = ncandidates - 1;
        for (it : bloc_map^) {
            int last = it^.rankings_[pos];
            counts[last] += it^.bloc_.size_;
        }

        /* find the winner. */
        winner_ = 0;
        int votes = 0x7FFFFFFF;
        for (i : 0..ncandidates) {
            int count = counts[i];
            if (count <= votes) {
                winner_ = i;
                votes = count;
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
