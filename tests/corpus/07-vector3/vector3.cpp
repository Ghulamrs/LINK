/* vector3.cpp - the arithmetic vector3.h promised. */
#include "vector3.h"

#include <cmath>

double dot(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/* Right-handed, which is the convention every graphics book and every physics
   book agrees on and is still worth writing down: x cross y is z. */
Vector3 cross(const Vector3& a, const Vector3& b) {
    return Vector3(a.y * b.z - a.z * b.y,
                   a.z * b.x - a.x * b.z,
                   a.x * b.y - a.y * b.x);
}

double length(const Vector3& v) {
    return std::sqrt(dot(v, v));
}
