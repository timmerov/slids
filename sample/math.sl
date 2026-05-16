/*
interop with c libraries
*/

const float64 kPi = 3.14;

float64 cos(float64 angle) = import;
float64 sin(float64 angle) = import;
float64 tan(float64 angle) = import;
float64 acos(float64 x) = import;
float64 asin(float64 x) = import;
float64 atan(float64 x) = import;
float64 atan2(float64 y, float64 x) = import;

int32 main() {

    angle = kPi/2.0/3.0;
    x = cos(angle);
    y = sin(angle);
    __println("x = " + x);
    __println("y = " + y);

    return 0;
}
