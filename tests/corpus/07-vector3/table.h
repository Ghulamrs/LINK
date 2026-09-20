/* table.h - a fixed table of names against numbers.
 *
 * The third header, and the one that shows why a header is not only a place
 * to put declarations: `Table::kMost` is the one number both this file and
 * whoever uses it have to agree about, and there is exactly one of it here.
 *
 * No allocation anywhere. This tree holds itself to C++14 and to what a small
 * machine can do, and a table of twelve is a table of twelve. */
#ifndef TABLE_H
#define TABLE_H

#include <string>

class Table {
public:
    /* An enumerator rather than a static const int, so that cxx1 reads it as
       the constant the array bounds below need; cl and clang++ take either. */
    enum { kMost = 12 };

    Table() : held_(0) {}

    /* Refuses quietly once it is full, and says so. A caller that ignores the
       answer gets a table that is still correct, just not any bigger. */
    bool add(const std::string& name, double value);

    int held() const { return held_; }
    bool find(const std::string& name, double& into) const;

private:
    std::string names_[kMost];
    double values_[kMost];
    int held_;
};

#endif
