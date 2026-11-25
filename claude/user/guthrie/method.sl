/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
electoral method base class.
*/

import slids.string;
import method;
import runner;
import settings;
alias slids;

/*
base class for electoral methods.
every derived class must define find_winner.
*/
ElectoralMethod() {

    global int g_nmethods = 0;

    _() {
        ++g_nmethods;
    }
    virtual ~() {}

    /* pretty print the winner. */
    virtual void print_winner() {
        Format fmt;
        fmt.min_width_ = kWidth;
        str = String + " " + name_;
        result = result_to_string(g_runner.guthrie_.winner_, winner_);
        winner_name = g_runner.candidates_[winner_].name_;
        println(String + fmt*str + ": " + winner_name + " " + result);
    }

    /* calculate and correlation. */
    void calculate_results(
    ) {
        /* calculate satisfaction. */
        ntrials float64 = g_runner.ntrials_;
        float64 average_utility = total_utility_ / ntrials;
        satisfaction_ = calculate_satisfaction(average_utility, g_runner.accumulated_);

        /* calculate correlation. */
        correlation_ = 100.0 * (float64=is_winner_) / ntrials;
    }

    /* pretty print satisfaction. */
    virtual void print_satisfaction() {
        Format fmt;
        fmt.min_width_ = kWidth;
        str = String + " " + name_;
        println(String + fmt*str + ": " + satisfaction_);
    }

    /* pretty print correlation. */
    virtual void print_correlation() {
        Format fmt;
        fmt.min_width_ = kWidth;
        str = String + " " + name_;
        println(String + fmt*str + ": " + correlation_ + "%");
    }

    /* analyze strategic voting. */
    virtual void strategic_voting() {
        /* do nothing. */
    }

    /* pretty print strategic voting summary. */
    virtual void print_strategic_results() {
        /* do nothing. */
    }
}

/* if winner is expected then "pass" otherwise "=FAIL=" or "n/a". */
(const char)[] result_to_string(int winner, int expected) {
    if (expected < 0) {
        return "n/a";
    }
    if (winner == expected) {
        return "pass";
    }
    return "=FAIL=";
}

/* calculate satisfaction from utility and metrics. */
float64 calculate_satisfaction(
    float64 utility,
    SatisfactionMetrics^ metric
) {
    float64 best = metric^.best_;
    float64 average = metric^.average_;
    float64 satisfaction = (utility - average) / (best - average);
    return satisfaction;
}

/*
utility method to return the margin of victory.
this is the number of votes A can surrender and still beat B.
*/
int victory_margin(
    int a,
    int b,
    int votesa,
    int votesb
) {
    margin = votesa - votesb;

    /* apply the tie-breaker rule. */
    if (a > b) {
        --margin;
    }

    return margin;
}

/*
cycle through all possible permutations.
assume it starts in ascending order.
*/
bool permute(slids:Vector<int>^ order, int n = -1) {
    if (n < 0) {
        n = (int=order^.size());
    }
    for (i : (n-2)..>=0 -1) {
        if (order^[i] < order^[i+1]) {
            for (k : (n-1)..>i -1) {
                if (order^[k] > order^[i]) {
                    order^[k] <--> order^[i];
                    for (p=i+1, q=n-1) (p < q) {++p; --q;} {
                        order^[p] <--> order^[q];
                    }
                    return false;
                }
            }
        }
    }
    return true;
}

/*
convert rankings to voting schedule given an elimination order.
*/
Rankings rankings_to_schedule(
    Rankings^ rankings,
    Rankings^ elimination_order
) {
    /* there may be more rankings than elimination orders. */
    nr int = rankings^.size();
    neo int = elimination_order^.size();
    Rankings schedule;
    schedule.reserve(nr);
    elligible uint = (1<<nr) - 1;
    for (i : 0..nr) {
        for (which : rankings^) {
            uint bit = 1 << which;
            if (bit & elligible) {
                schedule.append(which);
                break;
            }
        }
        if (i < neo) {
            uint bit = 1 << elimination_order^[i];
            elligible &= ~bit;
        }
    }
    return schedule;
}

/*
convert rankings to last-first voting schedule given an elimination order.
*/
Rankings rankings_to_last_first_schedule(
    Rankings^ rankings,
    Rankings^ elimination_order
) {
    /* there may be more rankings than elimination orders. */
    nr int = rankings^.size();
    neo int = elimination_order^.size();
    Rankings schedule;
    schedule.reserve(nr);
    elligible uint = (1<<nr) - 1;
    for (i : 0..nr) {
        for (k : (nr-1)..>=0 -1) {
            which = rankings^[k];
            uint bit = 1 << which;
            if (bit & elligible) {
                schedule.append(which);
                break;
            }
        }
        if (i < neo) {
            uint bit = 1 << elimination_order^[i];
            elligible &= ~bit;
        }
    }
    return schedule;
}

/*
utility functions for breaking ties in linear programming.
*/
LP:Comparison gt_or_gte(int a, int b) {
    /*
    a wants to win this contest.
    a wins ties iff a<b.
    */
    if (a < b) {
        /* a wins ties. */
        return LP:gte.Comparison;
    }
    /* a loses ties. */
    return LP:gt.Comparison;
}

/*
utility functions for breaking losing ties in linear programming.
*/
LP:Comparison lt_or_lte(int a, int b) {
    /*
    this is used by coombs.
    the candidate with the most votes loses.

    if a votes < b votes then b loses.
    if a<b and a votes == b votes then choose b to lose.
    if a>b and a votes == b votes then choose a to lose.
    */
    if (a < b) {
        return LP:lte.Comparison;
    }
    return LP:lt.Comparison;
}
