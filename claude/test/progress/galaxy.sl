/*
simulate the milky way galaxy.

float64 is about 16 digits of precision.

the milky way is about 100,000 ly in diameter.
GMM/r^2 will be about 1e-10.
leaving about 6 digits precision.
we will want to accumulate small numbers first.

we will approximate the galaxy as a set of concentric rings.
each ring has some total mass,
rotates at some speed around the center.
we want to find a steady state where things balance.
where the net inward/outward force on each ring is zero.

we pick some arbitrary characteristic distance.
say 100 ly.
there will be 10,000 rings.
the ring will be approximated as point masses spaced at
approximately the characteristic distance around the ring.
the gravitational force will be calculated and summed
for each piece of the ring.
and summed over all rings.

we will do this using newton and relativity.
in newton gravity propagates instantly.
in relativity, it propagates at c.
so we need to figure out where each point mass was when
it "emitted" the gravity that is here now.

we assume there is a central bulge at the center of the galaxy.
it has a fixed mass.
it does not rotate or move in/out.
it has some radius that's larger than the characteristic distance.
the innermost ring is at this radius.
*/

import dump;
import math;
import string;
import vector;

/* meters per light year. */
const float64 kMetersPerLightyear = 9.461e15;

/* seconds per year. */
const float64 kSecondsPerYear = 3.154e7;

/* Gravitational constant: m^3 / kg / s^2 */
const float64 kG = 6.6743e-11;

/* estimated mass of milky way: 2e42 to 6e42 kg */
const float64 kGalaxyMass = 3e42;

/* diameter and radius of milky way. */
const float64 kGalaxyDiameterLY = 100_000.0;
const float64 kGalaxyDiameter = kGalaxyDiameterLY * kMetersPerLightyear;
const float64 kGalaxyRadius = kGalaxyDiameter / 2.0;

/* characteristic distance aka resolution. */
const float64 kScaleLY = 3_000.0; // 100.0;
const float64 kScale = kScaleLY * kMetersPerLightyear;

/* estimated mass central bulge: 1.5e40 to 4e40 kg */
const float64 kCentralBulgeMass = 2e40;

/* estimated radius of central bulge: 3000 to 6500 ly. */
const float64 kCentralBulgeRadiusLY = 4_000.0;
const float64 kCentralBulgeRadius = kCentralBulgeRadiusLY * kMetersPerLightyear;

/* estimated total mass of rings: 2e42 to 6e42 kg */
const float64 kRingsMass = kGalaxyMass - kCentralBulgeMass;

/* estimated rotation period of the galaxy: 225 to 250 million years. */
const float64 kRotationPeriodYr = 237_000_000.0;
const float64 kRotationPeriod = kRotationPeriodYr * kSecondsPerYear;

/* we assume the entire galaxy rotates as a rigid disk. */
const float64 kAngularVelocity = 2.0 * math:kPi64 / kRotationPeriod;

/* separate the mass of the galaxy into rings. */
Ring(
    /* radius from center of galaxy. */
    float64 radius_,

    /* total mass of the ring. */
    float64 mass_,

    /* angular velocity. */
    float64 velocity_,

    /* number of slices. */
    int slices_,

    /* inward and spinward acceleration. */
    float64 inward_,
    float64 spinward_
) {
}
alias Rings = Vector<Ring>;

alias Floats = Vector<float64>;

Galaxy(
    int nrings_,
    Rings rings_,

    /* sort numbers before summing them. */
    Floats ins_,
    Floats spins_
) {
    void run() {
        init();
        acceleration();
    }

    void init() {
        /* allocate space for rings. */
        nrings = (kGalaxyRadius - kCentralBulgeRadius) / kScale;
        //dump(#nrings_f);
        nrings = math:round(nrings);
        nrings_ = (int=nrings) + 1;
        //dump(#nrings);
        rings_.resize(nrings_);
        println(String + "Divided the galaxy into " + nrings_ + " rings.");

        /*
        initialize the rings.
        sum the radii - proportional to circumference.
        */
        float64 sum_radius = 0.0;
        step_radius = (kGalaxyRadius - kCentralBulgeRadius) / (nrings_ - 1);
        total_slices = 0;
        for (int i : 0..nrings_) {
            radius = step_radius * i + kCentralBulgeRadius;
            sum_radius += radius;
            //dump(#radius);

            /* divide the ring into equal width slices. */
            circumference = 2.0 * math:kPi64 * radius;
            divs = circumference / kScale;
            divs = math:round(divs);
            slices = (int=divs);
            total_slices += slices;
            //dump(#divs);

            ring = ^rings_[i];
            ring^.radius_ = radius;
            ring^.slices_ = slices;
            ring^.velocity_ = kAngularVelocity;
        }
        //dump(#sum_radius);
        dump(#total_slices);

        /* distribute the mass evenly by area. */
        mass_per_radius = kRingsMass / sum_radius;
        for (int i : 0..<nrings_) {
            ring = ^rings_[i];
            ring^.mass_ = mass_per_radius * ring^.radius_;
            //dump(#ring^.mass_);
        }

        nins = total_slices + 2 * nrings_;
        nspins = total_slices;
        ins_.reserve(nins);
        spins_.reserve(nspins);
        dump(#nins);
        dump(#nspins);
    }

    void acceleration() {

        for (i : 0..nrings_) {
            println(String + "Calculating total acceleration on ring " + i + ".");
            for (k : 0..nrings_) {
                acceleration(i, k);
            }

            sort(ins_);
            sort(spins_);

            inward = sum(ins_);
            spinward = sum(spins_);
            dump(#inward);
            dump(#spinward);

            ring = ^rings_[i];
            ring^.inward_ = inward;
            ring^.spinward_ = spinward;

            ins_.clear();
            spins_.clear();
        }
    }

    void acceleration(int on_idx, int by_idx) {
        //println(String + "Calculating acceleration on ring " + on_idx + " caused by ring " + by_idx + ".");

        /* the rings. */
        on_ring = ^rings_[on_idx];
        by_ring = ^rings_[by_idx];

        /* the mass of each slice. */
        by_mass = by_ring^.mass_ / by_ring^.slices_;

        /* radius of each ring. */
        on_r = on_ring^.radius_;
        by_r = by_ring^.radius_;

        /* angular velocity of the on ring. */
        on_velocity = on_ring^.velocity_;
        on_velocity2 = on_velocity * on_velocity;

        /* centripetal acceleration is outward - negative. */
        in = - on_velocity2 * on_r;
        ins_.append(in);

        /* gravity of central bulge. */
        r2 = on_r * on_r;
        in = kG * kCentralBulgeMass / r2;
        ins_.append(in);

        /*
        contribution from each slice.
        skip the contribution from the slice on itself.
        */
        nslices = by_ring^.slices_;
        first = 0;
        if (on_idx == by_idx) {
            first = 1;
        }
        for (slice : first..nslices) {
            angle = math:k2Pi64 * slice / nslices;
            x = by_r * math:cos(angle);
            y = by_r * math:sin(angle);
            dx = x - on_r;
            dy = y;
            d2 = dx*dx + dy*dy;
            a = kG * by_mass / d2;
            d = math:sqrt(d2);

            in = a * dx / d;
            ins_.append(in);

            spin = a * dy / d;
            spins_.append(spin);
        }
    }

    void sort(Floats^ vec) {
        sz = vec^.size();
        sort(vec, 0, sz-1);

        /*
        println("sorted:");
        for (value : vec^) {
            println(String + value);
        }
        */
    }

    void sort(Floats^ vec, intptr first, intptr last) {
        /* done */
        if (first >= last) {
            return;
        }

        /* grab the pivot. */
        raw_pivot = vec^[last];
        pivot = raw_pivot;
        if (pivot < 0.0) {
            pivot = - pivot;
        }

        /* loop. */
        scan = first;
        limit = last;
        while {
            /*
            small values stay in place.
            big values move to the end.
            */
            value = vec^[scan];
            if (value < 0.0) {
                value = - value;
            }
            if (value <= pivot) {
                ++scan;
            } else {
                vec^[limit] = vec^[scan];
                --limit;
                vec^[scan] = vec^[limit];
            }
        } (scan < limit);

        /* place the pivot at the correct place. */
        vec^[scan] = raw_pivot;

        /* recurse. */
        sort(vec, first, scan-1);
        sort(vec, scan+1, last);
    }

    float64 sum(Floats^ vec) {
        float64 total = 0.0;
        for (value : vec^) {
            total += value;
        }
        return total;
    }
}

int32 main() {
    println("Hello, World!");
    Galaxy galaxy;
    galaxy.run();
    println("Goodbye, World!");
    return 0;
}
