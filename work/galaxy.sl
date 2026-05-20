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
const float64 kMassGalaxy = 3e42;

/* diameter and radius of milky way. */
const float64 kGalaxyDiameterLY = 100_000.0;
const float64 kGalaxyDiameter = kGalaxyDiameterLY * kMetersPerLightyear;
const float64 kGalaxyRadius = kGalaxyDiameter / 2.0;

/* characteristic distance aka resolution. */
const float64 kScaleLY = 100.0;
const float64 kScale = kScaleLY * kMetersPerLightyear;

/* estimated mass central bulge: 1.5e40 to 4e40 kg */
const float64 kCentralBulgeMass = 2e40;

/* estimated radius of central bulge: 3000 to 6500 ly. */
const float64 kCentralBulgeRadiusLY = 4_000.0;
const float64 kCentralBulgeRadius = kCentralBulgeRadiusLY * kMetersPerLightyear;

/* estimated rotation period of the galaxy: 225 to 250 million years. */
const float64 kRotationPeriodYr = 237_000_000;
const float64 kRotationPeriod = kRotationPeriodYr * kSecondsPerYear;

/* separate the mass of the galaxy into rings. */
Ring(
    float64 radius_,
    float64 mass_
) {
}
alias Rings = Vector<Ring>;

Galaxy(
    Rings rings_
) {
    void run() {
        init();
    }

    void init() {
        nrings = (kGalaxyRadius - kCentralBulgeRadius) / kScale;
        dump(#nrings);
        nrings = math:round(nrings);
        size = (int=nrings);
        dump(#size);
    }
}

int32 main() {
    println("Hello, World!");
    Galaxy galaxy;
    galaxy.run();
    println("Goodbye, World!");
    return 0;
}
