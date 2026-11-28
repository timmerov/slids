/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
bucklin electoral method.

for strategic voting, we need:
W>A pairwise.
we need the election to end in round R.
no candidate may have a majority in any round before R.
A must have the greatest majority in round R.
the pool of A>W voters vote honestly or strategically.
R strategy
1 A
2 BA CA DA WA?
3 BCA BDA CBA CDA DBA DCA
*/

import slids.string;
import+ slids.vector;
import bucklin;
import method;
import runner;
import settings;
alias slids;

/*
bucklin voting.
while no greatest majority winner, accumulate the next choices.
*/
ElectoralMethod : BucklinMethod() {

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

        /* potentially accumulate over all rankings. */
        for (i : 0..ncandidates) {
            /* no winner. */
            winner_ = -1;

            /* sum the votes. */
            for (it : bloc_map^) {
                int which = it^.rankings_[i];
                counts[which] += it^.bloc_.size_;
            }

            /* find the index with the maximum count. */
            int max = -1;
            for (k : 0..ncandidates) {
                int c = counts[k];
                if (c > max) {
                    max = c;
                    winner_ = k;
                }
            }

            /* done when they have the greatest majority. */
            if (2*max > nvoters) {
                break;
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

    /*
    analyze strategic voting.

    A needs the greatest majority in round R.
    no candidate may have a majority in any round before R.
    the W>A may give W a majority without any help from the A>W voters.
    this limits R.

    the A>W blocs are the minions.
    some vote honestly.
    some vote strategically.
    in round R, all minions approve A.
    in prior rounds, some number of minions approve not-A candidates.
    including W.
    the sum is the number of strategic voters.
    */
    virtual void strategic_voting() {

        ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;
        nvoters = g_runner.electorate_.nvoters_;

        majority = nvoters/2 + 1;

        Vector<int> counts;
        counts.resize(ncandidates);
        Vector<BlocMapIter> wa_blocs;
        Vector<BlocMapIter> aw_blocs;
        Vector<LP:Expr> approval_exprs;
        approval_exprs.reserve(ncandidates);

        HonestMinion(
            LP:Var var_,
            BlocMapIter bmit_
        ) {
        }
        Vector<HonestMinion> honest_minions;
        Vector<LP:Var> strategic_minions;

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

            /* separate the W>A and A>W blocs. */
            wa_blocs.clear();
            aw_blocs.clear();
            for (it : bloc_map^) {
                for (which : it^.rankings_) {
                    if (which == a) {
                        aw_blocs.append(it);
                        break;
                    }
                    if (which == winner_) {
                        wa_blocs.append(it);
                        break;
                    }
                }
            }

            /* clear the counts. */
            for (p^ : counts) {
                p^ = 0;
            }

            /* count W votes. */
            for (it : wa_blocs) {
                for (r : 0..ncandidates) {
                    which = it^.rankings_[r];
                    if (which == winner_) {
                        counts[r] +=  it^.bloc_.size_;
                        break;
                    }
                }
            }

            /*
            find the final round.
            in the last round, every voter approves every candidate.
            skip it.
            */
            final_round = 0;
            wvotes = 0;
            for (final_round : 0..(ncandidates-1)) {
                wvotes += counts[final_round];
                if (wvotes >= majority) {
                    break;
                }
            }

            /* done if W has a majority in the first round. */
            if (final_round == 0) {
                continue;
            }

            /* for each round. */
            for (round_a_wins : 0..<=final_round) {

                /* begin linear programming. */
                LP lp;

                /* create variables and bloc sizes for honest minions. */
                honest_minions.clear();
                for (it : aw_blocs) {
                    minion = honest_minions.append();
                    name = String + "h" + it^.rankings_.to_string();
                    minion^.bmit_ = it;
                    minion^.var_ = lp.add_var(name, false);
                }

                /*
                add variables for strategic minions.
                one variable per candidate except A per round after the first.
                */
                strategic_minions.clear();
                idx = 0;
                for (round : 0..round_a_wins) {
                    for (c : 0..ncandidates) {
                        if (c == a) {
                            continue;
                        }
                        minion = strategic_minions.append();
                        name = String + "s" + idx;
                        minion^ = lp.add_var(name);
                        ++idx;
                    }
                }

                /* all strategic minions approve A in the final round. */
                strategic_minions_var = lp.get_cost_var();

                /* constraint: honest minions limited by bloc size. */
                for (minion : honest_minions) {
                    it = minion^.bmit_;
                    row = LP:Expr + minion^.var_ + LP:lte + it^.bloc_.size_;
                    lp.add_row(row);
                }

                /*
                constraint: all strategic minions vote every round.
                make a >= cycle.
                S >= col0 >= col1 >= S
                */
                if (round_a_wins > 0) {
                    prev_expr = LP:Expr + strategic_minions_var;
                    idx = 0;
                    for (i : 0..round_a_wins) {
                        LP:Expr this_expr;
                        for (k : 0..(ncandidates-1)) {
                            this_expr += strategic_minions[idx];
                            ++idx;
                        }
                        row = prev_expr + LP:gte + this_expr;
                        lp.add_row(row);
                        prev_expr <-- this_expr;
                    }
                    row = prev_expr + LP:gte + strategic_minions_var;
                    lp.add_row(row);
                }

                /*
                constraint: strategic voters cannot repeat votes for the same candidate.
                like the borda matrix.
                but it's <= instead of equality.
                */
                if (round_a_wins > 0) {
                    for (k : 0..(ncandidates-1)) {
                        LP:Expr expr;
                        idx = k;
                        for (i : 0..round_a_wins) {
                            expr += strategic_minions[idx];
                            idx += ncandidates - 1;
                        }
                        row = expr + LP:lte + strategic_minions_var;
                        lp.add_row(row);
                    }
                }

                /* constraint: honest minions + strategic minions = pool size. */
                expr = LP:Expr + strategic_minions_var;
                pool_size = 0;
                for (minion : honest_minions) {
                    it = minion^.bmit_;
                    pool_size += it^.bloc_.size_;
                    expr += minion^.var_;
                }
                /* add twice cause equality. */
                row = expr + LP:lte + pool_size;
                lp.add_row(row);
                row = expr + LP:gte + pool_size;
                lp.add_row(row);

                /*
                add votes for each candidate by round.
                check for no majority or greatest majority.
                */
                approval_exprs.clear();
                approval_exprs.resize(ncandidates);
                for (round : 0..<=round_a_wins) {

                    /* count the W>A votes. */
                    for (it : wa_blocs) {
                        which = it^.rankings_[round];
                        approval_exprs[which] += it^.bloc_.size_;
                    }

                    /* count the honest minion votes. */
                    for (minion : honest_minions) {
                        it = minion^.bmit_;
                        which = it^.rankings_[round];
                        approval_exprs[which] += minion^.var_;
                    }

                    /* count the strategic minion votes. */
                    if (round < round_a_wins) {
                        which = round * (ncandidates-1);
                        for (c : 0..ncandidates) {
                            if (c == a) {
                                continue;
                            }
                            approval_exprs[c] += strategic_minions[which];
                            ++which;
                        }
                    }

                    /* no candidate may have a majority in any round before the round where A wins. */
                    if (round < round_a_wins) {
                        /* no majority. */
                        for (c : 0..ncandidates) {
                            row = approval_exprs[c] + LP:lt + majority;
                            lp.add_row(row);
                        }
                    } else {
                        /* A has greatest majority this round. */
                        approval_exprs[a] += strategic_minions_var;
                        for (c : 0..ncandidates) {
                            if (c == a) {
                                /* A must have majority. */
                                row = approval_exprs[a] + LP:gte + majority;
                                lp.add_row(row);
                            } else {
                                /* A must have the greatest majority. */
                                row = approval_exprs[a] + LP:gt + approval_exprs[c];
                                lp.add_row(row);
                            }
                        }
                    }
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
            println("  W>A pairwise. BC+ voters who prefer A>W vote A earlier.");
        }
    }
}
