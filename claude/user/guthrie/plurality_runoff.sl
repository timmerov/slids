/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
plurality runoff electoral method.
*/

import slids.string;
import+ slids.vector;
import plurality_runoff;
import method;
import runner;
import settings;
alias slids;

/*
find the plurality runoff winner.
if there is no majority, then the top two go head to head.
*/
ElectoralMethod : PluralityRunoffMethod() {

    _() { }
    virtual ~() { }
    virtual void find_winner(
        bool quiet = kQuiet
    ) {
        ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;
        nvoters = g_runner.electorate_.nvoters_;

        Vector<int> counts;
        counts.resize(ncandidates);

        /* clear the counts. */
        for (p^ : counts) {
            p^ = 0;
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

        /* go to the runoff if no majority. */
        if (2*votes <= nvoters) {
            /* remove the winner from the counts. */
            counts[winner_] = 0;

            /* find the second place finisher. */
            int second = -1;
            votes = -1;
            for (i : 0..ncandidates) {
                int count = counts[i];
                if (count > votes) {
                    second = i;
                    votes = count;
                }
            }

            /* find the head to head winner. */
            upset = g_runner.pairwise_.is_winner(second, winner_);

            /* overwrite the winner. */
            if (upset) {
                winner_ = second;
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

    virtual void strategic_voting() {
        ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;
        nvoters = g_runner.electorate_.nvoters_;

        Vector<int> counts;
        Vector<int> pools;
        counts.resize(ncandidates);
        pools.resize(ncandidates);

        Minion(
            int x_,
            LP:Var xa_,
            LP:Var xb_
        ) { }
        alias Minions = Vector<Minion>;
        Minions minions;
        minions.reserve(ncandidates);

        villaina = -1;
        villainb = -1;
        min_minions = -1;
        min_ab = -1;
        min_ba = -1;
        min_xa = -1;
        min_xb = -1;

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
            we need an an AB runoff.
            if we have an AW runoff, A loses.
            we need to find B who loses to A.
            */
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

                /*
                solve this using linear programming.
                we need to know the total vote counts for every candidate.
                we need to know the number of X voters that prefer A>W.
                this set the size of the pool we can recruit minions from.

                A = counts[a]
                B = counts[b]
                W = counts[w]
                X = counts[x]
                XAW = pools[x]
                note: there may be multiple X's.
                */

                /* clear the counts. */
                for (p^ : counts) {
                    p^ = 0;
                }
                for (p^ : pools) {
                    p^ = 0;
                }

                /* re-count the votes. */
                for (it : bloc_map^) {
                    first = it^.rankings_[0];
                    counts[first] += it^.bloc_.size_;

                    /* add voters who rank A>W to a pool. */
                    for (which : it^.rankings_) {
                        if (which == a) {
                            pools[first] += it^.bloc_.size_;
                            break;
                        }
                        if (which == winner_) {
                            break;
                        }
                    }
                }

                /*
                linear programming variables:
                AB shifts votes from A to B.
                BA shifts votes from B to A.
                XA shifts votes from X to A.
                XB shifts votes from X to B.
                note: there may be multiple X's.
                */
                LP lp;
                ab = lp.add_var(String + candidates^[a].name_ + candidates^[b].name_);
                ba = lp.add_var(String + candidates^[b].name_ + candidates^[a].name_);
                minions.clear();
                for (x : 0..ncandidates) {
                    /* skip candidates A,B,W. */
                    if (x == a || x == b || x == winner_) {
                        continue;
                    }

                    minion = minions.append();
                    minion^.x_ = x;
                    minion^.xa_ = lp.add_var(String + candidates^[x].name_ + candidates^[a].name_);
                    minion^.xb_ = lp.add_var(String + candidates^[x].name_ + candidates^[b].name_);
                }

                /*
                linear programming equations:

                A,B must finish 1st or 2nd.
                A - AB + BA + sum XA > W
                A - AB + BA + sum XA > X - XA - XB
                B + AB - BA + sum XB > W
                B + AB - BA + sum XB > X - XA - XB

                B cannot have a majority.
                B < votes/2

                votes shifted from A,B,X are limited:
                AB <= A
                BA <= BAW
                XA + XB <= XAW
                */
                suma = LP:Expr + counts[a] - ab + ba;
                sumb = LP:Expr + counts[b] + ab - ba;
                for (minion : minions) {
                    suma += minion^.xa_;
                    sumb += minion^.xb_;
                }
                rowaw = suma + gt_or_gte(a, winner_) + counts[winner_];
                rowbw = sumb + gt_or_gte(b, winner_) + counts[winner_];
                rowab = LP:Expr + ab + LP:lte + counts[a];
                rowba = LP:Expr + ba + LP:lte + pools[b];
                /* ==tsc== check this. */
                rowb = sumb + LP:lt + nvoters/2;
                lp.add_row(rowaw);
                lp.add_row(rowbw);
                lp.add_row(rowab);
                lp.add_row(rowba);
                lp.add_row(rowb);
                for (minion : minions) {
                    x = minion^.x_;
                    sumx = LP:Expr + counts[x] - minion^.xa_ - minion^.xb_;
                    rowax = suma + LP:gt + sumx;
                    rowbx = sumb + LP:gt + sumx;
                    lp.add_row(rowax);
                    lp.add_row(rowbx);
                    rowxab = LP:Expr + minion^.xa_ + minion^.xb_ + LP:lte + pools[x];
                    lp.add_row(rowxab);
                }
                lp.solve();
                lp.solve_integral(min_minions);

                /* check no solution. */
                if (lp.solved_ == false) {
                    continue;
                }

                /* every solution is better. */
                villaina = a;
                villainb = b;
                min_minions = lp.get_minimum();
                min_ab = lp.get_value(ab);
                min_ba = lp.get_value(ba);
                min_xa = 0;
                min_xb = 0;
                for (minion : minions) {
                    min_xa += lp.get_value(minion^.xa_);
                    min_xb += lp.get_value(minion^.xb_);
                }
            }
        }

        /* no strategic winner. */
        if (villaina < 0) {
            return;
        }

        namea = candidates^[villaina].name_;
        nameb = candidates^[villainb].name_;
        String namex;
        need_comma = false;
        for (minion : minions) {
            if (need_comma) {
                namex += ",";
            }
            namex += candidates^[minion^.x_].name_;
        }
        need_comma = false;
        s = String + " " + name_ + ":";
        if (min_ab > 0) {
            need_comma = true;
            s += String + " " + min_ab + " " + namea + " votes switch to " + nameb;
        }
        if (min_ba > 0) {
            if (need_comma) {
                s += ",";
            }
            need_comma = true;
            s += String + " " + min_ba + " " + nameb + " votes switch to " + namea;
        }
        if (min_xa > 0) {
            if (need_comma) {
                s += ",";
            }
            need_comma = true;
            s += String + " " + min_xa + " " + namex + " votes switch to " + namea;
        }
        if (min_xb > 0) {
            if (need_comma) {
                s += ",";
            }
            need_comma = true;
            s += String + " " + min_xb + " " + namex + " votes switch to " + nameb;
        }
        s += ".";
        println(s);

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

        println(String + " " + name_ + ": " + fmt*wins + "% ("  + nvillains_ + ")"
            + " strategic voters: " + fmt*coalition_pct + "% (" + coalition_size + ")");
        if (kExplainStrategies) { void;
            println("  W>A pairwise. Arrange AB runoff. A,B voters shift.");
            println("  Non-A,B,W voters who prefer A>W switch to A or B.");
        }
    }
}
