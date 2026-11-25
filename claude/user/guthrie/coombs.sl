/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
coombs electoral method.
*/

import slids.string;
import slids.vector;
import coombs;
import method;
import runner;
import settings;
alias slids;

/*
find the coombs winner.
*/
ElectoralMethod : CoombsMethod() {

    _() { }
    virtual ~() { }
    virtual void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;
        nvoters = g_runner.electorate_.nvoters_;

        winner_ = -1;
        majority = nvoters/2 + 1;

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
        for (nc : ncandidates..>1-1) {
            /* clear the vote counts. */
            for (i : 0..ncandidates) {
                counts[i] = 0;
            }

            /* count the first place votes. */
            for (rc : ranked_choices) {
                int which = rc^.rankings_[0];
                counts[which] += rc^.size_;
            }

            /* check for a strict majority. */
            for (which : elligible) {
                int count = counts[which];
                if (count >= majority) {
                    elligible[0] = which;
                    break winner_loop;
                }
            }

            /* clear the vote counts. */
            for (i : 0..ncandidates) {
                counts[i] = 0;
            }

            /* count the last place votes. */
            int ix = nc - 1;
            for (rc : ranked_choices) {
                int which = rc^.rankings_[ix];
                counts[which] += rc^.size_;
            }

            /* find the candidates with the most last place votes. */
            int loser = -1;
            int max = -1;
            for (i : 0..nc) {
                int which = elligible[i];
                int count = counts[which];
                /* by rule, last in the list loses ties. */
                if (max <= count) {
                    loser = which;
                    max = count;
                }
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
        } :winner_loop;

        /* winner has a majority or is the last elligible candidate. */
        winner_ = elligible[0];

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
        the objective is to shift A>W votes so A is not eliminated.
        */

        ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;
        nvoters = g_runner.electorate_.nvoters_;

        Rankings order;
        order.reserve(ncandidates);
        Rankings elimination_pool;
        Rankings elimination_order;
        elimination_pool.reserve(ncandidates);
        elimination_order.reserve(ncandidates);
        Vector<LP:Expr> votes;
        votes.reserve(ncandidates);

        /*
        we're looking for the minimum number of strategic
        voters it would take to replace the honest winner
        with a min/maxxing villain candidate.
        */
        villain = -1;
        min_minions = -1;
        majority = nvoters/2 + 1;

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

                /*
                establish an elimination order.
                this is a bit complex.
                put all of the not-a not-b candidates into a pool.
                a bitfield determines which candidates are "in" this stage.
                we cycle through all possible permutations of the currently "in" candidates.
                then increment the bitfield.
                which runs from 0 to 2^N.
                */
                elimination_pool.clear();
                for (c : 0..ncandidates) {
                    if (c == a || c == b) {
                        continue;
                    }
                    elimination_pool.append(c);
                }
                elimination_pool_size = ncandidates - 2;
                elimination_limit = 1 << elimination_pool_size;

                elimination_bitfield = 0;
                elimination_order.clear();
                while() {
                    /* convert the bloc map to a schedule map. */
                    void bloc_map_to_schedule(
                        BlocMapIters^ bloc_map,
                        ScheduleMap^ schedule_map
                    ) {
                        for (it : bloc_map^) {
                            forward = rankings_to_schedule(it^.rankings_, elimination_order);
                            backward = rankings_to_last_first_schedule(it^.rankings_, elimination_order);
                            Rankings key;
                            key.reserve(forward.size() + backward.size());
                            for (r : forward) {
                                key.append(r);
                            }
                            for (r : backward) {
                                key.append(r);
                            }
                            found_it = schedule_map^.find(key);
                            if (found_it == schedule_map^.end()) {
                                SizeVar sv;
                                sv.size_ = it^.bloc_.size_;
                                schedule_map^.insert(key, sv);
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

                    /* begin linear programming. */
                    LP lp;

                    /*
                    add variables.
                    we want to minimize the number of strategic minions.
                    honest minions are free ie no cost ie is_costly=false.
                    */
                    for (it : abw_schedules) {
                        name = String + "h" + it^.schedule_.to_string();
                        /* honest minions. */
                        it^.sv_.var_ = lp.add_var(name, false);
                    }
                    for (it : baw_schedules) {
                        name = String + "h" + it^.schedule_.to_string();
                        /* honest minions. */
                        it^.sv_.var_ = lp.add_var(name, false);
                    }
                    /* strategic minions. */
                    name = String + "s" + elimination_order.to_string();
                    ab_var = lp.add_var(name + "AB", true);
                    ba_var = lp.add_var(name + "BA", true);

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
                    ab_expr += ab_var;
                    ba_expr += ba_var;

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

                    /* how many rounds until A must have a majority? */
                    nrounds int = elimination_order.size();

                    /*
                    n+1 rounds.
                    A must have a strict majority in the last round.
                    no other eligible candidate may have a majority in any other round.
                    */
                    eligible = (1<<ncandidates) - 1;
                    for (round : 0..<=nrounds) {
                        /* last round. */
                        if (round == nrounds) {
                            /* count A votes. */
                            LP:Expr expr;
                            for (it : wa_schedules) {
                                which = it^.schedule_[round];
                                if (which == a) {
                                    expr += it^.sv_.size_;
                                }
                            }
                            for (it : abw_schedules) {
                                which = it^.schedule_[round];
                                if (which == a) {
                                    expr += LP:Expr + it^.sv_.var_;
                                }
                            }
                            /* A must have strict majority. */
                            row = expr + ab_var + LP:gte + majority;
                            lp.add_row(row);
                        } else {
                            /* count all other votes. */
                            votes.clear();
                            votes.resize(ncandidates);
                            for (it : wa_schedules) {
                                which = it^.schedule_[round];
                                if (which != a) {
                                    votes[which] += it^.sv_.size_;
                                }
                            }
                            for (it : abw_schedules) {
                                which = it^.schedule_[round];
                                if (which != a) {
                                    votes[which] += LP:Expr + it^.sv_.var_;
                                }
                            }
                            for (it : baw_schedules) {
                                which = it^.schedule_[round];
                                if (which != a) {
                                    votes[which] += LP:Expr + it^.sv_.var_;
                                }
                            }
                            votes[b] += ba_var;

                            /* no eligible candidate may have a majority. */
                            for (c : 0..ncandidates) {
                                if (c == a) {
                                    continue;
                                }
                                bit = 1 << c;
                                if (eligible & bit) {
                                    row = votes[c] + LP:lt + majority;
                                    lp.add_row(row);
                                }
                            }
                        }

                        /* the first round does not eliminate a candidate. */
                        if (round > 0) {
                            /* the vote against schedule is after the vote for schedule. */
                            eround = round - 1;
                            idx = eround + ncandidates;

                            /* count all against votes. */
                            eliminated = elimination_order[eround];
                            votes.clear();
                            votes.resize(ncandidates);
                            for (it : wa_schedules) {
                                which = it^.schedule_[idx];
                                votes[which] += it^.sv_.size_;
                            }
                            for (it : abw_schedules) {
                                which = it^.schedule_[idx];
                                votes[which] += LP:Expr + it^.sv_.var_;
                            }
                            for (it : baw_schedules) {
                                which = it^.schedule_[idx];
                                votes[which] += LP:Expr + it^.sv_.var_;
                            }
                            votes[eliminated] += ab_var;
                            votes[eliminated] += ba_var;

                            /* remove the eliminated candidate. */
                            bit = 1 << eliminated;
                            eligible &= ~bit;

                            /*
                            the eliminated candidate must have more votes against
                            than any other elligible candidate.
                            */
                            for (c : 0..ncandidates) {
                                bit = 1 << c;
                                if (eligible & bit) {
                                    row = votes[c] + lt_or_lte(c, eliminated) + votes[eliminated];
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

                    /* cycle through all possible permutations. */
                    done = permute(elimination_order);
                    if (done == false) {
                        continue;
                    }

                    /* update the "in" candidates. */
                    ++elimination_bitfield;
                    if (elimination_bitfield >= elimination_limit) {
                        break;
                    }

                    /* copy the new "in" candidates to the order. */
                    elimination_order.clear();
                    for (i : 0..elimination_pool_size) {
                        bit = 1 << i;
                        if (elimination_bitfield & bit) {
                            which = elimination_pool[i];
                            elimination_order.append(which);
                        }
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
