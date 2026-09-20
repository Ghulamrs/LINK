// main.cpp - a driver for vector3.cpp and table.cpp, the two modules RIDE's examples
// leave without a main: written for the review so that they link as a program.
#include "vector3.h"
#include "table.h"
#include <cstdio>

int main()
{
    Vector3 x(1, 0, 0), y(0, 1, 0);
    Vector3 z = cross(x, y);
    Table t;
    t.add("dot", dot(x, y));
    t.add("z.z", z.z);
    t.add("len", length(Vector3(3, 4, 0)));
    double v = 0;
    const char* names[] = { "dot", "z.z", "len", "none" };
    for (int i = 0; i < 4; i++) {
        if (t.find(names[i], v)) std::printf("%s = %.3f\n", names[i], v);
        else std::printf("%s: not in the table\n", names[i]);
    }
    std::printf("%d held\n", t.held());
    return t.held() == 3 ? 0 : 1;
}
