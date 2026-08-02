/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
model the electorate.
**/

#pragma once

#include <string>
#include <vector>


/** initialization options. **/
constexpr int kElectorateUniform = 0;
constexpr int kElectorateRandom = 1;
constexpr int kElectorateClusters = 2;


class Position {
public:
    Position() = default;
    ~Position() = default;

    std::vector<double> axis_;

    /** return the utility to another position. **/
    double utility(const Position& other) noexcept;

    /** format the position as a string. **/
    std::string to_string() noexcept;

    /** for sorting **/
    bool operator < (const Position& other) const;
};

class Voter {
public:
    Voter() = default;
    ~Voter() = default;

    /** position along the axis ranges from 0..1 **/
    Position position_;

    /** for sorting **/
    bool operator < (const Voter& other) const;
};
typedef std::vector<Voter> Voters;

class Cluster;
typedef std::vector<Cluster> Clusters;

class Electorate {
public:
    Electorate() = default;
    ~Electorate() = default;

    /** configuration. **/
    int nvoters_ = 100;
    int method_ = kElectorateUniform;
    int naxes_ = 1;
    /** minor axes have reduced range (and utility). **/
    double axis_weight_decay_ = 1.0;
    int nclusters_ = 6;

    /** data **/
    Voters voters_;

    /** set configuration parameters above. required. **/
    void init() noexcept;

    /** what did we get. **/
    void show_distribution() noexcept;

    /** private implementation. **/
private:
    void size_position_axes() noexcept;
    void ranked() noexcept;
    void random() noexcept;
    void clusters() noexcept;
    void seat_voter(Clusters &clusters, int k) noexcept;
    void normalize() noexcept;
    void normalize(int axis, double weight) noexcept;
    void show_distribution(int axis) noexcept;
};
