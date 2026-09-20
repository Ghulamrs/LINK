#ifndef DEMO_REPORT_H
#define DEMO_REPORT_H

#include <string>
#include <vector>

#include "shapes.h"

// A second translation unit, so the Makefile beside this has something to
// compile separately - and so `make one` has two files to put on two threads.
double reportAreas(const std::vector<Shape *> &shapes);
std::string title(const std::string &what);

#endif
