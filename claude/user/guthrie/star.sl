/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
star electoral method.
*/

import slids.algorithm;
import slids.string;
import slids.math;
import slids.vector;
import star;
import method;
import runner;
import settings;
alias slids;

/*
find the star winner.
assign a score from 0 to 5 based on utilities for each block.
*/
ElectoralMethod : StarMethod() {

    _() { }
    virtual ~() { }
    virtual void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;

        /* initialize rating for each candidate. */
        Vector<int> ratings;
        ratings.resize(ncandidates);
        for (i : 0..ncandidates) {
            ratings[i] = 0;
        }

        /* for each voter bloc. */
        for (it : bloc_map^) {
            float64 first = it^.bloc_.utilities_[0];
            float64 last = it^.bloc_.utilities_[ncandidates-1];
            float64 denom = first - last;

            for (i : 0..ncandidates) {
                float64 utility = it^.bloc_.utilities_[i];
                float64 rating = (utility - last) / denom;
                score int = math:floor(6.0 * rating);
                score = math:clamp(score, 0, 5);
                int which = it^.rankings_[i];
                ratings[which] += score * it^.bloc_.size_;
            }
        }

        /* find the largest score. */
        int a = -1;
        int max = -1;
        for (i : 0..ncandidates) {
            int rating = ratings[i];
            if (max < rating ) {
                a = i;
                max = rating;
            }
        }

        /* find the second largest score. */
        int b = -1;
        max = -1;
        for (i : 0..ncandidates) {
            if (i == a) {
                continue;
            }
            int rating = ratings[i];
            if (max < rating ) {
                b = i;
                max = rating;
            }
        }

        /* find which is ranked higher most often. */
        int counta = 0;
        int countb = 0;
        for (it : bloc_map^) {
            float64 first = it^.bloc_.utilities_[0];
            float64 last = it^.bloc_.utilities_[ncandidates-1];
            float64 denom = first - last;

            int scorea = -1;
            int scoreb = -1;
            for (i : 0..ncandidates) {
                float64 utility = 0.0;
                float64 rating = 0.0;
                int which = it^.rankings_[i];
                if (which == a) {
                    utility = it^.bloc_.utilities_[i];
                    rating = (utility - last) / denom;
                    scorea = (int=math:floor(6.0 * rating));
                    scorea = (int=math:clamp(scorea, 0, 5));
                } else if (which == b) {
                    utility = it^.bloc_.utilities_[i];
                    rating = (utility - last) / denom;
                    scoreb = (int=math:floor(6.0 * rating));
                    scoreb = (int=math:clamp(scoreb, 0, 5));
                }
            }

            if (scorea > scoreb) {
                counta += it^.bloc_.size_;
            } else if (scoreb > scorea) {
                countb += it^.bloc_.size_;
            }
        }
        if (counta >= countb) {
            winner_ = a;
        } else {
            winner_ = b;
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
