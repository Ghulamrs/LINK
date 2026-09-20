/* counter.h - a count that can be stepped and read back.
 *
 * The smallest thing worth splitting into a header and a source: a type you
 * are given, and three things you may do to it. Nothing here says how it is
 * stored, which is the point of the split - counter.c may change its mind
 * about that without a single caller being rebuilt for a different reason.
 *
 * Paired with counter.c. Open both and F4 builds them together, if the
 * project's "build" names the group they are in. */
#ifndef COUNTER_H
#define COUNTER_H

typedef struct {
    long count;
    long step;
} Counter;

/* Starts at zero, moving by `step` each time it is advanced. A step of zero
   is allowed and stands still; it is a count, not a promise of progress. */
void counter_start(Counter* it, long step);

void counter_advance(Counter* it);
long counter_read(const Counter* it);

#endif
