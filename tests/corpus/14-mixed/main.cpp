// main.cpp - the C++ half of a mixed program: drives calc.c through calc.h, with
// a class, a std::string, an exception (an int: see ask) and a static object with a destructor,
// so that the link needs both compilers' output and the CRT's C++ startup.
#include "calc.h"
#include <cstdio>
#include <string>

namespace {

struct Tally {
    int asked, faulted;
    Tally() : asked(0), faulted(0) {}
    ~Tally() { std::printf("tally: %d asked, %d faulted\n", asked, faulted); }
};

Tally tally;

// The string lives here and not in main: cxx1i's x86_64-windows tables cannot yet hold a
// local with a destructor beside a try, and it names a type descriptor for a fundamental
// type only, so the fault is thrown as an int.
long ask(const char* line)
{
    std::string text(line);
    int error = 0;
    long v = calc_eval(text.c_str(), &error);
    tally.asked++;
    if (error) throw (int)text.size();
    return v;
}

}

int main()
{
    static const char* const lines[] = {
        "1 + 2 * 3", "(1 + 2) * 3", "100 / (5 - 5)", "2 * (3 + 4) - -5", "7 +", 0
    };
    std::printf("%s\n", calc_version());
    for (int i = 0; lines[i]; i++) {
        try {
            std::printf("%-20s = %ld\n", lines[i], ask(lines[i]));
        } catch (int n) {
            tally.faulted++;
            std::printf("%-20s : cannot evaluate (%d characters)\n", lines[i], n);
        }
    }
    return tally.faulted == 2 ? 0 : 1;
}
