/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
analyze guthrie voting.

assumptions:
1. voter engagement is limited.
they have enough capacity to pick their favorite candidate.
but that's it.
they don't have the time or energy or motivation to score or order candidates.
they are effectively stupid.
2. a candidate with a majority of the votes wins.
3. we want more than two parties.
4. third parties cannot split the vote.
5. protest votes should be safe.
6. voters should vote honestly.
7. candidates are not stupid.
they are informed, motivated, and have time and energy to fully engage the system.
they can score and order the other candidates.
8. candidates may vote strategically.
9. the winning candidate should maximize total satisfaction (utility) of the voters.
10. voters know how all the candidates score/rank each other.
11. candidates should not be harmed by getting more votes from voters.
independence of irrelevant alternatives and participation criteria.


guthrie voting works like this:

phase 1:
the primary reduces a large number of candidates to a manageable number.
but not less than 3.
we pick a number of primary candidates equal to the cube root of the number of voters.
voters cast a single vote for their favorite primary candidate.
excess candidates are removed by single transferable vote.
other culling methods are acceptable.

phase 2:
voters cast a single vote for their favorite candidate.
working example: A,B,D received 35,30,25,10 votes respectively.
A prefers B > C > D.
B prefers A > C > D.
C prefers D > B > A
D prefers C > B > A

phase 3:
find the winner by coombs method.
round 1:
A casts 35 votes for A.
B casts 30 votes for B.
C casts 25 votes for C.
D casts 10 votes for D.
totals: A=35, B=30, C=25, D=10.
no one has a majority.
proceed to the elimination phase.
A casts 35 votes against D.
B casts 30 votes against D.
C casts 25 votes against A.
D casts 10 votes against A.
totals: A=35, B=0, C=0, D=65.
D is eliminated from the ballot but not the voting.
round 2:
A casts 35 votes for A.
B casts 30 votes for B.
C casts 25 votes for C.
D casts 10 votes for C.
totals: A=35, B=30, C=35, D=0.
no one has a majority.
proceed to the elimination phase.
A casts 35 votes against C.
B casts 30 votes against C.
C casts 25 votes against A.
D casts 10 votes against A.
totals: A=35, B=0, C=65, D=0.
C is eliminated from the ballot but not the voting.
round 3;
A casts 35 votes for A.
B casts 30 votes for B.
C casts 25 votes for B.
D casts 10 votes for B.
totals: A=35, B=65, C=0, D=0.
B has a majority.
B wins the election.


there are several ways to model the electorate:
see electorate.h for details.

for single axis and uniform or random distribution...
either there's a majority.
or there's a condorcet winner.
we're in a nash equilibrium.
ie there's no incentive for either the voters or the candidates to change their votes.
ie to vote strategically instead of honestly.
an exception would be when a candidate's preference isn't honest.
example: A=40 B=35 C=25 where C for whatever reason prefers A>B.
then A+C eliminates B and A wins in round 2 with votes from B.
but if enough voters can see this coming before the election,
they will also vote dishonestly for their second choice, B.
giving B a majority win in round 1.
so C's strategy for getting a victory for A would be to lie to their constituents.
and vote against their wishes.

ties are difficult to handle.
the order of the candidates never changes.
in case of tie, the first candidate wins.
but there's a gotcha.
sometimes we vote to eliminate a candidate.
the candidate with the most last place votes is eliminated.
in other words, they lose.
in this case, the last candidate "wins" the tie.
when searching for a winner, we take the first candidate with the most votes.
when searching for a loser, we take the last candidate with the most votes.

in the art, the electorate is modeled as voters clustering around positions.
this is usually implemented as the chinese restaurant problem.
there's a dispersion parameter alpha.
N patrons are already seated at tables.
the probability the next patron sits at a table is proportional to how many
patrons are already at the table.
P(patron N+1 sits at table k) = n[k] / (N + alpha)
p(patron N+1 sits by himself) = alpha / (N + alpha)
we place voters this way.
clusters have a position and a standard deviation.
the position ranges from 0.0 to 1.0.
some outstanding questions:
how many clusters do we want?
how does that relate to alpha?

satisfaction is a rather unsatisfying metric.
the best candidate gets a 1.0.
the average candidate gets a 0.0.
what if all candidates are equally good?
if two candidates have equal nearly optimal utility...
and the third has slightly less utility...
then the third candidate has a satisfaction of -2.
even though the third candidate is just as good as the other two.
weird.
what if all candidates are equally horrible?
one of them is going to get satisfaction rating of 1.0.
even though they'd get stomped by a randomly selected voter.
weird.
satisfaction is calculated using maximum and average utility.
i tried using max and average for all voters (expensive),
and all candidates in the primary (not helpful).
so status quo it is.
and more ranting...
when you're doing a summary, the satisfaction isn't to the same scale.
so they can't really be averaged meaningfully.
could try using the worst possible candidate as the baseline.
could try using the median voter/candidate as the baseline.
okay so what we do is this:
average the winner(s), best, and random candidate utilities over all trials.
use the average utilities to compute the satisfaction.
instead of averaging the satisfactions of each trial.

we do not need to check independence when multiple candidates drop out.
guthrie does not have to satisfy independence from irrelevant alternatives.
the occurance is rare.
but is an opportunity for voters to vote strategically.
specifically B wins.
candidate A prefers B.
but a majority of A's voters prefer C.
if A drops out, or equivalently A's voters defect to C, C may win by majority.

where there are many candidates...
and they form a condorcet ordering...
we have a nash equilibrium.
"proof":
a majority coalition can efficiently eliminate all candidates not in the coalition.
thereby ensuring one of the coalition members wins.
in order to form a majority coalition of the left (close to position 0)...
the guthrie winner must be included.
otherwise they don't have a majority of the voters.
same for the right.
the guthrie winner is the choice for the eliminated right minority coalition.
a right majority coalition forms that includes the guthrie winner.
all other candidates of the left coalition are eliminated.
okay.
a split coalition includes candidates left and right of the guthrie winner
and excludes the guthrie winner.
non-coalition members are eliminated.
including the guthrie winner.
a new winner is selected from the left (for example).
coalition members from the right get a worse result.
and therefore would not join such a coalition.
qed.
the interesting cases are when there are condorcet cycles.
which require two or more uncorrelated axes for the issues.

"dispersion" moves candidates towards the center.
cause voters are far more radical than the candidates.
looks like this paper:
https://voting-in-the-abstract.medium.com/voter-satisfaction-efficiency-many-many-results-ad66ffa87c9e
scales the candidate standard deviation from 5% to 100% of the voter standard deviation.
i think we can just move the candidates position centerward by some percentage.
realistic numbers range from 34% to 94%.
with maybe a reasonable guess around 75%.
the apparent effect of dispersion is to amplify the difference between electoral methods.

things done:

handle ties.
check if the winner is the condorcet winner if there is one.
report satisfaction (two different ways) and bayer regret.
find the voter that would be the optimal candidate.
check for independence ie the winner should still win if any other candidate drops out.
check if the most satisfactory candidate wins.
they don't. but that's okay.
these are diabolical cases that no voting system can do better. except maybe range voting.
multiple trials with summarized results,
normalize electorate to range from 0.0 to 1.0.
clustered voters,
multiple issue dimensions,
anti-plurality - winner has fewest last place votes.
bucklin - while no greatest majority, accumulate next choice counts.
dispersion
average utilities then calculate satisfactions instead of vice versa.
two round plurality - top 2 pluralities go head to head.
two round approval - top 2 vote getters go head to head.
coombs' method.
star - score candidates 0 to 5.
the winner has one of the top two highest total scores and...
is scored higher than the the other finalist on more ballots.

things in progress:

things to do:

- non-linear utility or piece-wise linear utility.
- account for candidate quality. this is a random adjustment to utility.
- weight utilities for satisfaction computation.
an election of all near-clones contributes less.
an election with high spread contributes more.
- asymmetric utility we assume U(A,B)=U(B,A) but irl it might not.

voting systems to consider adding:

majority judgement voting - winner has the largest median utility.
ranked robin - condorcet but voters can give candidates equal ranking.
instant pairwise elimination ipe - eliminate the condorcet loser each round.
with tie breaker rules. https://electowiki.org/wiki/Instant_Pairwise_Elimination
semi-random voting - pick a ballot at ranodm. presumably this motivates honest voting.
choose a winner by some good method that's subject to strategic voting.
run-off between the two.
bottom two - irv but the two candidates with the fewest first place votes go head to head.
loser eliminated.

other voting systems worth mentioning:
smith methods - condorcet completion.
maximal lotteries - condorcet completion.
non-smith condorcet.
star - range plus automatic runoff of the top two.
cumulative voting - split 1.0 votes among candidates.
*/

import slids.algorithm;
import slids.dump;
import slids.linpro;
import slids.map;
import slids.math;
import slids.string;
import slids.vector;
import candidate;
import electorate;
import method;
import random;
import runner;
import settings;

/* methods */
import approval;
import approval_runoff;
import borda;
import bucklin;
import candidate_method;
import condorcet;
import coombs;
import instant_runoff;
import plurality;
import plurality_runoff;
import range;
import star;
alias slids;

stdc import {
    void exit(int status);
}

/*
correlation class.
*/
Correlation() {
    /* allocate and initialize to 0. */
    void init() {
        n_ = ElectoralMethod:g_nmethods;
        size = n_ * n_;
        counts_.resize(size);
        for (i : 0..size) {
            counts_[i] = 0;
        }
    }

    /* increment count for pair a,b. */
    void increment(int a, int b) {
        idx = a * n_ + b;
        ++counts_[idx];
    }

    /* get count for pair a,b. */
    int count(int a, int b) {
        idx = a * n_ + b;
        return counts_[idx];
    }
}

/* define the global object. */
global TestRunner g_runner;

TestRunner() {
    _() {} ~() {}

    void run() {

        /* suppress unreachable error. */
        void;

        /* initialize the random number generators. */
        rng:init(kSeedChoice);

        /* configure the electorate. */
        electorate_.nvoters_ = kNVoters;
        electorate_.method_ = kElectorateMethod;
        electorate_.naxes_ = kNAxes;
        electorate_.axis_weight_decay_ = kAxisWeightDecay;
        electorate_.nclusters_ = kNClusters;

        /* some sanity checks. */
        sanity_checks();

        /* name the methods. */
        init_methods();
        set_method_names();

        /* initialize the correlations and pairwise counts. */
        correlation_.init();
        pairwise_.init();

        /* hello, world. */
        println(String + "Guthrie voting analysis:");
        show_header();

        /* run many trials. */
        for (trial : 1..<=ntrials_) {
            if (ntrials_ > 1) {
                println(String);
                println(String + "Trial: " + trial);
            }

            /* initialize the electorate and candidates. */
            electorate_.init();
            if (kShowElectorateDistribution) {
                void; /* disable unreachable code error. */
                electorate_.show_distribution();
            }
            find_best_candidate();
            init_candidates();
            actual_ = calculate_utilities();
            use_median_satisfaction(actual_);
            accumulated_.best_ += actual_.best_;
            accumulated_.average_ += actual_.average_;
            vote();
            find_winners();
            save_correlations();
            show_satisfaction();
            check_criteria();
            strategic_voting();
        }

        /* log the results. */
        show_summary();
    }

    void sanity_checks() {
        if (ntrials_ < 1) {
            println(String + "Error: Need at least 1 trial (" + ntrials_ + ").");
            stdc:exit(1);
        }
        if (ncandidates_ < 3) {
            println(String + "Error: Need at least 3 candidates (" + ncandidates_ + ").");
            stdc:exit(1);
        }
        if (ncandidates_ >= 26) {
            println(String + "Error: At most 26 candidates (" + ncandidates_ + ").");
            stdc:exit(1);
        }
        if (electorate_.nvoters_ < ncandidates_) {
            println(String + "Error: Need more voters (" + electorate_.nvoters_ + ") than candidates (" + ncandidates_ + ").");
            stdc:exit(1);
        }

        /* uniform electorate is a single axis. */
        if (kElectorateMethod == kElectorateUniform) {
            void; /* disable unreachable code error. */

            if (electorate_.naxes_ != 1) {
                println(String + "Warning: Uniform electorate requires number of issue axes (" + electorate_.naxes_ + ") to be 1, overriding.");
                electorate_.naxes_ = 1;
            }
        }
    }

    /*
    collect methods into a Vector.
    guthrie first.
    the rest in alphabetical order.
    the list will be sorted for the summary.
    */
    void init_methods() {
        methods_.reserve(ElectoralMethod:g_nmethods);
        methods_.append(^guthrie_);
        methods_.append(^anti_plurality_);
        methods_.append(^approval_);
        methods_.append(^approval_runoff_);
        methods_.append(^borda_);
        methods_.append(^bucklin_);
        methods_.append(^candidate_);
        methods_.append(^condorcet_);
        methods_.append(^coombs_);
        methods_.append(^instant_runoff_);
        methods_.append(^plurality_);
        methods_.append(^plurality_runoff_);
        methods_.append(^range_);
        methods_.append(^star_);

        methods_sorted_ = methods_;
    }

    /* name the methods. */
    void set_method_names() {
        anti_plurality_.name_ = "anti-plurality";
        approval_.name_ = "approval";
        approval_runoff_.name_ = "approval runoff";
        borda_.name_ = "Borda";
        bucklin_.name_ = "Bucklin";
        candidate_.name_ = "candidate";
        condorcet_.name_ = "Condorcet";
        coombs_.name_ = "Coombs";
        guthrie_.name_ = "Guthrie";
        instant_runoff_.name_ = "instant runoff";
        plurality_.name_ = "plurality";
        plurality_runoff_.name_ = "plurality runoff";
        range_.name_ = "range";
        star_.name_ = "star";
    }

    /* show configuration. */
    void show_header() {
        seed = rng:get_seed();

        println(String + "Configuration:");
        println(String + "Number trials    : " + ntrials_);
        println(String + "Number voters    : " + electorate_.nvoters_);
        println(String + "Number candidates: " + ncandidates_);
        if (kSeedChoice == 0) {
            void; /* disable unreachable code error. */
            println(String + "Random seed      : " + seed);
        } else {
            void; /* disable unreachable code error. */
            println(String + "Fixed seed       : " + seed);
        }
        switch (electorate_.method_) {
            kElectorateUniform: {
                println(String + "Electorate       : uniform");
            } kElectorateRandom: {
                println(String + "Electorate       : random");
            } kElectorateClusters: {
                println(String + "Electorate       : clusters (" + electorate_.nclusters_ + ")");
            }
        }
        if (electorate_.naxes_ == 1) {
            println(String + "Issue axes       : " + electorate_.naxes_);
        } else {
            println(String + "Issue axes       : " + electorate_.naxes_ + " (" + electorate_.axis_weight_decay_ + ")");
        }
        println(String + "Dispersion       : " + dispersion_);

        if (kUseApprovalVotes) {
            void; /* disable unreachable code error. */
            println(String + "Note: Guthrie winner is found using approval votes instead of single vote.");
        }

        if (kUseMedianForSatisfaction) {
            void; /* disable unreachable code error. */
            println(String + "Note: non-standard satisfaction metric is being shown.");
            println(String + "  The best utility is estimated by the median voter.");
            println(String + "  The worst utility is estimated by one of the extreme voters.");
            println(String + "  The average utility is 90% best and 10% worst.");
        }
    }

    /*
    find the voter that would make the best candidate.
    also find the average utility of all voters.
    */
    void find_best_candidate() {
        if (kFindTheoreticalBestCandidate == false) {
            return;
            void;
        }

        int best = 0;
        float64 best_utility = -1e99;
        Position^ best_position = nullptr;
        float64 total_utility = 0.0;
        for (i : 0..electorate_.nvoters_) {
            ipos = ^electorate_.voters_[i].position_;
            float64 utility = 0.0;
            for (k : 0..electorate_.nvoters_) {
                kpos = ^electorate_.voters_[k].position_;
                utility += ipos^.utility(kpos);
            }
            if (utility > best_utility) {
                best = i;
                best_utility = utility;
                best_position = ipos;
            }
            total_utility += utility;
        }

        /*
        save the best candidate and utility.
        and the average utility of all candidates.
        */
        theoretical_.which_ = best;
        theoretical_.best_ = best_utility;
        theoretical_.average_ = total_utility / (float64=electorate_.nvoters_);

        /* show results. */
        println(String + "Best candidate chosen from all voters:");
        println(String + " Position: " + best_position^.to_string());
        println(String + " Utility : " + best_utility);
        println(String + " Average : " + theoretical_.average_);
    }

    void init_candidates() {
        /*
        pick primary candidates from the voters.
        eliminate most of them.
        sort them by the major axis.
        name them
        figure out how candidates rank each other.
        calculate voter satisfactions.
        */
        println(String + "Selecting candidates from the electorate.");
        pick_candidates_from_electorate();
        disperse_candidates();
        /* sort them. */
        quicksort(candidates_);
        single_transferable_vote_primary();
        name_candidates();
        if (kShowVoterBlocs) { void;
            show_bloc_map();
        }
        show_candidate_positions();
        rank_candidates();
    }

    void pick_candidates_from_electorate() {
        /* choose the number of candidates based on the specified method. */
        int n = ncandidates_;
        if (canddiate_method_ == kCandidatesSingleTransferableVote) {
            /* use the cube root of the numbe of voters. */
            float64 cube_root = math:pow((float64=electorate_.nvoters_), kPrimaryPower);
            n int = math:round(cube_root);

            /* maybe increase it. */
            n = math:max(n, ncandidates_);
        }

        if (n > ncandidates_) {
            println(String + "Reducing the number of candidates from "
                + n + " to " + ncandidates_ + ".");
        }

        /* allocate space */
        candidates_.resize(n);

        /*
        choose random voters as candidates.
        do not allow duplicates.
        */
        Vector<bool> duplicates;
        duplicates.resize(electorate_.nvoters_);
        for (i : 0..electorate_.nvoters_) {
            duplicates[i] = false;
        }
        char name = 'a';
        for (candidate : candidates_) {
            int i = 0;
            while() {
                i = rng:generate_int(electorate_.nvoters_);
                if (duplicates[i] == false) {
                    break;
                }
            }
            duplicates[i] = true;
            candidate^.name_ = name++;
            candidate^.position_ = electorate_.voters_[i].position_;
        }
    }

    /*
    move candidates closer to the political center.
    */
    void disperse_candidates() {
        if (dispersion_ == 1.0) {
            return;
        }

        /* find the average voter position. */
        int naxes = electorate_.naxes_;
        Position pos;
        pos.axis_.resize(naxes);
        for (i : 0..naxes) {
            pos.axis_[i] = 0.0;
        }
        for (voter : electorate_.voters_) {
            for (i : 0..naxes) {
                pos.axis_[i] += voter^.position_.axis_[i];
            }
        }
        nvoters float64 = electorate_.nvoters_;
        float64 denom = 1.0 / nvoters;
        for (i : 0..naxes) {
            pos.axis_[i] *= denom;
        }

        /*
        pre-apply the weighting factor to the average position.
        yes, we could optimize this step with the divide in the average.
        but that would make the code less clear.
        */
        float64 factor = 1.0 - dispersion_;
        for (i : 0..naxes) {
            pos.axis_[i] *= factor;
        }

        /* move all candidates towards the center. */
        for (candidate : candidates_) {
            for (i : 0..naxes) {
                float64 cpos = candidate^.position_.axis_[i];
                float64 apos = pos.axis_[i];
                candidate^.position_.axis_[i] = dispersion_ * cpos + apos;
            }
        }
    }

    void single_transferable_vote_primary() {
        /*
        creating blocs is expensive.
        reducing blocks is cheaper.
        */
        create_blocs();

        /*
        reduce the number of candidates.
        remove the ones with the lowest vote counts.
        we don't care about ties.
        */
        while() {
            n int = candidates_.size();
            if (n <= ncandidates_) {
                break;
            }
            vote();
            int worst = 0;
            int worst_support = 0x7FFFFFFF;
            for (i : 0..n) {
                candidate = ^candidates_[i];
                if (candidate^.support_ < worst_support) {
                    worst = i;
                    worst_support = candidate^.support_;
                }
            }
            candidates_.remove(worst);
            reduce_blocs(worst);
        }

        /* repeat for correct approvals. sigh. */
        create_blocs();
    }

    void name_candidates() {
        /* name them in sorted normalized order */
        for (i : 0..ncandidates_) {
            candidate = ^candidates_[i];
            candidate^.name_ = 'A' + (char=i);
        }
    }

    void show_candidate_positions() {
        println("Candidate positions:");
        for (candidate : candidates_ ) {
            println(String + " " + candidate^.name_ + ": " +
                candidate^.position_.to_string());
        }
    }

    void rank_candidates(
        bool quiet = false
    ) {
        /* every candidate rank orders the others. */
        for (candidate : candidates_) {
            rank_other_candidates(candidate);
        }

        if (quiet == false) {
            println("Candidate rankings of other candidates:");
            for (candidate : candidates_ ) {
                ss = String + " " + candidate^.name_ + ":";
                for (i : 1..ncandidates_) {
                    int rank = candidate^.rankings_[i];
                    float64 utility = candidate^.utilities_[i];
                    if (i > 1) {
                        ss += " >";
                    }
                    ss += String + " " + candidates_[rank].name_
                        + " (" + utility + ")";
                }
                println(ss);
            }
        }
    }

    void rank_other_candidates(
        Candidate^ candidate
    ) {
        candidate^.rankings_.reserve(ncandidates_);
        candidate^.utilities_.reserve(ncandidates_);
        candidate^.rankings_.clear();
        candidate^.utilities_.clear();

        for (i : 0..ncandidates_) {
            other = ^candidates_[i];
            float64 utility = candidate^.position_.utility(other^.position_);
            int k = 0;
            for (k : 0..i) {
                if (utility > candidate^.utilities_[k]) {
                    break;
                }
            }
            candidate^.rankings_.insert(k, i);
            candidate^.utilities_.insert(k, utility);
        }
    }

    /*
    calculate the total utilities of all of the candidates.
    */
    SatisfactionMetrics calculate_utilities() {

        /* reset the candidate utilities. */
        for (candidate : candidates_) {
            candidate^.utility_ = 0.0;
        }

        /* use the voter blocks. */
        for (it : bloc_map_) {
            /* use pseudonyms: rankings_ and bloc_ instead of key_ and value_. */
            n = it^.rankings_.size();
            for (i : 0..n) {
                which = it^.rankings_[i];
                candidates_[which].utility_ += it^.bloc_.utilities_[i];
            }
        }

        /* this might be the primary with extra candidates. */
        n = candidates_.size();

        /*
        find the best candidate.
        their utility.
        and the total utility.
        */
        intptr best_candidate = 0;
        float64 best_utility = -1.0;
        float64 total_utility = 0.0;
        for (i : 0..n) {
            /* update best and sum */
            float64 utility = candidates_[i].utility_;
            if (utility > best_utility) {
                best_candidate = i;
                best_utility = utility;
            }
            total_utility += utility;
        }

        /* compute utility or random candidate. */
        float64 average_utility = total_utility / (float64=n);

        /* return results. */
        SatisfactionMetrics result(
            (int=best_candidate),
            best_utility,
            average_utility
        );

        return result;
    }

    /*
    experimental option for alternative method for calculating voter satisfaction.
    the idea is to use a definition of best and average candidate that tracks
    across multiple trials.

    approximate the optimal candidate by the median voter.
    estimate the worst candidate as the first or last voter.
    approximate the average candidate as the average of the best and worst.
    */
    void use_median_satisfaction(
        mutable SatisfactionMetrics^ result
    ) {
        if (kUseMedianForSatisfaction == false) {
            return;
        }

        /* calculate the utility for the first median last voters. */
        n = electorate_.nvoters_;
        left = 0;
        mid = n / 2;
        right = n - 1;
        first^ = ^electorate_.voters_[left].position_;
        median^ = ^electorate_.voters_[mid].position_;
        last^ = ^electorate_.voters_[right].position_;
        float64 first_utility = 0.0;
        float64 median_utility = 0.0;
        float64 last_utility = 0.0;
        for (voter : electorate_.voters_) {
            first_utility += voter^.position_.utility(first);
            median_utility += voter^.position_.utility(median);
            last_utility += voter^.position_.utility(last);
        }

        /* return the results. */
        float64 worst_utility = math:min(first_utility, last_utility);
        float64 average_utility = 0.90 * median_utility + 0.10 * worst_utility;
        result^.best_ = median_utility;
        result^.average_ = average_utility;
        println(String + "=tsc= median sats: " + first_utility + " " + median_utility + " "
            + last_utility + " " + average_utility + " " + (median_utility-worst_utility));
    }

    /*
    divide the electorate up into blocks.
    computing the distance between voters and candidates is expensive.
    will get worse when we convert from distance to utility.
    */
    void create_blocs() {
        /*
        when there are 3 candidates...
        the voters can only be ABC, ACB, BAC, BCA, CAB, CBA.
        */
        bloc_map_.clear();
        Rankings rankings;
        Utilities utilities;
        Approvals approvals;
        n int = candidates_.size();
        int total_support = 0;

        for (voter : electorate_.voters_) {
            rankings.reserve(n);
            utilities.reserve(n);
            approvals.reserve(n);
            rankings.clear();
            utilities.clear();
            approvals.resize(n);

            /* create the rankings and the utility. */
            for (i : 0..n) {
                candidate = ^candidates_[i];
                float64 utility = voter^.position_.utility(candidate^.position_);
                int k = 0;
                for (k : 0..i) {
                    if (utility > utilities[k]) {
                        break;
                    }
                }
                rankings.insert(k, i);
                utilities.insert(k, utility);
            }

            /* approve of above average candidates. */
            float64 sum = 0.0;
            int napprovals = 0;
            for (i : 0..n) {
                approvals[i] = 0;
                sum += utilities[i];
            }
            float64 avg = sum / (float64=n);
            for (i : 0..n) {
                if (utilities[i] < avg) {
                    break;
                }
                approvals[i] = 1;
                ++napprovals;
            }
            total_support += napprovals;

            /* find the key. */
            it = bloc_map_.find(rankings);
            if (it == bloc_map_.end()) {
                /* add new bloc. */
                Bloc bloc;
                bloc.size_ = 1;
                bloc.utilities_ <-- utilities;
                bloc.napprovals_ = napprovals;
                bloc.approvals_ <-- approvals;
                bloc_map_.insert(rankings, bloc);
            } else {
                /* add to the existing bloc. */
                it^.bloc_.size_ += 1;
                it^.bloc_.napprovals_ += napprovals;
                for (i : 0..n) {
                    it^.bloc_.utilities_[i] += utilities[i];
                    it^.bloc_.approvals_[i] += approvals[i];
                }
            }
        }

        /* save the total support for find winner. */
        if (kUseApprovalVotes == false) { void;
            /* voters vote for a single candidate. */
            total_support_ = electorate_.nvoters_;
        } else { void;
            /* voters may approve multiple candidates. */
            total_support_ = total_support;
        }
    }

    /*
    the k-th candidate has been removed.
    recreate the blocs without him.
    recreate the new keys and the new utilities.

    ignore approvals cause we don't have the information we need to update it.
    */
    void reduce_blocs(
        int k
    ) {
        Rankings new_rankings;
        Utilities new_utilities;
        Approvals new_approvals;
        BlocMap new_bloc_map;

        n int = candidates_.size();

        for (it : bloc_map_) {
            new_rankings.reserve(n);
            new_utilities.reserve(n);
            new_approvals.reserve(n);
            new_rankings.clear();
            new_utilities.clear();
            new_approvals.resize(n);

            /*
            copy the old rankings and utilities
            omitting the former k-th candidate.
            adjust number for the new candidates.

            the original block map has keys of size n+1.
            hence the less than or equal.
            */
            for (i : 0..<=n) {
                int r = it^.rankings_[i];
                if (r == k) {
                    /* skip the removed candidate. */
                    continue;
                }
                float64 u = it^.bloc_.utilities_[i];
                int new_r = r;
                if (new_r > k) {
                    /* later candidates change index. */
                    --new_r;
                }
                new_rankings.append(new_r);
                new_utilities.append(u);
            }

            /* find the new rankings in the map. */
            rit = new_bloc_map.find(new_rankings);
            if (rit == new_bloc_map.end()) {
                /* must have approvals. */
                new_approvals[0] = it^.bloc_.size_;
                for (i : 1..n) {
                    new_approvals[i] = 0;
                }

                /* add a new bloc. */
                Bloc new_bloc;
                new_bloc.size_ = it^.bloc_.size_;
                new_bloc.utilities_ <-- new_utilities;
                new_bloc.napprovals_ = it^.bloc_.size_;
                new_bloc.approvals_ <-- new_approvals;
                new_bloc_map.insert(new_rankings, new_bloc);
            } else {
                /* accumulate into an existing bloc. */
                rit^.bloc_.size_ += it^.bloc_.size_;
                rit^.bloc_.napprovals_ += it^.bloc_.size_;
                for (i : 0..n) {
                    rit^.bloc_.utilities_[i] += new_utilities[i];
                }
                rit^.bloc_.approvals_[0] += it^.bloc_.size_;
            }
        }

        /* blow away the old bloc. */
        bloc_map_ <-- new_bloc_map;
    }

    void show_bloc_map() {
        n int = bloc_map_.size();
        println(String + "Voter blocs (" + n + "):");
        for (it : bloc_map_) {
            ss = String + " " + it^.rankings_.to_string();
            nrankings int = it^.rankings_.size();
            ss += String + " size: " + it^.bloc_.size_ + " utilities:";
            for (i : 0..nrankings) {
                ss += String + " " + it^.bloc_.utilities_[i];
            }
            ss += String + " n: " + it^.bloc_.napprovals_ + " approvals:";
            for (i : 0..nrankings) {
                ss += String + " " + it^.bloc_.approvals_[i];
            }
            println(ss);
        }
    }

    void vote() {
        /*
        clear support.
        we may vote multiple times.
        */
        for (candidate : candidates_) {
            candidate^.support_ = 0;
        }

        /*
        for each voter bloc...
        give the support to that candidate.
        */
        for (it : bloc_map_) {
            if (kUseApprovalVotes == false) { void;
                /* voters vote for a single candidate. */
                which = it^.rankings_[0];
                candidates_[which].support_ += it^.bloc_.size_;
            } else { void;
                /* voters may approve multiple candidates. */
                for (i : 0..ncandidates_) {
                    which = it^.rankings_[i];
                    candidates_[which].support_ += it^.bloc_.approvals_[i];
                }
            }
        }
    }

    /* find all winners. */
    void find_winners() {
        /* some voting methods use pairwise results. */
        pairwise_.find_winners();

        /* find winner for each method. */
        for (ElectoralMethod^ method : methods_) {
            method^.find_winner();
        }
    }

    /* analyze strategic voting. */
    void strategic_voting() {
        println();
        println("Strategic voting:");
        for (ElectoralMethod^ method : methods_) {
            method^.strategic_voting();
        }
    }

    /* save correlations. */
    void save_correlations() {
        nmethods = ElectoralMethod:g_nmethods;
        for (a : 0..nmethods) {
            wina = methods_[a]^.winner_;
            for (b : 0..nmethods) {
                if (a == b) {
                    continue;
                }
                winb = methods_[b]^.winner_;
                if (wina != winb) {
                    continue;
                }
                correlation_.increment(a, b);
            }
        }
    }

    /*
    voter satisfaction is a function of the utility of a candidate.
    the best candidate has satisfaction 1.0.
    the average candidate has a satisfaction of 0.0.
    worse candidates have negative satisfaction.

    show the voter satisfication for each candidate a number of ways.
    standard: considers just the candidates.
    primary: includes candidates eliminated in the primary.
    all possible: includes all voters.
    */
    void show_satisfaction() {
        println();
        println("Voter satisfaction (utility):");
        calculate_satisfaction(actual_);

        if (kFindTheoreticalBestCandidate) { void;
            println("Voter satisfaction (all possible):");
            calculate_satisfaction(theoretical_);
        }
    }

    void calculate_satisfaction(
        SatisfactionMetrics^ metric
    ) {
        float64 best = metric^.best_;
        float64 average = metric^.average_;
        float64 denom = best - average;
        for (candidate : candidates_) {
            float64 dutility = candidate^.utility_ - average;
            float64 satisfaction = dutility / denom;
            println(String + " " + candidate^.name_ + ": " + satisfaction
                + " (" + dutility + ")");
        }
    }

    void check_criteria() {
        println();
        println("Checking voting criteria.");
        int max_satisfaction = find_max_satisfaction_candidate();

        /* check for candidate rankings for cycle. */
        int candidate_cycle = check_candidate_rankings_for_cycles();
        if (candidate_cycle) {
            ++candidate_cycles_;
        }

        /* check guthrie for independence of irrelevant candidates. */
        int independence = check_independence();
        if (guthrie_.winner_ == max_satisfaction) {
            ++winner_maximizes_satisfaction_;
        }
        if (guthrie_.winner_ == independence) {
            ++independence_;
        }

        char condorcet_loser_name = '?';
        if (condorcet_loser_ >= 0) {
            condorcet_loser_name = candidates_[condorcet_loser_].name_;
        }

        /*
        tracking the condorcet loser is a bit tricky.
        if the winner is not the condorcet lower then result = string(x, x) = pass.
        */
        int loser = -1;
        /*
        if (condorcet_loser < 0) ie there is no condorcet loser
            loser = -1; condorcet_loser = -1;
            result = string(-1, -1) = n/a.
        */
        /*
        else if (winner_ == condorcet loser)
            loser = -1; condorcet_loser = x;
            result = string(-1, x) = fail.
        */
        /*
        else if (winner != condorcet_loser)
            loser = condorcet_loser = x
            result = string(x, x) = pass
        */
        if (guthrie_.winner_ != condorcet_loser_) {
            loser = condorcet_loser_;
        }

        /* check if the irv winner picks the condorcet winner. */
        if (condorcet_.winner_ >= 0) {
            if (instant_runoff_.winner_ == condorcet_.winner_) {
                ++irv_is_condorcet_;
            }
        }

        Format fmt;
        fmt.min_width_ = ElectoralMethod:kWidth;

        println();
        println(fmt*"Voting criteria results" + ":");

        /* guthrie, then max satisfaction, then everyone else. */
        (const char)[] result = nullptr;
        println(String + fmt*" Guthrie winner" + ": " + candidates_[guthrie_.winner_].name_);
        result = result_to_string(guthrie_.winner_, max_satisfaction);
        println(String + fmt*" Maximizes voter satisfaction" + ": " + candidates_[max_satisfaction].name_ + " " + result);
        for (ElectoralMethod^ method : methods_) {
            method^.print_winner();
        }

        /* more data dump. */
        result = result_to_string(guthrie_.winner_, candidate_.winner_);
        println(String + fmt*" maximizes candidate utility" + ": " + candidates_[candidate_.winner_].name_ + " " + result);
        result = result_to_string(loser, condorcet_loser_);
        println(String + fmt*" Condorcet loser" + ": " + condorcet_loser_name + " " + result);
        result = result_to_string(guthrie_.winner_, independence);
        println(String + fmt*" independence" + ": " + candidates_[independence].name_ + " " + result);
    }

    int find_max_satisfaction_candidate() {
        /* max satisfaction candidate. */
        int winner = actual_.which_;

        /* update summary */
        winning_candidate = ^candidates_[guthrie_.winner_];
        total_utility_ += winning_candidate^.utility_;

        return winner;
    }

    /*
    check the candidate rankings for a condorcet cycle.
    */
    int check_candidate_rankings_for_cycles() {
        /* ensure the candidate rankings are correct. */
        rank_candidates(true);

        /* initialize number of wins for each candidate. */
        Vector<int> wins;
        wins.reserve(ncandidates_);
        wins.resize(ncandidates_);
        for (i : 0..ncandidates_) {
            wins[i] = 0;
        }

        /* for every candidate pairing. */
        for (i : 0..ncandidates_) {
            for (k : (i + 1)..ncandidates_) {
                /* count the votes of every candidate. */
                int ivotes = 0;
                int kvotes = 0;
                for (candidate : candidates_) {
                    for (p : 0..ncandidates_) {
                        int which = candidate^.rankings_[p];
                        if (which == i) {
                            ++ivotes;
                            break;
                        }
                        if (which == k) {
                            ++kvotes;
                            break;
                        }
                    }
                }
                if (ivotes > kvotes) {
                    ++wins[i];
                }
                if (kvotes > ivotes) {
                    ++wins[k];
                }
            }
        }

        /*
        find the undefeated candidate.
        */
        int max_wins = ncandidates_ - 1;
        for (i : 0..ncandidates_) {
            if (wins[i] == max_wins) {
                println(String + "Candidate " + candidates_[i].name_ + " is undefeated.");
                return false;
            }
        }

        println("Candidate rankings form a cycle.");
        return true;
    }

    /*
    check for independence of irrelevant choices.
    that's not quite what we do.
    we remove a candidate and ensure the winner still wins.
    which is close enough.
    */
    int check_independence() {
        /* assume we pass. */
        int independence = guthrie_.winner_;

        /* save the block map */
        BlocMap org_bloc_map;
        org_bloc_map <--> bloc_map_;

        /* save the original winner */
        int original_winner = guthrie_.winner_;

        /* save the name of the original winner. */
        char original_winner_name = candidates_[original_winner].name_;

        /* save the original candidates. */
        original_candidates = candidates_;

        /* decrement the number of candidates. */
        int ncandidates = ncandidates_;
        ncandidates_ = ncandidates - 1;

        /* remove the first candidate. */
        candidates_.remove(0);

        /* for summary */
        int nwinners = 0;
        float64 total_utility = 0.0;

        /* remove one of the non-winners and revote. */
        for (i : 0..ncandidates) {
            /* skip the winner. */
            if (i != original_winner) {
                /* re-vote. */
                rank_candidates(kQuiet);
                create_blocs();
                vote();
                guthrie_.find_winner(kQuiet);

                /* check by name, not index. */
                candidate = ^candidates_[guthrie_.winner_];
                char winner_name = candidate^.name_;
                if (winner_name != original_winner_name) {
                    independence = i;
                    println(String + winner_name + " wins if " + original_candidates[i].name_ + " doesn't run.");
                    ++nwinners;
                    total_utility += candidate^.utility_;
                }
            }

            /*
            update the list of candidates.
            avoid buffer overrun at end of loop.
            3 candidates.
            i ranges from 0,1,2.
            original size is 3.
            current size is 2.
            */
            if (i < ncandidates_) {
                candidates_[i] = original_candidates[i];
            }
        }

        /* restore the original candidates, count, and winner. */
        candidates_ <--> original_candidates;
        ncandidates_ = ncandidates;
        guthrie_.winner_ = original_winner;

        /* accumulate utility. */
        float64 utility;
        if (nwinners == 0) {
            utility = candidates_[guthrie_.winner_].utility_;
        } else {
            utility = total_utility / (float64=nwinners);
        }
        total_utility_strategic_ += utility;

        /* restore the bloc map. */
        bloc_map_ <-- org_bloc_map;

        return independence;
    }

    void show_summary() {
        if (ntrials_ <= 1) {
            return;
        }

        denom float64 = ntrials_;
        non_cycle_trials float64 = ntrials_ - condorcet_cycles_;

        /* calculates satisfactions from average utilities. */
        accumulated_.best_ /= denom;
        accumulated_.average_ /= denom;

        /* cause guthrie is special. */
        guthrie_.total_utility_ = total_utility_;

        /* calculate results for all methods. */
        for (method : methods_) {
            method^.calculate_results();
        }

        /* sort methods by satisfaction. */
        sort_methods();

        /* miscellaneous calculations. */
        float64 maximizes_satisfaction = 100.0 * (float64=winner_maximizes_satisfaction_) / denom;
        float64 is_condorcet_loser = 100.0 * (float64=condorcet_is_loser_) / denom;
        float64 independence = 100.0 * (float64=independence_) / denom;
        float64 majority_winners = 100.0 * (float64=majority_winners_) / denom;
        float64 condorcet_cycles = 100.0 * (float64=condorcet_cycles_) / denom;
        float64 candidate_cycles = 100.0 * (float64=candidate_cycles_) / denom;
        float64 irv_is_condorcet = 100.0 * (float64=irv_is_condorcet_) / non_cycle_trials;

        Format fmt;
        fmt.min_width_ = ElectoralMethod:kWidth;

        println();
        show_header();

        println();
        println("Summary:");
        println(fmt*"Voter satisfactions by method" + ":");
        for (method : methods_sorted_) {
            method^.print_satisfaction();
        }

        println();
        println(fmt*"Guthrie correlations by method" + ":");
        for (method : methods_sorted_) {
            method^.print_correlation();
        }

        println();
        println(fmt*"Miscellaneous" + ":");
        println(String + fmt*" candidate satisfaction" + ": " + candidate_.satisfaction_);
        println(String + fmt*" maxes candidate satisfaction" + ": " + candidate_.correlation_ + "%");
        println(String + fmt*" maxes voter satisfaction" + ": " + maximizes_satisfaction + "%");
        println(String + fmt*" independence" + ": " + independence + "%");
        println(String + fmt*" won by majority" + ": " + majority_winners + "%");
        println(String + fmt*" Condorcet loser wins" + ": " + is_condorcet_loser + "%");
        println(String + fmt*" Condorcet cycles" + ": " + condorcet_cycles + "%");
        println(String + fmt*" candidate cycles" + ": " + candidate_cycles + "%");
        println(String + fmt*" instant runoff is Condorcet" + ": " + irv_is_condorcet + "%");

        println();
        print_correlations();

        println();
        print_strategic_results();
    }

    /*
    sort electoral methods by decreasing satisfaction.
    */
    void sort_methods() {
        /*
        quicksort using inline less-than function.
        sort by names in case of ties.
        */
        bool decreasing_satisfaction(
            ElectoralMethod^ a,
            ElectoralMethod^ b
        ) {
            if (a^.satisfaction_ > b^.satisfaction_) {
                return true;
            }
            if (a^.satisfaction_ < b^.satisfaction_) {
                return false;
            }
            if (a^.name_ < b^.name_) {
                return true;
            }
            return false;
        }
        quicksort(methods_sorted_, ^decreasing_satisfaction);
    }

    /* print all correlations */
    void print_correlations() {
        n = correlation_.n_;

        maxlen = 0;
        for (ElectoralMethod^ method : methods_) {
            len int = method^.name_.size();
            if (maxlen < len) {
                maxlen = len;
            }
        }
        Format sfmt;
        sfmt.min_width_ = maxlen;

        Format nfmt;
        nfmt.justify_ = kRightJustify;
        nfmt.float_style_ = kFixedPoint;
        nfmt.min_width_ = 6; // _100.0
        nfmt.max_width_ = 6;
        nfmt.precision_ = 1;

        ntrials float = ntrials_;

        println("Correlations by method:");
        for (a : 0..(n-1)) {
            metha = methods_[a];
            s = String + " " + sfmt*metha^.name_ + ":";
            for (b : (n-1)..>a -1) {
                count float = correlation_.count(a, b);
                pct = 100.0 * count / ntrials;
                s += nfmt*pct + "%";
            }
            if (a != 0) {
                methb = methods_[a];
                s += "   ^-";
                s += methb^.name_;
            }
            println(s);
        }

        meth = methods_[1];
        s = sfmt*"" + "     ^-" + meth^.name_;
        println(s);
    }

    /* print all strategic results. */
    void print_strategic_results() {
        println("Strategic voting results: opportunities %, average minimum strategic voters: %");
        for (ElectoralMethod^ method : methods_) {
            method^.print_strategic_results();
        }
    }
}
