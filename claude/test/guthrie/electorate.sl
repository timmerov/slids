/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/*
manage the electorate of voters.

there are several ways to model the electorate:

1. single axis - simplest.
2. multiple axes with equal weights.
3. multiple axes where the weights decrease geometrically.

per axis:

A. uniform - distributed evenly from 0.0 to 1.0.
B. random - placed at random.
with large electorates, is indistinguishable from a uniform distribution.
C. clustered - voters are normally distributed around one of several points.
how many clusters? how much spread? to be determined.
*/

import dump;
import electorate;
import math;
import random;

Position() {
    /*
    calculate the utility of the other's position.
    utility is defined to be 1.0 when distance is 0.0.
    utility decreases with distance.
    */
    float64 utility(
        Position^ other
    ) {
        int naxes = (int=axis_.size());
        if (naxes == 1) {
            float64 utility = 1.0 - math:fabs(axis_[0] - other^.axis_[0]);
            return utility;
        }
        float64 sum2 = 0.0;
        for (int i : 0..naxes) {
            float64 dx = axis_[i] - other^.axis_[i];
            sum2 += dx * dx;
        }
        float64 dist = math:sqrt(sum2);

        /** the simplest model for utility is 1.0 - distance. **/
        float64 utility = 1.0 - dist;

        /*
        ==experimental==
        non-linear utility.

        hack the distance to be more of a utility distance.
        near 0.0 is even nearer 1.0.
        at a threshold value the utility equals the 1.0 - distance.
        over the threshold asymptotically approaches 0.0.
        a threshold of about 0.3 t0 0.4 feels about right.
        the exponent factor would be 3.2 to 4.0.
        */
        /*
        constexpr float64 kScaleFactor = 3.9;
        float64 utility = std::exp(- kScaleFactor * dist * dist);
        */

        return utility;
    }

    /** format the position as a string. **/
    String to_string() {
        String s;
        int naxes = (int=axis_.size());
        if (naxes == 1) {
            s = axis_[0];
        } else {
            s = "{";
            for (int i : 0..naxes) {
                if (i > 0) {
                    s += ", ";
                }
                s += axis_[i];
            }
            s += "}";
        }
        return s;
    }

    /** for sorting **/
    /*bool operator < (const Position& other) const;*/
}

Cluster(
    /** size **/
    int count_ = 0,

    /** position **/
    Position position_
) {
    /** for sorting **/
    /*bool operator < (const Cluster& other) const
    {
        return (position_ < other.position_);
    }*/
}

/*
/** for sorting **/
bool Position::operator < (const Position& other) const
{
    return (axis_[0] < other.axis_[0]);
}

/** for sorting **/
bool Voter::operator < (const Voter& other) const
{
    return (position_ < other.position_);
}
*/

Electorate() {
    void init() {
        /** allocate space for the voters and candidates **/
        voters_.resize(nvoters_);

        /** allocate space for the positions. **/
        size_position_axes();

        /** distribute the voter by the chosen method. **/
        switch (method_) {
            default:
            kElectorateUniform: {
                ranked();
            } kElectorateRandom: {
                random();
            } kElectorateClusters: {
                clusters();
            }
        }

        /** normalize non-uniform distributions. **/
        if (method_ != kElectorateUniform) {
            normalize();
        }
    }

    /** allocate space for the voter positions. **/
    void size_position_axes() {
        for (voter : voters_) {
            voter^.position_.axis_.resize(naxes_);
        }
    }

    /**
    evenly distribute voters from 0 to 1 along a single axis.
    this is the simplest model of the electorate.
    **/
    void ranked() {
        println(String + "Electorate is uniform ranked order.");
        nvoters float64 = nvoters_;
        float64 offset = 0.5 / nvoters;
        for (int i : 0..nvoters_) {
            axis = ^voters_[i].position_.axis_;
            axis^[0] = offset + (float64=i) / nvoters;
        }
    }

    /**
    randomly distribute voters along the axes.
    **/
    void random() {
        println(String + "Electorate is random.");
        for (int i : 0..nvoters_) {
            axis = ^voters_[i].position_.axis_;
            for (int k : 0..naxes_) {
                axis^[k] = rng:generate_float64();
            }
        }
    }

    /**
    chinese restaurant process.
    **/
    void clusters() {
        println(String + "Electorate is clustered.");

        /** create empty clusters. **/
        Clusters clusters;
        clusters.resize(nclusters_);
        for (cluster : clusters) {
            cluster^.count_ = 0;
            cluster^.position_.axis_.reserve(naxes_);
            cluster^.position_.axis_.resize(naxes_);
            for (int i : 0..naxes_) {
                cluster^.position_.axis_[i] = rng:generate_float64();
            }
        }
        // ==tsc== skipped
        //std::sort(clusters.begin(), clusters.end());

        for (int i : 0..nvoters_) {
            seat_voter(clusters, i);
        }
    }

    /**
    pick a random cluster and join it.
    or create a new one.

    maybe we fix the number of clusters.
    and the first voters fill empty clusters.
    how many clusters?
    maybe 2 times number of candidates.
    **/
    void seat_voter(
        Clusters^ clusters,
        int k
    ) {
        /**
        what should the standard deviation be?
        should it start small and grow with the size of the table?
        this number found by trial and error.
        **/
        const float64 kStdDev = 0.07;

        /** pick a cluster. **/
        int where = 0;
        nclusters int = clusters^.size();
        if (k < nclusters) {
            /** seat the first n voters in their own cluster. **/
            where = k;
        } else {
            /** populare clusters attract more people. **/
            int rn = rng:generate_int(k);
            for (cluster : clusters^) {
                rn -= cluster^.count_;
                if (rn < 0) {
                    break;
                }
                ++where;
            }
            if (where >= nclusters) {
                println(String + "=tsc= " + ##line + ": uh oh! where=" + where + " > " + nclusters + "=" + nclusters);
            }
        }

        /** add the voter to the cluster. **/
        cluster = ^clusters^[where];
        cluster^.count_ += 1;

        /** position the voter. **/
        voter = ^voters_[k];
        for (int i : 0..naxes_) {
            float64 rn = rng:normal() * kStdDev;
            float64 position = cluster^.position_.axis_[i] + rn;
            voter^.position_.axis_[i] = position;
        }
    }

    /**
    normalize all axes.
    **/
    void normalize() {
        float64 weight = 1.0;
        for (int axis : 0..naxes_) {
            normalize(axis, weight);
            weight *= axis_weight_decay_;
        }

        // ==tsc== skipped
        //std::sort(voters_.begin(), voters_.end());
    }

    /**
    normalize the voters so they range from 0 to 1 along this axis.
    **/
    void normalize(
        int axis,
        float64 weight
    ) {
        float64 mn = 1e99;
        float64 mx = -1e99;
        for (voter : voters_) {
            mn = math:fmin(mn, voter^.position_.axis_[axis]);
            mx = math:fmax(mx, voter^.position_.axis_[axis]);
        }

        float64 scale = weight / (mx - mn);
        for (voter : voters_) {
            float64 pos = voter^.position_.axis_[axis];
            pos -= mn;
            pos *= scale;
            //pos = std::clamp(pos, 0.0, 1.0);
                pos = math:fmax(0.0, pos);
                pos = math:fmin(pos, 1.0);
            voter^.position_.axis_[axis] = pos;
        }
    }

    /**
    show all axes.
    **/
    void show_distribution() {
        println(String + "Electorate distribution:");
        for (int axis : 0..naxes_) {
            if (naxes_ > 1) {
                println(String + " Axis: " + axis);
            }
            show_distribution(axis);
        }
    }

    /**
    show distribution for one axis.
    **/
    void show_distribution(
        int axis
    ) {
        const int kNBins = 20;
        Vector<int> bins;
        bins.resize(kNBins);
        for (int i : 0..kNBins) {
            bins[i] = 0;
        }
        for (voter : voters_) {
            float64 position = voter^.position_.axis_[axis];
            i int = math:floor(position * kNBins);
            //i = std::clamp(i, 0, kNBins - 1);
                if (i < 0) {
                    i = 0;
                } else if (i >= kNBins) {
                    i = kNBins - 1;
                }
            ++bins[i];
        }
        nvoters float64 = voters_.size();
        for (int i : 0..kNBins) {
            float64 mx = (float64=i+1) / kNBins;
            float64 frac = 100.0 * (float64=bins[i]) / nvoters;
            println(String + "  " + mx + ": " + bins[i] + " " + frac + "%");
        }
    }
}
