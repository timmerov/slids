/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
guthrie electoral method.
*/

import slids.string;
import slids.vector;
import guthrie;
import method;
import runner;
import settings;
alias slids;

/*
find the guthrie winner.
*/
ElectoralMethod : GuthrieMethod() {

    _() { }
    virtual ~() { }

    /*
    find winner by condorcet method.
    resolve cycles by coombs.
    use candidate rankings - not voter rankings.
    */
    virtual void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        int total_support = g_runner.total_support_;

        /* initialize the number of wins. */
        Vector<int> wins;
        wins.resize(ncandidates);
        for (i : 0..ncandidates) {
            wins[i] = 0;
        }

        /* for every pair of candidates. */
        for (a : 0..ncandidates) {
            for (b : (a+1)..ncandidates) {
                /* count the votes. */
                avotes = 0;
                bvotes = 0;
                for (candidate : candidates^) {
                    for (which : candidate^.rankings_) {
                        if (which == a) {
                            avotes += candidate^.support_;
                            break;
                        }
                        if (which == b) {
                            bvotes += candidate^.support_;
                            break;
                        }
                    }
                }
                /* record the winner. */
                if (avotes >= bvotes) {
                    ++wins[a];
                } else {
                    ++wins[b];
                }
            }
        }

        /*
        find the smith set.
        technically, this is the copeland leaders - thanks claude.
        good enough.
        */
        Vector<int> smithset;
        smithset.reserve(ncandidates);
        max_wins = -1;
        for (i : 0..ncandidates) {
            test = wins[i];
            if (test > max_wins) {
                smithset.resize(0);
            }
            if (test >= max_wins) {
                smithset.append(i);
                max_wins = test;
            }
        }

        /* create a bitmap of elligible candidates. */
        uint64 elligible = 0;
        for (which : smithset) {
            elligible |= (uint64=1) << which;
        }

        /* allocate support and anti-support vectors. */
        Vector<int> support;
        Vector<int> anti_support;
        support.resize(ncandidates);
        anti_support.resize(ncandidates);

        /* eliminate the worst. */
        while () {
            smithsize int = smithset.size();
            if (smithsize <= 1) {
                break;
            }

            /* init support and anti-support. */
            for (i : 0..ncandidates) {
                support[i] = 0;
                anti_support[i] = 0;
            }

            /* sum support and anti-support. */
            for (candidate : candidates^) {
                /* search forwards. */
                for (i : 0..ncandidates) {
                    which = candidate^.rankings_[i];
                    bit = (uint64=1) << which;
                    if (elligible & bit) {
                        support[which] += candidate^.support_;
                        break;
                    }
                }
                /* search backwards. */
                for (i : (ncandidates-1)..>=0 -1) {
                    which = candidate^.rankings_[i];
                    bit = (uint64=1) << which;
                    if (elligible & bit) {
                        anti_support[which] += candidate^.support_;
                        break;
                    }
                }
            }

            /* find a candidate with majority support. */
            for (i : smithset) {
                votes = support[i];
                if (2*votes > total_support) {
                    smithset.resize(1);
                    smithset[0] = i;
                    break elimination_loop;
                }
            }

            /* find the candidate with the most anti-support. */
            max_anti_support = -1;
            index_to_remove = -1;
            for (i : 0..smithsize) {
                which = smithset[i];
                test = anti_support[which];
                if (test >= max_anti_support) {
                    max_anti_support = test;
                    index_to_remove = i;
                }
            }

            /* remove them. */
            which = smithset[index_to_remove];
            bit = (uint64=1) << which;
            elligible &= ~bit;
            smithset.remove(index_to_remove);
        } : elimination_loop;

        /* save the winner. */
        winner_ = smithset[0];
    }

    virtual void print_winner() {
        /* already printed. */
    }

    virtual void print_satisfaction() {
        /* calculate strategic voting total utility. */
        ntrials float64 = g_runner.ntrials_;
        float64 average_utility_strategic = g_runner.total_utility_strategic_ / ntrials;
        float64 satisfaction_strategic = calculate_satisfaction(average_utility_strategic, g_runner.accumulated_);

        /* pretty print the line. */
        Format fmt;
        fmt.min_width_ = kWidth;
        str = String + " " + name_ + " (strategic)";
        println(String + fmt*str + ": " + satisfaction_ + " (" + satisfaction_strategic + ")");
    }

    virtual void print_correlation() {
        /*
        nothing to print.
        guthrie correlates with itself 100% of the time.
        */
    }

    virtual void print_strategic_results() {
        Format fmt;
        fmt.float_style_ = kFixedPoint;
        fmt.precision_ = 1;

        ntrials float64 = g_runner.ntrials_;
        nstrategic float64 = g_runner.ntrials_ - g_runner.independence_;
        wins = 100.0 * nstrategic / ntrials;

        println(String + " " + name_ + ": " + fmt*wins + "% favorite betrayal");
        if (kExplainStrategies) { void;
            println("  You prefer A>B>C. C wins because candidate A prefers C>B. Vote B.");
            println("  You prefer A>C>B. Vote C to counter ABC strategy.");
        }
    }
}
