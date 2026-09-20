#include "report.h"

#include <iostream>

double reportAreas(const std::vector<Shape *> &shapes) {
    double total = 0.0;
    for (std::size_t i = 0; i < shapes.size(); i++) {
        std::cout << shapes[i]->name() << " area " << shapes[i]->area() << "\n";
        total += shapes[i]->area();
    }
    return total;
}

std::string title(const std::string &what) { return "== " + what + " =="; }
