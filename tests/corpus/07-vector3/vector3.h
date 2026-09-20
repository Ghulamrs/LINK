/* vector3.h - three numbers and the things worth doing to three numbers.
 *
 * C++ where counter.h is C, so a project holding both has two languages in it
 * and the editor compiles each group with the compiler its language asks for -
 * cc1 for the C, the machine's C++ compiler for this. The objects meet at the
 * linker, which does not care which compiler wrote them.
 *
 * Header-and-source rather than all-inline, because the point of the example
 * is the split. */
#ifndef VECTOR3_H
#define VECTOR3_H

struct Vector3 {
    double x, y, z;

    Vector3() : x(0), y(0), z(0) {}
    Vector3(double a, double b, double c) : x(a), y(b), z(c) {}
};

double dot(const Vector3& a, const Vector3& b);
Vector3 cross(const Vector3& a, const Vector3& b);
double length(const Vector3& v);

#endif
