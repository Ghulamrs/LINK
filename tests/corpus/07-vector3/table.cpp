/* table.cpp - the lookups table.h declared.
 *
 * Linear, over at most twelve entries. Anything cleverer would be slower to
 * read and no faster to run at this size, and this file exists to be read. */
#include "table.h"

bool Table::add(const std::string& name, double value) {
    if (held_ >= kMost) return false;

    double already = 0;
    if (find(name, already)) return false;   // one entry per name

    names_[held_] = name;
    values_[held_] = value;
    ++held_;
    return true;
}

bool Table::find(const std::string& name, double& into) const {
    for (int i = 0; i < held_; ++i) {
        if (names_[i] != name) continue;
        into = values_[i];
        return true;
    }
    return false;
}
