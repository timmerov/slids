/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
electoral method base class.
*/

import pairwise;
import runner;
alias slids;

HeadToHead(
    int winner_ = 0,
    int loser_ = 0,
    int winner_votes_ = 0,
    int loser_votes_ = 0
) {
    _() {}
    ~() {}

    /* find the winner in a head to head election. */
    void resolve(
        int a,
        int b
    ) {
        bloc_map = ^g_runner.bloc_map_;

        /* init vote counts. */
        int avotes = 0;
        int bvotes = 0;

        /* for each voter bloc. */
        for (it : bloc_map^) {
            /* give the votes to whichever is first. */
            for (which : it^.rankings_) {
                if (which == a) {
                    avotes += it^.bloc_.size_;
                    break;
                }
                if (which == b) {
                    bvotes += it^.bloc_.size_;
                    break;
                }
            }
        }

        /*
        we assume that a comes before b in the candidate list.
        and that we're looking for a winner.
        in which case, by the rules, a wins ties.
        */
        if (avotes >= bvotes) {
            winner_ = a;
            loser_ = b;
            winner_votes_ = avotes;
            loser_votes_ = bvotes;
        } else {
            winner_ = b;
            loser_ = a;
            winner_votes_ = bvotes;
            loser_votes_ = avotes;
        }

        /* correct the a first assumption. */
        if (avotes == bvotes) {
            if (a > b) {
                winner_ <--> loser_;
                winner_votes_ <--> loser_votes_;
            }
        }
    }
}

/*
store the results of head to head matchups between all candidates.
the vote totals are stored: winner is positive, loser is negative.
*/
PairwiseCounts() {
    void init() {
        n_ = g_runner.ncandidates_;
        size = n_ * n_;
        counts_.resize(size);
        for (i : 0..size) {
            counts_[i] = 0;
        }
    }

    /* find all pairwise winners. */
    void find_winners() {
        HeadToHead matchup;
        for (a : 0..n_) {
            for (b : (a+1)..n_) {
                matchup.resolve(a, b);
                set_count(
                    matchup.winner_,
                    matchup.loser_,
                    matchup.winner_votes_
                );
                set_count(
                    matchup.loser_,
                    matchup.winner_,
                    - matchup.loser_votes_
                );
            }
        }
    }

    /* return positive vote count for a versus b. */
    int count(int a, int b) {
        idx = a * n_ + b;
        votes = counts_[idx];
        if (votes < 0) {
            votes = - votes;
        }
        return votes;
    }

    /* set count for a versus b. */
    void set_count(int a, int b, int count) {
        idx = a * n_ + b;
        counts_[idx] = count;
    }

    /* return true if a beats b. */
    bool is_winner(int a, int b) {
        idx = a * n_ + b;
        votes = counts_[idx];
        return (votes > 0);
    }
}
