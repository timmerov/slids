/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
condorcet electoral method.
*/

import slids.string;
import slids.vector;
import condorcet;
import method;
import runner;
import settings;
alias slids;

/*
condorcet wins the most head to head races.
*/
ElectoralMethod : CondorcetMethod() {

    _() { }
    virtual ~() { }
    virtual void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;

        /* initialize number of wins for each candidate. */
        Vector<int> wins;
        wins.reserve(ncandidates);
        wins.resize(ncandidates);
        for (i : 0..ncandidates) {
            wins[i] = 0;
        }

        println("Condorcet results:");
        /* count head to head victories. */
        for (i : 0..ncandidates) {
            for (k : (i + 1)..ncandidates) {
                a = i;
                b = k;
                avotes = g_runner.pairwise_.count(a, b);
                bvotes = g_runner.pairwise_.count(b, a);
                bwins = g_runner.pairwise_.is_winner(b, a);
                if (bwins) {
                    a <--> b;
                    avotes <--> bvotes;
                }
                println(String + " " + candidates^[a].name_ + " (" + avotes
                    + ") > " + candidates^[b].name_ + " (" + bvotes + ")");
                ++wins[a];
            }
        }

        /* test for condorcet ordering. */
        uint64 counts = 0;

        /*
        find the candidate with the most wins.
        find the average utility while we're here.
        */
        int max = -1;
        winner_ = -1;
        int nwinners = 0;
        int loser = -1;
        float64 total_utility = 0.0;
        for (i : 0..ncandidates) {
            int w = wins[i];
            bool sum_it = false;
            if (max == w) {
                ++nwinners;
                sum_it = true;
            } else if (max < w) {
                winner_ = i;
                nwinners = 1;
                max = w;
                total_utility = 0.0;
                sum_it = true;
            }
            if (sum_it) {
                total_utility += candidates^[i].utility_;
            }
            if (w == 0) {
                loser = i;
            }

            /* set bit for condorcet ordering. */
            counts |= 1 << w;
        }

        /* no winner if there's a cycle. */
        if (nwinners > 1) {
            println("Condorcet cycle exists.");
            winner_ = -1;
            ++g_runner.condorcet_cycles_;
        }

        /* check if there is a condorcet ordering. */
        if (nwinners == 1) {
            println("Condorcet ordering exists.");
            candidate = ^candidates^[winner_];
            g_runner.total_utility_ordering_ += candidate^.utility_;
        }

        /*
        average the utility for all candidates in the cycle if any.
        accumulate for the summary.
        */
        float64 utility = total_utility / (float64=nwinners);
        total_utility_ += utility;

        /* accumulate correlations. */
        if (winner_ == g_runner.guthrie_.winner_) {
            ++is_winner_;
        }

        /* return results. */
        g_runner.condorcet_loser_ = loser;
    }

    /* pretty print the winner. */
    virtual void print_winner() {
        Format fmt;
        fmt.min_width_ = kWidth;
        str = String + " " + name_;
        result = result_to_string(g_runner.guthrie_.winner_, winner_);

        winner_name = '?';
        if (winner_ >= 0) {
            winner_name = g_runner.candidates_[winner_].name_;
        }
        println(String + fmt*str + ": " + winner_name + " " + result);
    }

    virtual void print_satisfaction() {
        ntrials float64 = g_runner.ntrials_;
        condorcet_cycles float64 = g_runner.condorcet_cycles_;
        non_cycle_trials float64 = ntrials - condorcet_cycles;
        float64 average_utility_ordering = g_runner.total_utility_ordering_ / non_cycle_trials;
        float64 satisfaction_ordering = calculate_satisfaction(average_utility_ordering, g_runner.accumulated_);

        /* pretty print the line. */
        Format fmt;
        fmt.min_width_ = kWidth;
        str = String + " " + name_ + " (winner exists)";
        println(String + fmt*str + ": " + satisfaction_ + " (" + satisfaction_ordering + ")");
    }

    virtual void print_correlation() {
        non_cycle_trials float64 = g_runner.ntrials_ - g_runner.condorcet_cycles_;
        float64 correlation_max = 100.0 * (float64=is_winner_) / non_cycle_trials;

        /* pretty print the line. */
        Format fmt;
        fmt.min_width_ = kWidth;
        str = String + " " + name_ + " (winner exists)";
        println(String + fmt*str + ": " + correlation_ + "% (" + correlation_max + "%)");
    }
}
