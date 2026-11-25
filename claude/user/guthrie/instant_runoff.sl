/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
instant runoff electoral method.
*/

import slids.string;
import slids.vector;
import instant_runoff;
import method;
import runner;
import settings;
alias slids;

/*
instant runoff voting.
*/
ElectoralMethod : InstantRunoffMethod() {

    _() { }
    virtual ~() { }
    virtual void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;
        int nvoters = g_runner.electorate_.nvoters_;

        winner_ = -1;

        /* copy the voter blocs. */
        nblocs int = bloc_map^.size();
        RankedChoices ranked_choices;
        ranked_choices.reserve(nblocs);
        for (it : bloc_map^) {
            ranked_choice = ranked_choices.append();
            ranked_choice^.rankings_ = it^.rankings_;
            ranked_choice^.size_ = it^.bloc_.size_;
        }

        /* allocate vote counts. */
        Vector<int> counts;
        counts.reserve(ncandidates);
        counts.resize(ncandidates);

        /* initialize a list of elligible candidates. */
        Vector<int> elligible;
        elligible.reserve(ncandidates);
        elligible.resize(ncandidates);
        for (i : 0..ncandidates) {
            elligible[i] = i;
        }

        /* repeat until we have a winner. */
        for (nc : ncandidates..>0-1) {
            /* clear the vote counts. */
            for (i : 0..ncandidates) {
                counts[i] = 0;
            }

            /* count the first place votes. */
            for (rc : ranked_choices) {
                int which = rc^.rankings_[0];
                counts[which] += rc^.size_;
            }

            /* find the candidates with the most and least first place votes. */
            int winner = -1;
            int loser = -1;
            int max = -1;
            int min = 0x7FFFFFFF;
            for (i : 0..nc) {
                int which = elligible[i];
                int count = counts[which];
                /* by rule, first in the list wins ties. */
                if (max < count) {
                    winner = which;
                    max = count;
                }
                /* by rule, last in the list loses ties. */
                if (min >= count) {
                    loser = which;
                    min = count;
                }
            }

            /* check for a majority winner. */
            if (2*max > nvoters) {
                winner_ = winner;
                break;
            }

            /* remove the loser from the rankings. */
            for (rc : ranked_choices) {
                for (i : 0..nc) {
                    int which = rc^.rankings_[i];
                    if (which == loser) {
                        rc^.rankings_.remove(i);
                        break;
                    }
                }
            }

            /* remove the loser from the candidate list. */
            for (i : 0..nc) {
                int which = elligible[i];
                if (which == loser) {
                    elligible.remove(i);
                    break;
                }
            }
        }

        if (winner_ < 0) {
            println("=tsc= uh oh! irv failed to find winner." );
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
        A gets a majority before being eliminated.
        */

        ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;

        Rankings order;
        order.reserve(ncandidates);
        Rankings elimination_order;
        elimination_order.reserve(ncandidates);

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
            find a finalist B who loses to A in the final round head to head.
            */
            for (b : 0..ncandidates) {
                /*
                can't rely on W>A, A>B therefore W>B.
                cause there could be a cycle.
                and this would allow B to be W.
                */
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

                /* find blocs that prefer: W>A; A>W and A>B; A>W and B>A. */
                BlocMapIters wa_blocs;
                BlocMapIters abw_blocs;
                BlocMapIters baw_blocs;
                for (it : bloc_map^) {
                    for (which : it^.rankings_) {
                        if (which == winner_) {
                            wa_blocs.append(it);
                            continue bloc_loop;
                        }
                        if (which == a) {
                            break;
                        }
                    }
                    for (which : it^.rankings_) {
                        if (which == a) {
                            abw_blocs.append(it);
                            continue bloc_loop;
                        }
                        if (which == b) {
                            baw_blocs.append(it);
                            continue bloc_loop;
                        }
                    }
                } :bloc_loop;

                /* generate and separate permutation orders with A>B and B>A. */
                alias Orders = Vector<Rankings>;
                Orders ab_orders;
                Orders ba_orders;
                order.clear();
                for (c : 0..ncandidates) {
                    order.append(c);
                }
                while() {
                    for (c : order) {
                        if (c == a) {
                            ab_orders.append(order);
                            break;
                        }
                        if (c == b) {
                            ba_orders.append(order);
                            break;
                        }
                    }
                    done = permute(order);
                    if (done) {
                        break;
                    }
                }

                /* establish an elimination order. */
                elimination_order.clear();
                for (c : 0..ncandidates) {
                    if (c == a || c == b) {
                        continue;
                    }
                    elimination_order.append(c);
                }
                elimination_order.append(b);
                elimination_order.append(a);

                while () {
                    /* convert the bloc map to a schedule map. */
                    void bloc_map_to_schedule(
                        BlocMapIters^ bloc_map,
                        ScheduleMap^ schedule_map
                    ) {
                        for (it : bloc_map^) {
                            schedule = rankings_to_schedule(it^.rankings_, elimination_order);
                            found_it = schedule_map^.find(schedule);
                            if (found_it == schedule_map^.end()) {
                                SizeVar sv;
                                sv.size_ = it^.bloc_.size_;
                                schedule_map^.insert(schedule, sv);
                            } else {
                                found_it^.sv_.size_ += it^.bloc_.size_;
                            }
                        }
                    }
                    /*
                    wa_schedules holds this size of the bloc. no variable.
                    abw_schedules and baw_schedules hold both the size of the bloc
                    and the variable which is number of minions who vote honestly.
                    */
                    ScheduleMap wa_schedules;
                    ScheduleMap abw_schedules;
                    ScheduleMap baw_schedules;
                    bloc_map_to_schedule(wa_blocs, wa_schedules);
                    bloc_map_to_schedule(abw_blocs, abw_schedules);
                    bloc_map_to_schedule(baw_blocs, baw_schedules);

                    /*
                    convert orders to schedule map.
                    ab_schedules and ba_schedules hold the variable which is the number
                    of minions who vote strategically.
                    */
                    void orders_to_schedule(
                        Orders^ orders,
                        ScheduleMap^ schedule_map
                    ) {
                        for (it : orders^) {
                            schedule = rankings_to_schedule(it, elimination_order);
                            found_it = schedule_map^.find(schedule);
                            if (found_it == schedule_map^.end()) {
                                schedule_map^.insert(schedule, SizeVar);
                            }
                        }
                    }
                    ScheduleMap ab_schedules;
                    ScheduleMap ba_schedules;
                    orders_to_schedule(ab_orders, ab_schedules);
                    orders_to_schedule(ba_orders, ba_schedules);

                    /* begin linear programming. */
                    LP lp;

                    /*
                    add variables.
                    we want to minimize the number of strategic minions.
                    honest minions are free ie no cost ie is_costly=false.
                    */
                    for (it : abw_schedules) {
                        name = String + "h" + it^.schedule_.to_string();
                        /* honest minion. */
                        it^.sv_.var_ = lp.add_var(name, false);
                    }
                    for (it : baw_schedules) {
                        name = String + "h" + it^.schedule_.to_string();
                        /* honest minion. */
                        it^.sv_.var_ = lp.add_var(name, false);
                    }
                    for (it : ab_schedules) {
                        name = String + "s" + it^.schedule_.to_string();
                        /* strategic minion. */
                        it^.sv_.var_ = lp.add_var(name, true);
                    }
                    for (it : ba_schedules) {
                        name = String + "s" + it^.schedule_.to_string();
                        /* strategic minion. */
                        it^.sv_.var_ = lp.add_var(name, true);
                    }

                    /*
                    constraint: abw and baw are limited by the bloc size.
                    constraint: total minions defecting from all honest blocks
                    equals total minions landing on all strategic blocks.
                    since that's an equality, we need to add it twice with >= and <=.
                    */
                    ab_pool = 0;
                    ba_pool = 0;
                    LP:Expr ab_expr;
                    LP:Expr ba_expr;
                    for (it : abw_schedules) {
                        /* constraint: honest minions <= bloc size. */
                        row = LP:Expr + it^.sv_.var_ + LP:lte + it^.sv_.size_;
                        lp.add_row(row);
                        ab_pool += it^.sv_.size_;
                        ab_expr += it^.sv_.var_;
                    }
                    for (it : baw_schedules) {
                        /* constraint: honest minions <= bloc size. */
                        row = LP:Expr + it^.sv_.var_ + LP:lte + it^.sv_.size_;
                        lp.add_row(row);
                        ba_pool += it^.sv_.size_;
                        ba_expr += it^.sv_.var_;
                    }
                    for (it : ab_schedules) {
                        ab_expr += it^.sv_.var_;
                    }
                    for (it : ba_schedules) {
                        ba_expr += it^.sv_.var_;
                    }
                    /*
                    constraint: honest minions + strategic minions = pool size.
                    since this is an equality, it must be added twice.
                    */
                    ab_row = ab_expr + LP:lte + ab_pool;
                    ba_row = ba_expr + LP:lte + ba_pool;
                    lp.add_row(ab_row);
                    lp.add_row(ba_row);
                    ab_row = ab_expr + LP:gte + ab_pool;
                    ba_row = ba_expr + LP:gte + ba_pool;
                    lp.add_row(ab_row);
                    lp.add_row(ba_row);

                    /* for each round */
                    elligible = (1<<ncandidates) - 1;
                    for (round : 0..(ncandidates-1)) {
                        /* vote totals */
                        Vector<LP:Expr> votes;
                        votes.resize(ncandidates);
                        for (it : wa_schedules) {
                            which = it^.schedule_[round];
                            votes[which] += it^.sv_.size_;
                        }
                        for (it : abw_schedules) {
                            which = it^.schedule_[round];
                            votes[which] += LP:Expr + it^.sv_.var_;
                        }
                        for (it : baw_schedules) {
                            which = it^.schedule_[round];
                            votes[which] += LP:Expr + it^.sv_.var_;
                        }
                        for (it : ab_schedules) {
                            which = it^.schedule_[round];
                            /* minions land in this strategic bloc. */
                            votes[which] += it^.sv_.var_;
                        }
                        for (it : ba_schedules) {
                            which = it^.schedule_[round];
                            /* minions land in this strategic bloc. */
                            votes[which] += it^.sv_.var_;
                        }

                        /* remove the eliminated candidate. */
                        eliminated = elimination_order[round];
                        bit = 1 << eliminated;
                        elligible &= ~bit;

                        /* add the vote total equations. */
                        for (i : 0..ncandidates) {
                            bit = 1<<i;
                            if (bit & elligible) {
                                expr = votes[i] + gt_or_gte(i,eliminated) + votes[eliminated];
                                lp.add_row(expr);
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

                    done = permute(elimination_order, ncandidates-2);
                    if (done) {
                        break;
                    }
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
            println("  W>A pairwise. BC+ voters who prefer A>W eliminate W early.");
        }
    }
}
