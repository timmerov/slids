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
const float64 kAngularVelocity2 = kAngularVelocity * kAngularVelocity;

/* separate the mass of the galaxy into rings. */
Ring(
    /* radius from center of galaxy. */
    float64 radius_,
    /* total mass of the ring. */
    float64 mass_,
    /* number of slices. */
    int slices_
) {
}
alias Rings = Vector<Ring>;

Galaxy(
    int nrings_,
    Rings rings_,

    /* track the range of numbers we're summing. */
    float64 in_min_,
    float64 in_max_,
    float64 spin_min_,
    float64 spin_max_,
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
    }

    void acceleration() {
        /*
        track the magnitudes of the numbers we're summing.
        check if we're adding "too small" numbers to "too large" numbers.
        */
        in_min_ = +1e300;
        in_max_ = 0.0;
        spin_min_ = +1e300;
        spin_max_ = 0.0;

        /*for (i : 0..nrings_) {
            println(String + "Calculating total acceleration on ring " + i + ".");
            for (k : 0..nrings_) {
                acceleration(i, k);
            }
        }*/
        acceleration(nrings_*2/3, nrings_/3);

        in_ratio = in_max_ / in_min_;
        spin_ratio = spin_max_ / spin_min_;
        dump(#in_ratio);
        dump(#spin_ratio);
    }

    void acceleration(int on_idx, int by_idx) {
        println(String + "Calculating acceleration on ring " + on_idx + " caused by ring " + by_idx + ".");

        /* the rings. */
        on_ring = ^rings_[on_idx];
        by_ring = ^rings_[by_idx];

        /* the mass of each slice. */
        by_mass = by_ring^.mass_ / by_ring^.slices_;

        /* radius of each ring. */
        on_r = on_ring^.radius_;
        by_r = by_ring^.radius_;

        /* two components of acceleration. */
        float64 inward = 0.0;
        float64 spinward = 0.0;

        /* centripetal acceleration is outward - negative. */
        in = kAngularVelocity2 * on_r;
        update_in_min_max(in);
        inward -= in;

        /* gravity of central bulge. */
        r2 = on_r * on_r;
        in = kG * kCentralBulgeMass / r2;
        update_in_min_max(in);
        inward += in;

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
            update_in_min_max(in);
            spin = a * dy / d;
            update_spin_min_max(in);
            inward += in;
            spinward += spin;
        }

        dump(#inward);
        dump(#spinward);
    }

    void update_in_min_max(float64 in) {
        if (in < 0.0) {
            in = - in;
        }
        if (in_max_ < in) {
            in_max_ = in;
        }
        if (in_min_ > in) {
            in_min_ = in;
        }
    }

    void update_spin_min_max(float64 spin) {
        if (spin < 0.0) {
            spin = - spin;
        }
        if (spin_max_ < spin) {
            spin_max_ = spin;
        }
        if (spin_min_ > spin) {
            spin_min_ = spin;
        }
    }
}

int32 main() {
    println("Hello, World!");
    Galaxy galaxy;
    galaxy.run();
    println("Goodbye, World!");
    return 0;
}
