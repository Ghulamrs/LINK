#include "shapes.h"

Shape::~Shape() {}

Circle::Circle(double radius) : r(radius) {}
Circle::~Circle() {}
double Circle::area() const { return 3.14159265358979 * r * r; }
std::string Circle::name() const { return "circle"; }

Rect::Rect(double width, double height) : w(width), h(height) {}
Rect::~Rect() {}
double Rect::area() const { return w * h; }
std::string Rect::name() const { return "rect"; }
