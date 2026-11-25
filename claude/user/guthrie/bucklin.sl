/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
bucklin electoral method.
*/

import slids.string;
import slids.vector;
import bucklin;
import method;
import runner;
import settings;
alias slids;

/*
bucklin voting.
while no greatest majority winner, accumulate the next choices.
*/
ElectoralMethod : BucklinMethod() {

    _() { }
    virtual ~() { }
    virtual void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;
        int nvoters = g_runner.electorate_.nvoters_;

        Vector<int> counts;
        counts.reserve(ncandidates);
        counts.resize(ncandidates);

        /* clear the counts. */
        for (i : 0..ncandidates) {
            counts[i] = 0;
        }

        /* potentially accumulate over all rankings. */
        for (i : 0..ncandidates) {
            /* no winner. */
            winner_ = -1;

            /* sum the votes. */
            for (it : bloc_map^) {
                int which = it^.rankings_[i];
                counts[which] += it^.bloc_.size_;
            }

            /* find the index with the maximum count. */
            int max = -1;
            for (k : 0..ncandidates) {
                int c = counts[k];
                if (c > max) {
                    max = c;
                    winner_ = k;
                }
            }

            /* done when they have the greatest majority. */
            if (2*max > nvoters) {
                break;
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
