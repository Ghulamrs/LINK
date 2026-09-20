/* counter.c - and what counter.h left unsaid.
 *
 * Ctrl-B compiles this file on its own; F4 builds the project's program from
 * every source in the groups its "build" entry names. The two are separate
 * commands on purpose - one is the file in front of you, the other is the
 * program, and neither has to guess which you meant. */
#include "counter.h"

void counter_start(Counter* it, long step) {
    it->count = 0;
    it->step = step;
}

void counter_advance(Counter* it) {
    it->count += it->step;
}

long counter_read(const Counter* it) {
    return it->count;
}
