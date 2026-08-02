/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
wraopper class for c++ random number generator.
**/

#pragma once

#include <cstdint>


class RandomNumberGenerator {
public:
    /**
    must be called first.
    use a randomly generated seed if the parameter is 0.
    **/
    static void init(std::uint64_t seed) noexcept;

    /** exposed so cases can be reproduced. **/
    static std::uint64_t get_seed() noexcept;

    /** uniform from 0.0 to 1.0-. **/
    static double generate() noexcept;

    /** uniform from 0 to max-1 **/
    static int generate(int max) noexcept;

    /** normal distribritution with mean 0.0 and standard deviation 1.0. **/
    static double normal() noexcept;
};

typedef RandomNumberGenerator Rng;
