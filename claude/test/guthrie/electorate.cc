/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
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
**/

#include "electorate.h"
#include "random.h"

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>

#include <algorithm>
#include <cmath>
#include <iomanip>


class Cluster {
public:
    /** size **/
    int count_ = 0;

    /** position **/
    Position position_;

    /** for sorting **/
    bool operator < (const Cluster& other) const
    {
        return (position_ < other.position_);
    }
};

/**
calculate the utility of the other's position.
utility is defined to be 1.0 when distance is 0.0.
utility decreases with distance.
**/
double Position::utility(
    const Position& other
) noexcept {
    int naxes = axis_.size();
    if (naxes == 1) {
        double utility = 1.0 - std::abs(axis_[0] - other.axis_[0]);
        return utility;
    }
    double sum2 = 0.0;
    for (int i = 0; i < naxes; ++i) {
        double dx = axis_[i] - other.axis_[i];
        sum2 += dx * dx;
    }
    double dist = std::sqrt(sum2);

    /** the simplest model for utility is 1.0 - distance. **/
    double utility = 1.0 - dist;

    /**
    ==experimental==
    non-linear utility.

    hack the distance to be more of a utility distance.
    near 0.0 is even nearer 1.0.
    at a threshold value the utility equals the 1.0 - distance.
    over the threshold asymptotically approaches 0.0.
    a threshold of about 0.3 t0 0.4 feels about right.
    the exponent factor would be 3.2 to 4.0.
    **/
#if 0
    constexpr double kScaleFactor = 3.9;
    double utility = std::exp(- kScaleFactor * dist * dist);
#endif

    return utility;
}

/** format the position as a string. **/
std::string Position::to_string() noexcept {
    std::stringstream ss;
    int naxes = axis_.size();
    if (naxes == 1) {
        ss<<axis_[0];
    } else {
        ss<<"{";
        for (int i = 0; i < naxes; ++i) {
            if (i > 0) {
                ss<<", ";
            }
            ss<<axis_[i];
        }
        ss<<"}";
    }
    return ss.str();
}

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

void Electorate::init() noexcept {
    /** allocate space for the voters and candidates **/
    voters_.resize(nvoters_);

    /** allocate space for the positions. **/
    size_position_axes();

    /** distribute the voter by the chosen method. **/
    switch (method_) {
    default:
    case kElectorateUniform:
        ranked();
        break;

    case kElectorateRandom:
        random();
        break;

    case kElectorateClusters:
        clusters();
        break;
    }

    /** normalize non-uniform distributions. **/
    if (method_ != kElectorateUniform) {
        normalize();
    }
}

/** allocate space for the voter positions. **/
void Electorate::size_position_axes() noexcept {
    for (auto&& voter : voters_) {
        voter.position_.axis_.resize(naxes_);
    }
}

/**
evenly distribute voters from 0 to 1 along a single axis.
this is the simplest model of the electorate.
**/
void Electorate::ranked() noexcept {
    LOG("Electorate is uniform ranked order.");
    double nvoters = double(nvoters_);
    double offset = 0.5 / nvoters;
    for (int i = 0; i < nvoters_; ++i) {
        auto& axis = voters_[i].position_.axis_;
        axis[0] = offset + double(i) / nvoters;
    }
}

/**
randomly distribute voters along the axes.
**/
void Electorate::random() noexcept {
    LOG("Electorate is random.");
    for (int i = 0; i < nvoters_; ++i) {
        auto& axis = voters_[i].position_.axis_;
        for (int k = 0; k < naxes_; ++k) {
            axis[k] = Rng::generate();
        }
    }
}

/**
chinese restaurant process.
**/
void Electorate::clusters() noexcept {
    LOG("Electorate is clustered.");

    /** create empty clusters. **/
    Clusters clusters;
    clusters.resize(nclusters_);
    for (auto&& cluster : clusters) {
        cluster.count_ = 0;
        cluster.position_.axis_.reserve(naxes_);
        cluster.position_.axis_.resize(naxes_);
        for (int i = 0; i < naxes_; ++i) {
            cluster.position_.axis_[i] = Rng::generate();
        }
    }
    std::sort(clusters.begin(), clusters.end());

    for (int i = 0; i < nvoters_; ++i) {
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
void Electorate::seat_voter(
    Clusters& clusters,
    int k
) noexcept {
    /**
    what should the standard deviation be?
    should it start small and grow with the size of the table?
    this number found by trial and error.
    **/
    constexpr double kStdDev = 0.07;

    /** pick a cluster. **/
    int where = 0;
    int nclusters = clusters.size();
    if (k < nclusters) {
        /** seat the first n voters in their own cluster. **/
        where = k;
    } else {
        /** populare clusters attract more people. **/
        int rn = Rng::generate(k);
        for (auto&& cluster : clusters) {
            rn -= cluster.count_;
            if (rn < 0) {
                break;
            }
            ++where;
        }
        if (where >= nclusters) {
            LOG("=tsc= "<<__LINE__<<": uh oh! where="<<where<<" > "<<nclusters<<"="<<nclusters);
        }
    }

    /** add the voter to the cluster. **/
    auto& cluster = clusters[where];
    cluster.count_ += 1;

    /** position the voter. **/
    auto& voter = voters_[k];
    for (int i = 0; i < naxes_; ++i) {
        double rn = Rng::normal() * kStdDev;
        double position = cluster.position_.axis_[i] + rn;
        voter.position_.axis_[i] = position;
    }
}

/**
normalize all axes.
**/
void Electorate::normalize() noexcept {
    double weight = 1.0;
    for (int axis = 0; axis < naxes_; ++axis) {
        normalize(axis, weight);
        weight *= axis_weight_decay_;
    }

    std::sort(voters_.begin(), voters_.end());
}

/**
normalize the voters so they range from 0 to 1 along this axis.
**/
void Electorate::normalize(
    int axis,
    double weight
) noexcept {
    double mn = 1e99;
    double mx = -1e99;
    for (auto&& voter : voters_) {
        mn = std::min(mn, voter.position_.axis_[axis]);
        mx = std::max(mx, voter.position_.axis_[axis]);
    }

    double scale = weight / (mx - mn);
    for (auto&& voter : voters_) {
        double pos = voter.position_.axis_[axis];
        pos -= mn;
        pos *= scale;
        pos = std::clamp(pos, 0.0, 1.0);
        voter.position_.axis_[axis] = pos;
    }
}

/**
show all axes.
**/
void Electorate::show_distribution() noexcept {
    LOG("Electorate distribution:");
    for (int axis = 0; axis < naxes_; ++axis) {
        if (naxes_ > 1) {
            LOG(" Axis: "<<axis);
        }
        show_distribution(axis);
    }
}

/**
show distribution for one axis.
**/
void Electorate::show_distribution(
    int axis
) noexcept {
    constexpr int kNBins = 20;
    std::vector<int> bins;
    bins.resize(kNBins);
    for (int i = 0; i < kNBins; ++i) {
        bins[i] = 0;
    }
    for (auto&& voter : voters_) {
        double position = voter.position_.axis_[axis];
        int i = std::floor(position * kNBins);
        i = std::clamp(i, 0, kNBins - 1);
        ++bins[i];
    }
    double nvoters = voters_.size();
    for (int i = 0; i < kNBins; ++i) {
        double mx = double(i+1) / kNBins;
        double frac = 100.0 * double(bins[i]) / nvoters;
        LOG("  "<<mx<<": "<<bins[i]<<" "<<frac<<"%");
    }
}
