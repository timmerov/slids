/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
approval electoral method.
*/

import slids.string;
import slids.vector;
import approval;
import method;
import runner;
import settings;
alias slids;

/*
find the approval winner.
use the approvals stored when the bloc was created.
*/
ElectoralMethod : ApprovalMethod() {

    _() { }
    virtual ~() { }
    virtual void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;
        int nvoters = g_runner.electorate_.nvoters_;

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
        if (winner_ == g_runner.guthrie_.winner_) {
            ++is_winner_;
        }
    }
}
