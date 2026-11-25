/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
range (score) electoral method.
*/

import slids.string;
import slids.vector;
import range;
import method;
import runner;
import settings;
alias slids;

/*
find the range (score) voting winner.
assign a range based on utilities for each block.
*/
ElectoralMethod : RangeMethod() {

    _() { }
    virtual ~() { }
    virtual void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;

        /* initialize rating for each candidate. */
        Vector<float64> ratings;
        ratings.resize(ncandidates);
        for (i : 0..ncandidates) {
            ratings[i] = 0.0;
        }

        /* for each voter bloc. */
        for (it : bloc_map^) {
            float64 first = it^.bloc_.utilities_[0];
            float64 last = it^.bloc_.utilities_[ncandidates-1];
            float64 denom = first - last;

            for (i : 0..ncandidates) {
                float64 utility = it^.bloc_.utilities_[i];
                float64 rating = (utility - last) / denom;
                int which = it^.rankings_[i];
                ratings[which] += rating * (float64=it^.bloc_.size_);
            }
        }

        /* find the largest rating. */
        winner_ = -1;
        float64 max = -1.0;
        for (i : 0..ncandidates) {
            float64 rating = ratings[i];
            if (max < rating ) {
                winner_ = i;
                max = rating;
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
