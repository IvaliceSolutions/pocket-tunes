/* freestanding shim: fixed-point libopus includes <math.h> but never calls
 * libm at runtime — any real use would fail at link, loudly. */
#ifndef SHIM_MATH_H
#define SHIM_MATH_H
double floor(double); double ceil(double); double sqrt(double);
double pow(double, double); double exp(double); double log(double);
double log10(double); double sin(double); double cos(double); double atan(double);
float floorf(float); float ceilf(float); float sqrtf(float); float fabsf(float);
double fabs(double); double fmod(double, double);
#endif
