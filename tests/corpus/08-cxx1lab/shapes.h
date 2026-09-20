// A base with two overrides - the vtable this example is really about.
//
// **Every member is declared here and defined in shapes.cpp**, and that is not
// a style choice: this compiler has no COMDAT or weak linkage on
// x86_64-windows, so a member function *defined inside its class* becomes an
// ordinary strong symbol in every translation unit that includes the header,
// and two of them will not link. README.md says so under "Known shortcomings",
// and this example is what says it in code. Defining them once, in one .cpp,
// is portable and is what a C++ project of any size does anyway.
#ifndef DEMO_SHAPES_H
#define DEMO_SHAPES_H

#include <string>

struct Shape {
    virtual ~Shape();
    virtual double area() const = 0;
    virtual std::string name() const = 0;
};

struct Circle : Shape {
    double r;
    Circle(double radius);
    ~Circle();
    double area() const;
    std::string name() const;
};

struct Rect : Shape {
    double w, h;
    Rect(double width, double height);
    ~Rect();
    double area() const;
    std::string name() const;
};

#endif
