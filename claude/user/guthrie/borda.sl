/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
borda count electoral method.
*/

import+ slids.algorithm;
import slids.string;
import+ slids.vector;
import borda;
import method;
import runner;
import settings;
alias slids;

/*
borda count 0 for first, 1 for second, ...
lowest total wins.
*/
ElectoralMethod : BordaMethod() {

    _() { }
    virtual ~() { }
    virtual void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;

        /* initialize number of wins for each candidate. */
        Vector<int> counts;
        counts.reserve(ncandidates);
        counts.resize(ncandidates);
        for (i : 0..ncandidates) {
            counts[i] = 0;
        }

        /* for each voter bloc. */
        for (it : bloc_map^) {
            for (i : 0..ncandidates) {
                int which = it^.rankings_[i];
                counts[which] += i * it^.bloc_.size_;
            }
        }

        /* find the lowest count. */
        winner_ = -1;
        int min = 0x7FFFFFFF;
        for (i : 0..ncandidates) {
            int count = counts[i];
            if (min > count ) {
                winner_ = i;
                min = count;
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
        /*
        W is the honest winner.
        A is the villain.
        a majority prefer W>A.
        the objective is to shift A>W votes so
        A has the smallest borda count.
        */

        ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;

        Rankings order;
        order.reserve(ncandidates);
        Vector<int> counts;
        counts.resize(ncandidates);
        Vector<LP:Expr> borda_expr;
        borda_expr.reserve(ncandidates);

        HonestMinion(
            LP:Var var_,
            BlocMapIter bmit_
        ) {
        }
        Vector<HonestMinion> honest_minions;

        StrategicMinion(
            LP:Var var_,
            Rankings rankings_
        ) {
        }
        Vector<StrategicMinion> strategic_minions;

        /*
        we're looking for the minimum number of strategic
        voters it would take to replace the honest winner
        with a min/maxxing villain candidate.
        */
        villain = -1;
        min_minions = -1;

        /*
        search candidates by lowest borda count.
        the almost-won candidate is most likely to be the cheapest villain.
        */
        SearchOrder(
            int count_,
            int idx_
        ) {
            bool op<(SearchOrder^ rhs) {
                return (count_ < rhs^.count_);
            }
        }
        Vector<SearchOrder> search;
        search.resize(ncandidates);
        for (i : 0..ncandidates) {
            search[i].idx_ = i;
        }

        /* compute the borda counts. */
        for (it : bloc_map^) {
            for (i : 1..ncandidates) {
                c = it^.rankings_[i];
                search[c].count_ += i * it^.bloc_.size_;
            }
        }

        /* sort them. */
        quicksort(search);

        /* try every candidate in the search order. */
        for (srch : search) {
            a = srch^.idx_;
            if (a == winner_) {
                continue;
            }

            /*
            the pool of minions is all A>W blocs.
            some are honest.
            some vote strategically.
            they all vote A first.
            we need an lp:var for all honest minions ~N!/2.
            and an lp:var for all strategies (N-1)!
            honest minions are constrained by the bloc size.
            honest + strategic minions = pool size.
            borda count for A <? borda count for all other candidates.
            minimize strategic minions.

            claude says this is an inefficient method.
            use a N-1 x N-1 table of strategic minions instead.
            one dimension is rank order.
            the other dimension is the candidate.
            rows and columns sum to the number of strategic voters.
            */

            /* clear the borda counts. */
            for (p^ : counts) {
                p^ = 0;
            }

            /* begin linear programming. */
            LP lp;

            /*
            add honest minion variables and bloc size.
            accumulate non-minion border counts.
            */
            honest_minions.clear();
            for (it : bloc_map^) {
                for (which : it^.rankings_) {
                    if (which == a) {
                        /* A>W, add a variable and save the size. */
                        minion = honest_minions.append();
                        name = String + "h" + it^.rankings_.to_string();
                        minion^.var_ = lp.add_var(name, false);
                        minion^.bmit_ = it;
                        break;
                    }
                    if (which == winner_) {
                        /* W>A, add the borda counts to the candidates. */
                        for (i : 1..ncandidates) {
                            c = it^.rankings_[i];
                            counts[c] += i * it^.bloc_.size_;
                        }
                        break;
                    }
                }
            }

            /* initialize the order */
            order.clear();
            for (c : 0..ncandidates) {
                if (c == a) {
                    continue;
                }
                order.append(c);
            }

            /* add strategic minion variables. */
            strategic_minions.clear();
            while() {
                minion = strategic_minions.append();
                minion^.rankings_.reserve(ncandidates);
                minion^.rankings_.append(a);
                for (c : order) {
                    minion^.rankings_.append(c);
                }
                name = String + candidates^[a].name_ + order.to_string();
                minion^.var_ = lp.add_var(name, true);

                done = permute(order, ncandidates-1);
                if (done) {
                    break;
                }
            }

            /* constraint: honest minions limited by bloc size. */
            for (minion : honest_minions) {
                it = minion^.bmit_;
                row = LP:Expr + minion^.var_ + LP:lte + it^.bloc_.size_;
                lp.add_row(row);
            }

            /* constraint: honest minions + strategic minions = pool size. */
            LP:Expr honest_expr;
            pool_size = 0;
            for (minion : honest_minions) {
                honest_expr += minion^.var_;
                pool_size += minion^.bmit_^.bloc_.size_;
            }
            LP:Expr strategic_expr;
            for (minion : strategic_minions) {
                strategic_expr += minion^.var_;
            }
            /* add twice cause equality. */
            row = honest_expr + strategic_expr + LP:lte + pool_size;
            lp.add_row(row);
            row = honest_expr + strategic_expr + LP:gte + pool_size;
            lp.add_row(row);

            /* compute the borda counts. */
            borda_expr.clear();
            borda_expr.resize(ncandidates);
            /* add the constant part. */
            for (i : 0..ncandidates) {
                borda_expr[i] += counts[i];
            }
            /* add the honest minion contributions. */
            for (minion : honest_minions) {
                it = minion^.bmit_;
                for (i : 1..ncandidates) {
                    which = it^.rankings_[i];
                    expr = LP:Expr + minion^.var_;
                    borda_expr[which] += expr * i;
                }
            }
            /* add the strategic minion contributions. */
            for (minion : strategic_minions) {
                for (i : 1..ncandidates) {
                    which = minion^.rankings_[i];
                    expr = LP:Expr + minion^.var_;
                    borda_expr[which] += expr * i;
                }
            }
            /* add the equations. */
            for (c : 0..ncandidates) {
                if (c == a) {
                    continue;
                }
                row = LP:Expr + borda_expr[a] + lt_or_lte(a,c) + borda_expr[c];
                lp.add_row(row);
            }

            /* find a solution. */
            lp.solve();
            lp.solve_integral(min_minions);

            /* every solution is better. save it. */
            if (lp.solved_) {
                villain = a;
                min_minions = lp.get_minimum();
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
            println("  W>A pairwise. BC+ voters who prefer A>W eliminate W early.");
        }
    }
}
