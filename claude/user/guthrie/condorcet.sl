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
        }

        /*
        no winner if there's a cycle.
        check max=ncandidates-1 not nwinners>1.
        it's possible for 5 candidates to all have 1+ losses.
        */
        if (max != ncandidates - 1) {
            println("Condorcet cycle exists.");
            winner_ = -1;
            ++g_runner.condorcet_cycles_;
        }

        /* check if there is a condorcet ordering. */
        if (winner_ >= 0) {
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

    /*
    analyze strategic voting.

    A needs to create an A>B>W>A cycle.
    where A>B and W>A head to head.
    some AWB need to vote ABW.
    this may change votes for XvB.
    but they are harmless.
    they can only help B.
    B can never be the condorcet winner.

    nit: we need to make W lose to B without making B the condorcet winner.
    the algorithm does that.
    it doesn't require A to be in the cycle.
    nor does it need to.
    the cycle thinking is a mental shortcut.
    */
    virtual void strategic_voting() {

        /*
        abort if there is already a cycle.
        */
        if (winner_ < 0) {
            return;
        }

        ncandidates = g_runner.ncandidates_;
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

            /* W is the condorcet winner: W>A pairwise. */

            /* find a B minions can push ahead of W. */
            for (b : 0..ncandidates) {
                /* B cannot be A W or beat A head to head. */
                if (b == winner_) {
                    continue;
                }
                if (b == a) {
                    continue;
                }
                b_beats_a = g_runner.pairwise_.is_winner(b, a);
                if (b_beats_a) {
                    continue;
                }

                /* count BvW votes. */
                bvotes = 0;
                wvotes = 0;
                for (it : bloc_map^) {
                    for (which : it^.rankings_) {
                        if (which == b) {
                            bvotes += it^.bloc_.size_;
                            break;
                        }
                        if (which == winner_) {
                            wvotes += it^.bloc_.size_;
                            break;
                        }
                    }
                }

                /* W is the condorcet winner: W>A pairwise. */

                /* count AWB minions. */
                minions = 0;
                for (it : bloc_map^) {
                    arank = 0;
                    brank = 0;
                    wrank = 0;
                    for (i : 0..ncandidates) {
                        which = it^.rankings_[i];
                        if (which == a) {
                            arank = i;
                        } else if (which == b) {
                            brank = i;
                        } else if (which == winner_) {
                            wrank = i;
                        }
                    }
                    if (arank < wrank && wrank < brank) {
                        minions += it^.bloc_.size_;
                    }
                }

                /*
                find the margin of victory.
                W can lose this many votes and still beat B.
                */
                margin = victory_margin(winner_, b, wvotes, bvotes);
                required = margin/2 + 1;

                /* not enough minions. */
                if (minions < required) {
                    continue;
                }

                /* we have a villian. */
                if (min_minions < 0 || min_minions > required) {
                    villain = a;
                    min_minions = required;
                }
            }
        }

        /* no villain. */
        if (villain < 0) {
            return;
        }

        namev = candidates^[villain].name_;
        println(String + " " + name_ + ": " + namev + " needs " + min_minions
            + " voters to vote strategically.");

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
            println("  W>A, A>B pairwise. AWB voters shift to ABW to create cycle.");
        }
    }
}
