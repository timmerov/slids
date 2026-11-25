/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
anti-plurality electoral method.
*/

import slids.string;
import slids.vector;
import anti_plurality;
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

    /* analyze strategic voting. */
    virtual void strategic_voting() {
        ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;

        Vector<int> counts;
        Vector<int> minions;
        counts.resize(ncandidates);
        minions.resize(ncandidates);

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
            the W>A voters are going to vote honestly.
            we need to know the W>A and A>W counts for each candidate.
            check the victory margin for A v X.
            if negative then we have a deficit.
            zero it as much as possible with honest minions.
            otherwise accumulate the total deficit.
            */

            /* clear vote counts. */
            for (p^ : counts) {
                p^ = 0;
            }
            for (p^ : minions) {
                p^ = 0;
            }

            /* count last place votes. */
            last = ncandidates - 1;
            for (it : bloc_map^) {
                which = it^.rankings_[last];
                size = it^.bloc_.size_;
                for (c : it^.rankings_) {
                    if (c == a) {
                        minions[which] += size;
                        break;
                    }
                    if (c == winner_) {
                        counts[which] += size;
                        break;
                    }
                }
            }

            /* determine the surplus and the deficit. */
            surplus = 0;
            deficit = 0;
            for (c : 0..ncandidates) {
                if (c == a) {
                    continue;
                }
                /*
                margin is how many votes A can gain and still lose to C.
                when it's negative we have to add votes to C.
                some or all of the C-last minions need to vote honestly.
                the rest if any join the surplus pool.
                */
                margin = loss_margin(a, c, counts[a], counts[c]);
                if (margin < 0) {
                    margin = - margin;
                    if (margin > minions[c]) {
                        margin -= minions[c];
                        minions[c] = 0;
                    } else {
                        minions[c] -= margin;
                        margin = 0;
                    }
                    deficit += margin;
                }
                surplus += minions[c];
            }

            /* number of strategic minions. */
            nminions = surplus - deficit;
            if (nminions < 0) {
                continue;
            }

            /* save new minimums. */
            if (min_minions < 0
            ||  min_minions > deficit) {
                min_minions = deficit;
                villain = a;
            }
        }

        /* no strategic winner. */
        if (villain < 0) {
            return;
        }

        String others;
        for (c : 0..ncandidates) {
            if (c == villain) {
                continue;
            }
            if (others.size()) {
                others += ",";
            }
            others += candidates^[c].name_;
        }

        namew = candidates^[winner_].name_;
        namev = candidates^[villain].name_;
        println(String + " " + name_ + " needs " + min_minions + " " + namev
            + ">" + namew + " voters to veto " + others + ".");

        ++nvillains_;
        nminions_ += min_minions;
    }

    /* pretty print strategic voting summary. */
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
            println("  W>A pairwise. voters who prefer A>W veto all others.");
        }
    }
}
