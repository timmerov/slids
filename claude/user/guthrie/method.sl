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
