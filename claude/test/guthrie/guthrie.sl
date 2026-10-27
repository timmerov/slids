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

import+ algorithm;
import dump;
import electorate;
import guthrie;
import math;
import+ map;
import random;
import string;
import+ vector;
alias slids;

stdc import {
    void exit(int status);
}

/* number of trials. */
//const int kNTrials = 1;
const int kNTrials = 10;
//const int kNTrials = 30;
//const int kNTrials = 300;
//const int kNTrials = 1000;
//const int kNTrials = 10*1000;
//const int kNTrials = 30*1000;

/* number of voters. */
//const int kNVoters = 20;
//const int kNVoters = 50;
//const int kNVoters = 100;
const int kNVoters = 1000;
//const int kNVoters = 10*1000;

/* number of candidates */
//const int kNCandidates = 3;
const int kNCandidates = 4;
//const int kNCandidates = 5;
//const int kNCandidates = 6;
//const int kNCandidates = 7;
//const int kNCandidates = 9;

/* options for distributing the electorate. */
//const int kElectorateMethod = kElectorateUniform;
//const int kElectorateMethod = kElectorateRandom;
const int kElectorateMethod = kElectorateClusters;

/* options for clustered method. */
const int kNClusters = kNCandidates * 2;

/* options for number of issue dimensions (axes). */
//const int kNAxes = 1;
const int kNAxes = 2;
//const int kNAxes = 3;

/*
option for relative weighting of the axes.
the major axis has a weight of 1.0.
successive axes are scaled by this factor.
should be >0.0 and <=1.0.
intutition says 0.4 would be reasonable.
this means most real world issues are reasonably correlated.
and/or the minor issues are less important than the major one.
which seems to be the case.
1.0 means we have many uncorrelated issues.
and they are all equally important.
which seems a bit of a stretch.
*/
const float64 kAxisWeightDecay = 0.4;
//const float64 kAxisWeightDecay = 1.0;

/* options for choosing candidates */
const int kCandidatesRandom = 0;
const int kCandidatesSingleTransferableVote = 1;
const int kCandidateMethod = kCandidatesSingleTransferableVote;

/*
with the single transferable vote primary...
we choose a number of voters to be candidates.
intutition says the number should be between the cube root (0.333)
and the square root (0.5) of the number of voters.
too few is not a representative statistical distribution of the voters.
too many takes too long and doesn't help.
might even hurt.
default compromise is about 0.4.
*/
const float64 kPrimaryPower = 0.4;

/*
the assumption is voters are more radical than candidates.
we select candidates at random from the voters.
then we move them towards the middle.
a dispersion factor of 1.00 means don't move them at all.
0.00 means move them to the exact center.
real world estimates for dispersion range from 0.3 to 0.9.
with best guess around 0.7.
low dispersion amplifies errors by the electoral method.
with dispersion around 0.7, plurality satisfaction is 0.0 or negative.
interesting.
*/
//const float64 kDispersion = 1.0;
const float64 kDispersion = 0.7;
//const float64 kDispersion = 0.3;

/* option to use a fixed seed for testing. */
//const uint64 kSeedChoice = 0;
const uint64 kSeedChoice = 1753391226898898129;

/*
option to use approval votes to find guthrie winner.
this seems like it should work well.
but it actually increases the opportunities for strategic voting
that lowers total voter satisfaction.
this is not recommended.
*/
const bool kUseApprovalVotes = false;
//const bool kUseApprovalVotes = true;

/*
option to find the theoretical best candidate from the voters.
this feature is expensive and not used by the art.
*/
//const bool kFindTheoreticalBestCandidate = true;
const bool kFindTheoreticalBestCandidate = false;

/*
option to use the median voter and a constructed average voter
for calculating voter satisfaction.
this is not recommended.
*/
//const bool kUseMedianForSatisfaction = true;
const bool kUseMedianForSatisfaction = false;

/*
option to show the electorate distribution.
this is a bit spammy.
*/
//const bool kShowElectorateDistribution = true;
const bool kShowElectorateDistribution = false;

/*
option to show the voter blocs.
this is a bit spammy.
*/
const bool kShowVoterBlocs = true;
//const bool kShowVoterBlocs = false;

/*
option to show details of all coombs rounds.
this is a bit spammy.
*/
//const bool kShowCoombsRounds = true;
const bool kShowCoombsRounds = false;


/* some functions should sometimes be quiet. */
const bool kQuiet = true;

SatisfactionMetrics(
    /* candidate with the best utility. */
    int which_ = 0,

    /* utility of the best candidate. */
    float64 best_ = 0.0,

    /* average utility of the average candidate. */
    float64 average_ = 0.0
) { }

alias Rankings = Vector<int>;
alias Utilities = Vector<float64>;

Candidate(
    /* generated name */
    char name_ = '?',

    /* position along the axis ranges from 0..1 */
    Position position_,

    /* vote total aka asset */
    int support_ = 0,

    /* ranking of other candidates. */
    Rankings rankings_,
    Utilities utilities_,

    /* utility */
    float64 utility_ = 0.0
) {
    /* for sorting */
    bool const op<(Candidate^ other) {
        return (position_ < other^.position_);
    }
}
alias Candidates = Vector<Candidate>;

alias Approvals = Vector<int>;

Bloc(
    /* voters in this bloc. */
    int size_ = 0,

    /* distances to candidates in ranked order. */
    Utilities utilities_,

    /* total approvals of voters in the bloc. */
    int napprovals_ = 0,
    Approvals approvals_
) { }

alias BlocMap = Map<Rankings, Bloc>;

/*
fancy feature unique to slids:
MapNode has generic fields key_ and value_.
alias them for the MapNode associated with BlocMap
to rankings_ and bloc_ to make the code that iterates
over a BlocMap more readable.
*/
MapNode<Key=Rankings, Value=Bloc>(
    alias rankings_ = key_,
    alias bloc_ = value_
) {
}

/*
base class for electoral methods.
every derived class must define find_winner.
*/
ElectoralMethod(
    String name_,
    int winner_ = -1,
    int is_winner_ = 0,
    float64 total_utility_ = 0.0,
    float64 satisfaction_ = 0.0,
    float64 correlation_ = 0.0
) {
    global int g_nmethods = 0;
    const int kWidth = 30;

    _() {
        ++g_nmethods;
    }
    virtual ~() {}

    //void find_winner(bool quiet = kQuiet);

    /* calculate satisfaction and correlation. */
    void calculate_results();

    /* pretty print the line. */
    virtual void print_satisfaction() {
        Format fmt;
        fmt.min_width_ = kWidth;
        str = String + " " + name_;
        println(String + fmt*str + ": " + satisfaction_);
    }

    /* pretty print the line. */
    virtual void print_correlation() {
        Format fmt;
        fmt.min_width_ = kWidth;
        str = String + " " + name_;
        println(String + fmt*str + ": " + correlation_ + "%");
    }
}

/* define the global object. */
global GuthrieVoting g_guthrie;

GuthrieVoting(
    /* "constants" */
    int ntrials_ = kNTrials,
    int ncandidates_ = kNCandidates,
    int canddiate_method_ = kCandidateMethod,
    float64 dispersion_ = kDispersion,

    /* the electorate and the candidates. */
    Electorate electorate_,
    Candidates candidates_,
    BlocMap bloc_map_,
    int total_support_ = 0,

    /* results from the trial. */
    SatisfactionMetrics theoretical_,
    SatisfactionMetrics actual_,
    SatisfactionMetrics accumulated_,

    /* analyzed electoral methods. */
    AntiPluralityMethod anti_plurality_,
    ApprovalMethod approval_,
    ApprovalRunoffMethod approval_runoff_,
    BordaMethod borda_,
    BucklinMethod bucklin_,
    CandidateMethod candidate_,
    CondorcetMethod condorcet_,
    CoombsMethod coombs_,
    GuthrieMethod guthrie_,
    InstantRunoffMethod instant_runoff_,
    PluralityMethod plurality_,
    PluralityRunoffMethod plurality_runoff_,
    RangeMethod range_,
    StarMethod star_,

    /* summary */
    float64 total_utility_ = 0.0,
    float64 total_utility_strategic_ = 0.0,
    float64 total_utility_ordering_ = 0.0,
    int majority_winners_ = 0,
    int winner_maximizes_satisfaction_ = 0,
    int condorcet_loser_ = 0,
    int condorcet_is_loser_ = 0,
    int condorcet_cycles_ = 0,
    int candidate_cycles_ = 0,
    int independence_ = 0,
    int irv_is_condorcet_ = 0
) {
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
        set_method_names();

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
            guthrie_.find_winner();
            show_satisfaction();
            check_criteria();
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
            String ss = " ";
            nrankings int = it^.rankings_.size();
            for (k : 0..nrankings) {
                int which = it^.rankings_[k];
                ss += candidates_[which].name_;
            }
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

    float64 calculate_satisfaction(
        float64 utility,
        SatisfactionMetrics^ metric
    ) {
        float64 best = metric^.best_;
        float64 average = metric^.average_;
        float64 satisfaction = (utility - average) / (best - average);
        return satisfaction;
    }

    void check_criteria() {
        println();
        println("Checking voting criteria.");
        int max_satisfaction = find_max_satisfaction_candidate();
        approval_runoff_.find_winner();
        range_.find_winner();
        condorcet_.find_winner();
        star_.find_winner();
        borda_.find_winner();
        approval_.find_winner();
        coombs_.find_winner();
        bucklin_.find_winner();
        anti_plurality_.find_winner();
        instant_runoff_.find_winner();
        plurality_runoff_.find_winner();
        plurality_.find_winner();
        candidate_.find_winner();

        /* check for candidate rankings for cycle. */
        int candidate_cycle = check_candidate_rankings_for_cycles();
        if (candidate_cycle) {
            ++candidate_cycles_;
        }

        /* caution: this destroys the bloc map. */
        int independence = check_independence();

        if (guthrie_.winner_ == max_satisfaction) {
            ++winner_maximizes_satisfaction_;
        }
        if (guthrie_.winner_ == independence) {
            ++independence_;
        }

        char condorcet_winner_name = '?';
        if (condorcet_.winner_ >= 0) {
            condorcet_winner_name = candidates_[condorcet_.winner_].name_;
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

        println();
        println("Voting criteria results:");
        (const char)[] result = nullptr;
        println(String + "Guthrie winner              : " + candidates_[guthrie_.winner_].name_);
        result = result_to_string(guthrie_.winner_, max_satisfaction);
        println(String + "Maximizes voter satisfaction: " + candidates_[max_satisfaction].name_ + " " + result);
        result = result_to_string(guthrie_.winner_, approval_runoff_.winner_);
        println(String + "Approval runoff winner      : " + candidates_[approval_runoff_.winner_].name_ + " " + result);
        result = result_to_string(guthrie_.winner_, range_.winner_);
        println(String + "Range winner                : " + candidates_[range_.winner_].name_ + " " + result);
        result = result_to_string(guthrie_.winner_, condorcet_.winner_);
        println(String + "Condorcet winner            : " + condorcet_winner_name + " " + result);
        result = result_to_string(guthrie_.winner_, star_.winner_);
        println(String + "Star winner                 : " + candidates_[star_.winner_].name_ + " " + result);
        result = result_to_string(guthrie_.winner_, borda_.winner_);
        println(String + "Borda winner                : " + candidates_[borda_.winner_].name_ + " " + result);
        result = result_to_string(guthrie_.winner_, approval_.winner_);
        println(String + "Approval winner             : " + candidates_[approval_.winner_].name_ + " " + result);
        result = result_to_string(guthrie_.winner_, coombs_.winner_);
        println(String + "Coombs winner               : " + candidates_[coombs_.winner_].name_ + " " + result);
        result = result_to_string(guthrie_.winner_, bucklin_.winner_);
        println(String + "Bucklin winner              : " + candidates_[bucklin_.winner_].name_ + " " + result);
        result = result_to_string(guthrie_.winner_, anti_plurality_.winner_);
        println(String + "Anti-plurality winner       : " + candidates_[anti_plurality_.winner_].name_ + " " + result);
        result = result_to_string(guthrie_.winner_, instant_runoff_.winner_);
        println(String + "Instant runoff winner       : " + candidates_[instant_runoff_.winner_].name_ + " " + result);
        result = result_to_string(guthrie_.winner_, plurality_runoff_.winner_);
        println(String + "Plurality runoff winner     : " + candidates_[plurality_runoff_.winner_].name_ + " " + result);
        result = result_to_string(guthrie_.winner_, plurality_.winner_);
        println(String + "Plurality winner            : " + candidates_[plurality_.winner_].name_ + " " + result);
        result = result_to_string(guthrie_.winner_, candidate_.winner_);
        println(String + "Maximizes candidate utility : " + candidates_[candidate_.winner_].name_ + " " + result);
        result = result_to_string(loser, condorcet_loser_);
        println(String + "Condorcet loser             : " + condorcet_loser_name + " " + result);
        result = result_to_string(guthrie_.winner_, independence);
        println(String + "Independence                : " + candidates_[independence].name_ + " " + result);
    }

    (const char)[] result_to_string(int winner, int expected) {
        if (expected < 0) {
            return "n/a";
        }
        if (winner == expected) {
            return "pass";
        }
        return "=FAIL=";
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

        /* collect methods into a Vector. */
        Vector<ElectoralMethod^> methods;
        methods.reserve(ElectoralMethod:g_nmethods);
        methods.append(^anti_plurality_);
        methods.append(^approval_);
        methods.append(^approval_runoff_);
        methods.append(^borda_);
        methods.append(^bucklin_);
        methods.append(^candidate_);
        methods.append(^condorcet_);
        methods.append(^coombs_);
        methods.append(^guthrie_);
        methods.append(^instant_runoff_);
        methods.append(^plurality_);
        methods.append(^plurality_runoff_);
        methods.append(^range_);
        methods.append(^star_);

        /* calculate results for all methods. */
        for (method : methods) {
            method^.calculate_results();
        }

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
        for (method : methods) {
            method^.print_satisfaction();
        }
        println(fmt*"Guthrie correlations by method" + ":");
        for (method : methods) {
            method^.print_correlation();
        }
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
    }
}

RankedChoice(
    Rankings rankings_,
    int size_
) {
}
alias RankedChoices = Vector<RankedChoice>;

HeadToHead(
    int winner_ = 0,
    int loser_ = 0,
    int winner_votes_ = 0,
    int loser_votes_ = 0
) {
    _() {}
    ~() {}

    /* find the winner in a head to head election. */
    void resolve(
        int a,
        int b
    ) {
        bloc_map = ^g_guthrie.bloc_map_;

        /* init vote counts. */
        int avotes = 0;
        int bvotes = 0;

        /* for each voter bloc. */
        for (it : bloc_map^) {
            /* give the votes to whichever is first. */
            for (which : it^.rankings_) {
                if (which == a) {
                    avotes += it^.bloc_.size_;
                    break;
                }
                if (which == b) {
                    bvotes += it^.bloc_.size_;
                    break;
                }
            }
        }

        /*
        we assume that a comes before b in the candidate list.
        and that we're looking for a winner.
        in which case, by the rules, a wins ties.
        */
        if (avotes >= bvotes) {
            winner_ = a;
            loser_ = b;
            winner_votes_ = avotes;
            loser_votes_ = bvotes;
        } else {
            winner_ = b;
            loser_ = a;
            winner_votes_ = bvotes;
            loser_votes_ = avotes;
        }
    }
}

void ElectoralMethod:calculate_results(
) {
    /* calculate satisfaction. */
    ntrials float64 = g_guthrie.ntrials_;
    float64 average_utility = total_utility_ / ntrials;
    satisfaction_ = g_guthrie.calculate_satisfaction(average_utility, g_guthrie.accumulated_);

    /* calculate correlation. */
    correlation_ = 100.0 * (float64=is_winner_) / ntrials;
}

/*
find the anti-plurality winner.
*/
ElectoralMethod : AntiPluralityMethod() {
    void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_guthrie.ncandidates_;
        candidates = ^g_guthrie.candidates_;
        bloc_map = ^g_guthrie.bloc_map_;

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
        if (winner_ == g_guthrie.guthrie_.winner_) {
            ++is_winner_;
        }
    }

    virtual void print_correlation() {
        /* pretty print the line. */
        println(String + " anti-plurality               : " + correlation_ + "%");
    }
}

/*
find the approval winner.
use the approvals stored when the bloc was created.
*/
ElectoralMethod : ApprovalMethod() {
    void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_guthrie.ncandidates_;
        candidates = ^g_guthrie.candidates_;
        bloc_map = ^g_guthrie.bloc_map_;
        int nvoters = g_guthrie.electorate_.nvoters_;

        /* initialize number of approvals for each candidate. */
        Vector<int> approvals;
        approvals.reserve(ncandidates);
        approvals.resize(ncandidates);
        for (i : 0..ncandidates) {
            approvals[i] = 0;
        }

        /* for each voter bloc. */
        for (it : bloc_map^) {
            for (i : 0..ncandidates) {
                int which = it^.rankings_[i];
                approvals[which] += it^.bloc_.approvals_[i];
            }
        }

        /* find the largest approval. */
        winner_ = -1;
        int max = -1;
        for (i : 0..ncandidates) {
            int approval = approvals[i];
            if (max < approval ) {
                winner_ = i;
                max = approval;
            }
        }

        /*
        what does approval do when there is no majority winner?
        we use the plurality winner.
        log it. ship it.
        */
        candidate = ^candidates^[winner_];
        if (2*max <= nvoters) {
            int majority = (nvoters + 1) / 2;
            println(String + candidate^.name_ + " has a plurality (" + max +
                ") but not a majority (" + majority + ") of approval votes.");
        }

        /* accumulate the utility. */
        total_utility_ += candidate^.utility_;

        /* accumulate correlations. */
        if (winner_ == g_guthrie.guthrie_.winner_) {
            ++is_winner_;
        }
    }
}

/*
find the two round approval winner where the top two have a runoff.
use the approvals stored when the bloc was created.
*/
ElectoralMethod : ApprovalRunoffMethod() {
    void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_guthrie.ncandidates_;
        candidates = ^g_guthrie.candidates_;
        bloc_map = ^g_guthrie.bloc_map_;

        /* initialize number of approvals for each candidate. */
        Vector<int> approvals;
        approvals.reserve(ncandidates);
        approvals.resize(ncandidates);
        for (i : 0..ncandidates) {
            approvals[i] = 0;
        }

        /* for each voter bloc. */
        for (it : bloc_map^) {
            for (i : 0..ncandidates) {
                int which = it^.rankings_[i];
                approvals[which] += it^.bloc_.approvals_[i];
            }
        }

        /* find the largest approval. */
        winner_ = -1;
        int max = -1;
        for (i : 0..ncandidates) {
            int approval = approvals[i];
            if (max < approval ) {
                winner_ = i;
                max = approval;
            }
        }

        /* find the second largest approval. */
        approvals[winner_] = 0;
        int second = -1;
        max = -1;
        for (i : 0..ncandidates) {
            int approval = approvals[i];
            if (max < approval ) {
                second = i;
                max = approval;
            }
        }

        /* find the head to head winner. */
        HeadToHead runoff;
        runoff.resolve(winner_, second);

        /* overwrite the winner. */
        winner_ = runoff.winner_;

        /* accumulate the utility. */
        candidate = ^candidates^[winner_];
        total_utility_ += candidate^.utility_;

        /* accumulate correlations. */
        if (winner_ == g_guthrie.guthrie_.winner_) {
            ++is_winner_;
        }
    }
}

/*
borda count 0 for first, 1 for second, ...
lowest total wins.
*/
ElectoralMethod : BordaMethod() {
    void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_guthrie.ncandidates_;
        candidates = ^g_guthrie.candidates_;
        bloc_map = ^g_guthrie.bloc_map_;

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
        if (winner_ == g_guthrie.guthrie_.winner_) {
            ++is_winner_;
        }
    }
}

/*
bucklin voting.
while no greatest majority winner, accumulate the next choices.
*/
ElectoralMethod : BucklinMethod() {
    void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_guthrie.ncandidates_;
        candidates = ^g_guthrie.candidates_;
        bloc_map = ^g_guthrie.bloc_map_;
        int nvoters = g_guthrie.electorate_.nvoters_;

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
        if (winner_ == g_guthrie.guthrie_.winner_) {
            ++is_winner_;
        }
    }
}

/*
check if the winning candidate maximizes the total
satisfaction of all candidates.
not voters.
*/
ElectoralMethod : CandidateMethod() {
    void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_guthrie.ncandidates_;
        candidates = ^g_guthrie.candidates_;

        winner_ = -1;
        float64 max = -1e99;

        for (i : 0..ncandidates) {
            a = ^candidates^[i];
            float64 utility = 0.0;
            for (k : 0..ncandidates) {
                if (k == i) {
                    continue;
                }
                b = ^candidates^[k];
                utility += a^.position_.utility(b^.position_);
            }
            if (utility > max) {
                winner_ = i;
                max = utility;
            }
        }

        /* accumulate the utility. */
        candidate = ^candidates^[winner_];
        total_utility_ += candidate^.utility_;

        /* accumulate correlations. */
        if (winner_ == g_guthrie.guthrie_.winner_) {
            ++is_winner_;
        }
    }

    virtual void print_satisfaction() {
        /* nothing to print. */
    }

    virtual void print_correlation() {
        /* nothing to print. */
    }
}

/*
condorcet wins the most head to head races.
*/
ElectoralMethod : CondorcetMethod() {
    void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_guthrie.ncandidates_;
        candidates = ^g_guthrie.candidates_;

        /* initialize number of wins for each candidate. */
        Vector<int> wins;
        wins.reserve(ncandidates);
        wins.resize(ncandidates);
        for (i : 0..ncandidates) {
            wins[i] = 0;
        }

        println("Condorcet results:");
        /* count head to head victories. */
        for (i : 0..ncandidates) {
            for (k : (i + 1)..ncandidates) {
                HeadToHead result;
                result.resolve(i, k);
                println(String + " " + candidates^[result.winner_].name_ + " (" + result.winner_votes_
                    + ") > " + candidates^[result.loser_].name_ + " (" + result.loser_votes_ + ")");
                ++wins[result.winner_];
            }
        }

        /* test for condorcet ordering. */
        uint64 counts = 0;

        /*
        find the candidate with the most wins.
        find the average utility while we're here.
        */
        int max = -1;
        winner_ = -1;
        int nwinners = 0;
        int loser = -1;
        float64 total_utility = 0.0;
        for (i : 0..ncandidates) {
            int w = wins[i];
            bool sum_it = false;
            if (max == w) {
                ++nwinners;
                sum_it = true;
            } else if (max < w) {
                winner_ = i;
                nwinners = 1;
                max = w;
                total_utility = 0.0;
                sum_it = true;
            }
            if (sum_it) {
                total_utility += candidates^[i].utility_;
            }
            if (w == 0) {
                loser = i;
            }

            /* set bit for condorcet ordering. */
            counts |= 1 << w;
        }

        /* no winner if there's a cycle. */
        if (nwinners > 1) {
            println("Condorcet cycle exists.");
            winner_ = -1;
            ++g_guthrie.condorcet_cycles_;
        }

        /* check if there is a condorcet ordering. */
        if (nwinners == 1) {
            println("Condorcet ordering exists.");
            candidate = ^candidates^[winner_];
            g_guthrie.total_utility_ordering_ += candidate^.utility_;
        }

        /*
        average the utility for all candidates in the cycle if any.
        accumulate for the summary.
        */
        float64 utility = total_utility / (float64=nwinners);
        total_utility_ += utility;

        /* accumulate correlations. */
        if (winner_ == g_guthrie.guthrie_.winner_) {
            ++is_winner_;
        }

        /* return results. */
        g_guthrie.condorcet_loser_ = loser;
    }

    virtual void print_satisfaction() {
        ntrials float64 = g_guthrie.ntrials_;
        condorcet_cycles float64 = g_guthrie.condorcet_cycles_;
        non_cycle_trials float64 = ntrials - condorcet_cycles;
        float64 average_utility_ordering = g_guthrie.total_utility_ordering_ / non_cycle_trials;
        float64 satisfaction_ordering = g_guthrie.calculate_satisfaction(average_utility_ordering, g_guthrie.accumulated_);

        /* pretty print the line. */
        println(String + " Condorcet (winner exists)    : " + satisfaction_ + " (" + satisfaction_ordering + ")");
    }

    virtual void print_correlation() {
        non_cycle_trials float64 = g_guthrie.ntrials_ - g_guthrie.condorcet_cycles_;
        float64 correlation_max = 100.0 * (float64=is_winner_) / non_cycle_trials;

        /* pretty print the line. */
        println(String + " Condorcet (winner exists)    : " + correlation_ + "% (" + correlation_max + "%)");
    }
}

/*
find the coombs winner.
*/
ElectoralMethod : CoombsMethod() {
    void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_guthrie.ncandidates_;
        candidates = ^g_guthrie.candidates_;
        bloc_map = ^g_guthrie.bloc_map_;

        winner_ = -1;

        /* copy the voter blocs. */
        nblocs int = bloc_map^.size();
        RankedChoices ranked_choices;
        ranked_choices.reserve(nblocs);
        for (it : bloc_map^) {
            RankedChoice ranked_choice;
            ranked_choice.rankings_ = it^.rankings_;
            ranked_choice.size_ = it^.bloc_.size_;
            ranked_choices.append(ranked_choice);
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
        }

        /* winner is the last elligible candidate. */
        winner_ = elligible[0];

        /* accumulate the utility. */
        candidate = ^candidates^[winner_];
        total_utility_ += candidate^.utility_;

        /* accumulate correlations. */
        if (winner_ == g_guthrie.guthrie_.winner_) {
            ++is_winner_;
        }
    }
}

/*
find the guthrie winner.
*/
ElectoralMethod : GuthrieMethod() {
    void find_winner(
        bool quiet = kQuiet
    ) {
        bool show_everything = !quiet;
        if (kShowCoombsRounds == false) { void;
            show_everything = false;
        }
        bool show_required = !quiet;

        int ncandidates = g_guthrie.ncandidates_;
        candidates = ^g_guthrie.candidates_;
        int total_support = g_guthrie.total_support_;

        Vector<int> counts;
        counts.resize(ncandidates);

        /*
        normally we can find the winner in N-1 rounds.
        unless there's a tie in the last round.
        */
        for (int round = 1) (/*round < ncandidates_*/) {++round;} {
            if (show_everything) {
                println(String + "Round " + round + ":");
            }

            /* phase 1: count first place votes. */

            /* initialize the counts */
            for (i : 0..ncandidates) {
                counts[i] = 0;
            }

            /* count first place votes. */
            for (candidate : candidates^) {
                int favorite = candidate^.rankings_[0];
                counts[favorite] += candidate^.support_;
            }

            /* show first place vote counts. */
            bool show_it = show_everything;
            if (show_required && round == 1) {
                show_it = true;
            }
            if (show_it) {
                println("First place vote counts:");
                for (i : 0..ncandidates) {
                    candidate = ^candidates^[i];
                    println(String + " " + candidate^.name_ + ": " + counts[i]);
                }
            }

            /* check for majority. */
            for (i : 0..ncandidates) {
                if (2*counts[i] > total_support) {
                    winner_ = i;
                    if (show_required) {
                        candidate = ^candidates^[i];
                        println(String + candidate^.name_ + " wins Guthrie voting in round "
                            + round +".");
                    }
                    return;
                }
            }

            /* initialize the counts */
            for (i : 0..ncandidates) {
                counts[i] = 0;
            }

            /*
            count last place votes.
            */
            int last_index = ncandidates - round;
            for (candidate : candidates^) {
                int worst = candidate^.rankings_[last_index];
                counts[worst] += candidate^.support_;
            }

            /* find the candidate with the most last place votes. */
            int loser = 0;
            int loser_count = -1;
            for (i : 0..ncandidates) {
                int count = counts[i];
                /*
                we have this implied bias that the first candidate in the list wins ties.
                however in case, we're looking for the loser.
                if there's a tie we want to find the last candidate in the list.
                hence the comparison is greater than or equal to.
                instead of just greater than.
                */
                if (count >= loser_count) {
                    loser = i;
                    loser_count = count;
                }
            }

            /* remove the loser from the candidate rankings. */
            for (candidate : candidates^) {
                for (it^: candidate^.rankings_) {
                    if (it^ == loser) {
                        candidate^.rankings_.remove(it);
                        break;
                    }
                }
            }

            if (show_everything) {
                println("No candidate has a majority.");

                println("Last place vote counts:");
                for (i : 0..ncandidates) {
                    candidate = ^candidates^[i];
                    println(String + " " + candidate^.name_ + ": " + counts[i]);
                }
                println(String + " Candidate " + candidates^[loser].name_ +
                    " has the most last place votes - eliminated.");

                println("Updated rankings:");
                for (candidate : candidates^) {
                    ss = String + " " + candidate^.name_ + ":";
                    for (i : 0..last_index) {
                        int rank = candidate^.rankings_[i];
                        if (i > 0) {
                            ss += " >";
                        }
                        ss = ss + " " + candidates^[rank].name_;
                    }
                    println(ss);
                }
            }
        }
    }

    virtual void print_satisfaction() {
        /* calculate strategic voting total utility. */
        ntrials float64 = g_guthrie.ntrials_;
        float64 average_utility_strategic = g_guthrie.total_utility_strategic_ / ntrials;
        float64 satisfaction_strategic = g_guthrie.calculate_satisfaction(average_utility_strategic, g_guthrie.accumulated_);

        /* pretty print the line. */
        println(String + " Guthrie (strategic)          : " + satisfaction_ + " (" + satisfaction_strategic + ")");
    }

    virtual void print_correlation() {
        /*
        nothing to print.
        guthrie correlates with itself 100% of the time.
        */
    }
}

/*
instant runoff voting.
*/
ElectoralMethod : InstantRunoffMethod() {
    void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_guthrie.ncandidates_;
        candidates = ^g_guthrie.candidates_;
        bloc_map = ^g_guthrie.bloc_map_;
        int nvoters = g_guthrie.electorate_.nvoters_;

        winner_ = -1;

        /* copy the voter blocs. */
        nblocs int = bloc_map^.size();
        RankedChoices ranked_choices;
        ranked_choices.reserve(nblocs);
        for (it : bloc_map^) {
            RankedChoice ranked_choice;
            ranked_choice.rankings_ = it^.rankings_;
            ranked_choice.size_ = it^.bloc_.size_;
            ranked_choices.append(ranked_choice);
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
        if (winner_ == g_guthrie.guthrie_.winner_) {
            ++is_winner_;
        }
    }
}

/*
find the plurality winner.
*/
ElectoralMethod : PluralityMethod() {
    void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_guthrie.ncandidates_;
        candidates = ^g_guthrie.candidates_;
        bloc_map = ^g_guthrie.bloc_map_;
        int nvoters = g_guthrie.electorate_.nvoters_;

        Vector<int> counts;
        counts.reserve(ncandidates);
        counts.resize(ncandidates);

        /* clear the counts. */
        for (i : 0..ncandidates) {
            counts[i] = 0;
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

        /* accumulate the utility. */
        candidate = ^candidates^[winner_];
        total_utility_ += candidate^.utility_;

        /* was it a majority? */
        if (2*votes > nvoters) {
            ++g_guthrie.majority_winners_;
        }

        /* accumulate correlations. */
        if (winner_ == g_guthrie.guthrie_.winner_) {
            ++is_winner_;
        }
    }
}

/*
find the plurality runoff winner.
if there is no majority, then the top two go head to head.
*/
ElectoralMethod : PluralityRunoffMethod() {
    void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_guthrie.ncandidates_;
        candidates = ^g_guthrie.candidates_;
        bloc_map = ^g_guthrie.bloc_map_;
        int nvoters = g_guthrie.electorate_.nvoters_;

        Vector<int> counts;
        counts.reserve(ncandidates);
        counts.resize(ncandidates);

        /* clear the counts. */
        for (i : 0..ncandidates) {
            counts[i] = 0;
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
            HeadToHead runoff;
            runoff.resolve(winner_, second);

            /* overwrite the winner. */
            winner_ = runoff.winner_;
        }

        /* accumulate the utility. */
        candidate = ^candidates^[winner_];
        total_utility_ += candidate^.utility_;

        /* accumulate correlations. */
        if (winner_ == g_guthrie.guthrie_.winner_) {
            ++is_winner_;
        }
    }
}

/*
find the range (score) voting winner.
assign a range based on utilities for each block.
*/
ElectoralMethod : RangeMethod() {
    void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_guthrie.ncandidates_;
        candidates = ^g_guthrie.candidates_;
        bloc_map = ^g_guthrie.bloc_map_;

        /* initialize rating for each candidate. */
        Vector<float64> ratings;
        ratings.resize(ncandidates);
        for (i : 0..ncandidates) {
            ratings[i] = 0.0;
        }

        /* for each voter bloc. */
        for (it : bloc_map^) {
            float64 first = it^.bloc_.utilities_[0];
            float64 last = it^.bloc_.utilities_[ncandidates-1];
            float64 denom = first - last;

            for (i : 0..ncandidates) {
                float64 utility = it^.bloc_.utilities_[i];
                float64 rating = (utility - last) / denom;
                int which = it^.rankings_[i];
                ratings[which] += rating * (float64=it^.bloc_.size_);
            }
        }

        /* find the largest rating. */
        winner_ = -1;
        float64 max = -1.0;
        for (i : 0..ncandidates) {
            float64 rating = ratings[i];
            if (max < rating ) {
                winner_ = i;
                max = rating;
            }
        }

        /* accumulate the utility. */
        candidate = ^candidates^[winner_];
        total_utility_ += candidate^.utility_;

        /* accumulate correlations. */
        if (winner_ == g_guthrie.guthrie_.winner_) {
            ++is_winner_;
        }
    }
}

/*
find the star winner.
assign a score from 0 to 5 based on utilities for each block.
*/
ElectoralMethod : StarMethod() {
    void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_guthrie.ncandidates_;
        candidates = ^g_guthrie.candidates_;
        bloc_map = ^g_guthrie.bloc_map_;

        /* initialize rating for each candidate. */
        Vector<int> ratings;
        ratings.resize(ncandidates);
        for (i : 0..ncandidates) {
            ratings[i] = 0;
        }

        /* for each voter bloc. */
        for (it : bloc_map^) {
            float64 first = it^.bloc_.utilities_[0];
            float64 last = it^.bloc_.utilities_[ncandidates-1];
            float64 denom = first - last;

            for (i : 0..ncandidates) {
                float64 utility = it^.bloc_.utilities_[i];
                float64 rating = (utility - last) / denom;
                score int = math:floor(6.0 * rating);
                score = math:clamp(score, 0, 5);
                int which = it^.rankings_[i];
                ratings[which] += score * it^.bloc_.size_;
            }
        }

        /* find the largest score. */
        int a = -1;
        int max = -1;
        for (i : 0..ncandidates) {
            int rating = ratings[i];
            if (max < rating ) {
                a = i;
                max = rating;
            }
        }

        /* find the second largest score. */
        int b = -1;
        max = -1;
        for (i : 0..ncandidates) {
            if (i == a) {
                continue;
            }
            int rating = ratings[i];
            if (max < rating ) {
                b = i;
                max = rating;
            }
        }

        /* find which is ranked higher most often. */
        int counta = 0;
        int countb = 0;
        for (it : bloc_map^) {
            float64 first = it^.bloc_.utilities_[0];
            float64 last = it^.bloc_.utilities_[ncandidates-1];
            float64 denom = first - last;

            int scorea = -1;
            int scoreb = -1;
            for (i : 0..ncandidates) {
                float64 utility = 0.0;
                float64 rating = 0.0;
                int which = it^.rankings_[i];
                if (which == a) {
                    utility = it^.bloc_.utilities_[i];
                    rating = (utility - last) / denom;
                    scorea = (int=math:floor(6.0 * rating));
                    scorea = (int=math:clamp(scorea, 0, 5));
                } else if (which == b) {
                    utility = it^.bloc_.utilities_[i];
                    rating = (utility - last) / denom;
                    scoreb = (int=math:floor(6.0 * rating));
                    scoreb = (int=math:clamp(scoreb, 0, 5));
                }
            }

            if (scorea > scoreb) {
                counta += it^.bloc_.size_;
            } else if (scoreb > scorea) {
                countb += it^.bloc_.size_;
            }
        }
        if (counta >= countb) {
            winner_ = a;
        } else {
            winner_ = b;
        }

        /* accumulate the utility. */
        candidate = ^candidates^[winner_];
        total_utility_ += candidate^.utility_;

        /* accumulate correlations. */
        if (winner_ == g_guthrie.guthrie_.winner_) {
            ++is_winner_;
        }
    }
}
