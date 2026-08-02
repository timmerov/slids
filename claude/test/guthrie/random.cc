/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
wraopper class for c++ random number generator.

some methods:
uniform double from 0.0 to 1.0-.
int from 0 to N-1
normal distribution with mean 0.0 standard deviation 1.0.
**/

#include <chrono>
#include <cstdint>
#include <random>

namespace {

class RngImpl {
public:
    std::uint64_t seed_ = 0;
    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> unif_;
    std::normal_distribution<double> norm_;

    void init(
        std::uint64_t seed
    ) noexcept {
        if (seed == 0) {
            seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        }
        seed_ = seed;
        std::seed_seq ss{uint32_t(seed & 0xffffffff), uint32_t(seed>>32)};
        rng_.seed(ss);
        unif_ = std::uniform_real_distribution<double>(0.0, 1.0);
        norm_ = std::normal_distribution<double>(0.0, 1.0);
    }

    std::uint64_t get_seed() noexcept {
        return seed_;
    }

    double generate() noexcept {
        double x = unif_(rng_);
        return x;
    }

    int generate(int max) noexcept {
        double x = generate();
        int i = std::floor(x * double(max));
        return i;
    }

    double normal() noexcept {
        double x = norm_(rng_);
        return x;
    }
};

static RngImpl *impl_ = nullptr;

} // namespace

extern "C" {
    void rng_init(
        std::uint64_t seed
    ) noexcept {
        delete impl_;
        impl_ = new(std::nothrow) RngImpl();
        impl_->init(seed);
    }

    std::uint64_t rng_get_seed() noexcept {
        return impl_->get_seed();
    }

    double rng_generate_float64() noexcept {
        return impl_->generate();
    }

    int rng_generate_int(int max) noexcept {
        return impl_->generate(max);
    }

    double rng_normal() noexcept {
        return impl_->normal();
    }
} // extern "C"
