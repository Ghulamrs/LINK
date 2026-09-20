// The stream headers this compiler ships: <iostream>, <sstream>, <fstream>.
//
// **The oracle here is behavioural and not textual**, as it is for the other
// include cases: the suite hands this file to clang as well, and clang reads
// *its* <iostream>, not this one. So the two libraries are compared through
// what the same program prints - which is a far better question than reading
// the header and believing it.
//
// **`std::cout` is an object at file scope with no constructor**, and that is
// the whole trick these headers turn. A constructor there would have to run
// before `main`, which this compiler refuses because there is nowhere yet to
// put the code that would do it - so `ostream` is an aggregate, `cout` is a
// constant initialiser, and the `FILE *` is resolved at the point of use. An
// `ofstream` *does* have a constructor, and that is not an inconsistency: it
// is always a local, and a local with a constructor has always worked.
//
// A stringstream reuses every operator of its base rather than repeating them,
// through one pointer in `ostream` that says "write to this string instead".
// A virtual would have been the other way to do it, and a virtual is what
// `cout` cannot have - it would need a vptr, and a vptr needs a constructor.
//
// One compiler fault had to land before this case could be written: **a
// reference to a base would not bind to a derived object**, so
// `std::getline(istringstream, s)` found no matching function. The pointer
// form had always worked; the reference form was never done, and the fix
// ranks it as the derived-to-base conversion it is.
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <cstdio>

int main() {
    // Output, chained, with every built-in the header formats.
    std::string who("world");
    std::cout << "hello, " << who << "! " << 6 * 7 << " " << 2.5 << " "
              << 'c' << " " << true << std::endl;

    // A string built through a stream, and read back out of one.
    std::ostringstream o;
    o << "n=" << 42 << " d=" << 1.5;
    std::cout << o.str() << std::endl;

    std::istringstream in(std::string("7 hello 3.5"));
    int n = 0;
    std::string word;
    double d = 0;
    in >> n >> word >> d;
    std::cout << n << "|" << word << "|" << d << std::endl;

    // getline over a stringstream, which is the call that needed the
    // derived-to-base reference binding.
    std::istringstream lines(std::string("a\nbb\nccc"));
    std::string line;
    while (std::getline(lines, line)) std::cout << "[" << line << "]";
    std::cout << std::endl;

    // A file written and read back. **A relative name, not an absolute one**:
    // this case runs on three machines and `/tmp` is not a path on all of
    // them. A stream that fails to open writes nowhere - which is worth
    // stating, because falling through to `stdout` is what it used to do, and
    // that printed the file's contents to the console on the one box where
    // the path did not exist.
    {
        std::ofstream out("_cxx1_streams_case.txt");
        out << "first" << std::endl << 99 << std::endl;
    }
    {
        std::ifstream back("_cxx1_streams_case.txt");
        while (std::getline(back, line)) std::cout << "<" << line << ">";
        std::cout << std::endl;
    }
    // The suite runs from the tree's own directory, so a case that writes a
    // file and leaves it there puts one in `git status` after every run.
    // Closed first - the read stream above is scoped for that reason.
    std::remove("_cxx1_streams_case.txt");

    // A file that is not there fails to open rather than crashing.
    std::ifstream missing("_cxx1_no_such_file_here");
    std::cout << (missing.is_open() ? "opened" : "not open") << std::endl;
    return 0;
}
