// A small program for the Makefile beside it: two translation units, a class
// hierarchy with virtual functions, and enough of the library to be worth
// compiling - <iostream>, <map>, <string> and <vector>.
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "report.h"
#include "shapes.h"

int main() {
    std::cout << title("shapes") << "\n";

    Circle c(2.0);
    Rect r(3.0, 4.0);
    std::vector<Shape *> shapes;
    shapes.push_back(&c);
    shapes.push_back(&r);

    // **Sequenced by hand**: the order in which `<<`'s operands are evaluated
    // is unspecified, and reportAreas prints as it goes - so calling it inside
    // the same expression would let its lines land before "total " on one
    // compiler and after it on another. Both would be right.
    const double total = reportAreas(shapes);
    std::cout << "total " << total << "\n";

    std::map<std::string, int> counted;
    counted["circle"] = 1;
    counted["rect"] = 1;
    counted["circle"] = counted["circle"] + 1;
    for (std::map<std::string, int>::iterator it = counted.begin();
         it != counted.end(); ++it)
        std::cout << it->first << " x" << it->second << "\n";
    return 0;
}
