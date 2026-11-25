/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
plurality electoral method.
*/

import slids.string;
import slids.vector;
import plurality;
import method;
import runner;
import settings;
alias slids;

/*
find the plurality winner.
*/
ElectoralMethod : PluralityMethod() {

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

        /* sum the votes. */
        for (it : bloc_map^) {
            int first = it^.rankings_[0];
            counts[first] += it^.bloc_.size_;
        }

        /* find the winner. */
        winner_ = -1;
        int votes = -1;
        for (i : 0..ncandidates) {
            int count = counts[i];
            if (count > votes) {
                winner_ = i;
                votes = count;
            }
        }

        /* accumulate the utility. */
        candidate = ^candidates^[winner_];
        total_utility_ += candidate^.utility_;

        /* was it a majority? */
        if (2*votes > nvoters) {
            ++g_runner.majority_winners_;
        }

        /* accumulate correlations. */
        if (winner_ == g_runner.guthrie_.winner_) {
            ++is_winner_;
        }
    }

    virtual void strategic_voting() {
        int ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;

        /*
        we're looking for the minimum number of strategic
        voters it would take to replace the honest winner
        with a min/maxxing villain candidate.
        */
        villain = -1;
        min_minions = -1;

        for (a : 0..ncandidates) {
            if (a == winner_) {
                continue;
            }

            /*
            if A beats W (the winner) then strategically voting
            to make A the winner is good strategic voting.
            we're looking for bad strategic voting.
            */
            a_beats_w = g_runner.pairwise_.is_winner(a, winner_);
            if (a_beats_w) {
                continue;
            }

            /*
            we never change votes for winner.
            we only add counts to A.
            so we only need to count A votes and W votes.
            */
            a_votes = 0;
            w_votes = 0;
            xtoa_pool = 0;
            for (it : bloc_map^) {
                first = it^.rankings_[0];
                /* A voters keep voting A. */
                if (first == a) {
                    a_votes += it^.bloc_.size_;
                    continue;
                }
                /* W voters keep voting W. */
                if (first == winner_) {
                    w_votes += it^.bloc_.size_;
                    continue;
                }
                /* not A not W voters. */
                for (which : it^.rankings_) {
                    /* these prefer A>W */
                    if (which == a) {
                        xtoa_pool += it^.bloc_.size_;
                        break;
                    }
                    /* these prefer W>A so they keep voting honestly. */
                    if (which == winner_) {
                        break;
                    }
                }
            }

            /* A must shift enough votes to win. */
            marginwa = victory_margin(winner_, a, w_votes, a_votes);
            required = marginwa + 1;

            /* not enough votes in the pool */
            if (xtoa_pool < required) {
                continue;
            }

            /* save new minimums. */
            if (min_minions < 0
            ||  min_minions > required) {
                min_minions = required;
                villain = a;
            }
        }

        /* no strategic winner. */
        if (villain < 0) {
            return;
        }

        String others;
        for (a : 0..ncandidates) {
            if (a == winner_) {
                continue;
            }
            if (a == villain) {
                continue;
            }
            if (others.size()) {
                others += ",";
            }
            others += candidates^[a].name_;
        }

        namev = candidates^[villain].name_;
        println(String + " " + name_ + " needs " + min_minions
            + " voters to switch from " + others + " to " + namev + ".");

        ++nvillains_;
        nminions_ += min_minions;
    }

    virtual void print_strategic_results() {
        Format fmt;
        fmt.float_style_ = kFixedPoint;
        fmt.precision_ = 1;

        ntrials float64 = g_runner.ntrials_;
        nvillains float64 = nvillains_;
        wins = 100.0 * nvillains / ntrials;
        coalition_size float64 = 0.0;
        coalition_pct float64 = 0.0;
        if (nvillains_ > 0) {
            nvoters float64 = g_runner.electorate_.nvoters_;
            coalition_size = nminions_ / nvillains;
            coalition_pct = 100.0 * coalition_size / nvoters;
        }

        println(String + " " + name_ + ": " + fmt*wins + "% (" + nvillains_ + ")"
            + " strategic voters: " + fmt*coalition_pct + "% (" + coalition_size + ")");
        if (kExplainStrategies) { void;
            println("  W>A pairwise. BC+ voters who prefer A>W switch to A.");
        }
    }
}
