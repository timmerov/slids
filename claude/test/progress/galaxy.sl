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
say 100 lh.
there will be 10,000 rings.
the ring will be approximated as point masses spaced
approximate the characteristic distance around the ring.
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
const float64 kScaleLY = 3000.0; // 100.0;
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
    Rings rings_
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
        float64 sum = 0.0;
        factor = (kGalaxyRadius - kCentralBulgeRadius) / (nrings_ - 1);
        total_slices = 0;
        for (int i : 0..<nrings_) {
            radius = factor * i + kCentralBulgeRadius;
            sum += radius;
            //dump(#radius);

            /* divide the ring into equal width slices. */
            divs = radius / kScale;
            divs = math:round(divs);
            slices = (int=divs);
            total_slices += slices;
            //dump(#divs);

            ring = ^rings_[i];
            ring^.radius_ = radius;
            ring^.slices_ = slices;
        }
        //dump(#sum);
        dump(#total_slices);

        /* distribute the mass. */
        factor = kRingsMass / sum;
        for (int i : 0..<nrings_) {
            ring = ^rings_[i];
            ring^.mass_ = factor * ring^.radius_;
            //dump(#ring^.mass_);
        }
    }

    void acceleration() {
        /*for (i : 0..nrings_) {
            println(String + "Calculating total acceleration on ring " + i + ".");
            for (k : 0..nrings_) {
                acceleration(i, k);
            }
        }*/
        acceleration(nrings_*2/3, nrings_/3);
    }

    void acceleration(int on_idx, int by_idx) {
        println(String + "Calculating acceleration on ring " + on_idx + " caused by ring " + by_idx + ".");

        /* the rings. */
        on_ring = ^rings_[on_idx];
        by_ring = ^rings_[by_idx];

        /* the mass of each slice. */
        on_mass = on_ring^.mass_ / on_ring^.slices_;
        by_mass = by_ring^.mass_ / by_ring^.slices_;

        // ==tsc==
        on_mass;

        /* radius of each ring. */
        on_r = on_ring^.radius_;
        by_r = by_ring^.radius_;

        /* two components of acceleration. */
        float64 inward = 0.0;
        float64 spinward = 0.0;

        /* min/max */
        float64 in_max = -1e300;
        float64 in_min = +1e300;
        float64 spin_max = -1e300;
        float64 spin_min = +1e300;

        /* centripetal acceleration is outward - negative. */
        in = kAngularVelocity2 * on_r;
        if (in_max < in) {
            in_max = in;
        }
        if (in_min > in) {
            in_min = in;
        }
        inward -= in;

        /* gravity of central bulge. */
        r2 = on_r * on_r;
        in = kG * kCentralBulgeMass / r2;
        if (in_max < in) {
            in_max = in;
        }
        if (in_min > in) {
            in_min = in;
        }
        inward += in;

        /*
        contribution from each slice.
        skip the contribution from the slice on itself.
        */
        int nslices = by_ring^.slices_;
        int first = 0;
        if (on_idx == by_idx) {
            first = 1;
        }
        factor = 2.0 * math:kPi64 / nslices;
        for (int slice : first..nslices) {
            angle = factor * slice;
            x = by_r * math:cos(angle);
            y = by_r * math:sin(angle);
            dx = x - on_r;
            dy = y;
            d2 = dx*dx + dy*dy;
            a = kG * by_mass / d2;
            d = math:sqrt(d2);
            in = a * dx / d;
            if (in_max < in) {
                in_max = in;
            }
            if (in_min > in) {
                in_min = in;
            }
            spin = a * dy / d;
            if (spin_max < in) {
                spin_max = in;
            }
            if (spin_min > in) {
                spin_min = in;
            }
            inward += in;
            spinward += spin;
        }

        dump(#inward);
        dump(#spinward);
        dump(#in_min);
        dump(#in_max);
        dump(#spin_min);
        dump(#spin_max);
    }
}

int32 main() {
    println("Hello, World!");
    Galaxy galaxy;
    galaxy.run();
    println("Goodbye, World!");
    return 0;
}
