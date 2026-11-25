/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
approval runoff electoral method.
*/

import slids.string;
import slids.vector;
import approval_runoff;
import method;
import runner;
import settings;
alias slids;

/*
find the two round approval winner where the top two have a runoff.
use the approvals stored when the bloc was created.
*/
ElectoralMethod : ApprovalRunoffMethod() {

    _() { }
    virtual ~() { }
    virtual void find_winner(
        bool quiet = kQuiet
    ) {
        int ncandidates = g_runner.ncandidates_;
        candidates = ^g_runner.candidates_;
        bloc_map = ^g_runner.bloc_map_;

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
        upset = g_runner.pairwise_.is_winner(second, winner_);

        /* overwrite the winner. */
        if (upset) {
            winner_ = second;
        }

        /* accumulate the utility. */
        candidate = ^candidates^[winner_];
        total_utility_ += candidate^.utility_;

        /* accumulate correlations. */
        if (winner_ == g_runner.guthrie_.winner_) {
            ++is_winner_;
        }
    }
}
