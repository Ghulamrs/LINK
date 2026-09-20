// The object ledger, and why the suites needed one.
//
// Every suite here compares what a program *prints*. That cannot see a leak,
// a double destroy, a destructor run on a slot nothing constructed, or a use
// after free past the last printf - none of them change a printed value.
// Compiler++ matched a clang build on all 200 comparisons under a compiler
// that leaks every data member of every class with a written destructor, and
// it matched *because* a leak does not reach stdout.
//
// The invariant here is a language one and not a heap one: [basic.life] - an
// object is constructed exactly once and destroyed exactly once. A sanitizer
// cannot see a missing member destructor either, nothing having been
// allocated; only the program's own bookkeeping can.
//
// Usage: lfWatch() as the first statement of main, lfBuilt(this, "C") at the
// end of every constructor of C, lfGone(this, "C") at the start of every
// destructor. The report is printed at exit rather than inline, because a
// destructor that runs on the way out of main - including a wrong one - runs
// after the last statement of main and would otherwise be missed.
//
// Both compilers run the same ledger, so this stays differential - and the
// columns say different things. **live, double, phantom, over and lost must
// be zero for any conforming implementation**, whatever it chose to elide:
// each is an object leaked, destroyed twice, destroyed unbuilt, built over a
// live one, or past the table. built and gone are the weaker pair - C++11
// permits elision, so a case whose program leaves elision any freedom must
// not have its built count read as a fact. Write such a case so that it does
// not, exactly as return-copy-balance.cpp already does.
#ifndef CXX1_TESTS_LIFETIME_H
#define CXX1_TESTS_LIFETIME_H

extern "C" int printf(const char *, ...);
extern "C" int atexit(void (*)());

// **An address does not identify an object.** A member at offset 0 shares the
// address of the class holding it, and a base subobject shares it too - so a
// slot is keyed by address *and* class name, or building `D` and its first
// member looks like building one object twice. Found by the ledger reporting
// over=1 on a correct program the first time it ran.
// An enum and not a `static const int`: clang folds every reader of the
// latter and emits no symbol, cxx1 emits one, and the names suite reports a
// difference about emission wearing the shape of a mangling bug. An
// enumerator is not an object and neither compiler emits anything for it.
enum { lfCap = 512 };
static const void *lfAddr[lfCap];
static const char *lfName[lfCap];
static int lfState[lfCap];          // 0 never used, 1 live, 2 destroyed
static int lfBuiltCount = 0;
static int lfGoneCount = 0;
static int lfDoubleCount = 0;
static int lfPhantomCount = 0;
static int lfOverCount = 0;
static int lfLostCount = 0;

static bool lfSame(const char *a, const char *b) {
    while (*a != 0 && *a == *b) { a++; b++; }
    return *a == *b;
}

static int lfFind(const void *p, const char *n) {
    for (int i = 0; i < lfCap; i++)
        if (lfAddr[i] == p && lfSame(lfName[i], n)) return i;
    return -1;
}

static void lfBuilt(const void *p, const char *n) {
    lfBuiltCount++;
    int i = lfFind(p, n);
    if (i >= 0) {
        // A frame slot is reused, so building over a *dead* one is ordinary.
        // Building over a live one is an object constructed twice.
        if (lfState[i] == 1) lfOverCount++;
        lfState[i] = 1;
        return;
    }
    for (int j = 0; j < lfCap; j++)
        if (lfAddr[j] == 0) {
            lfAddr[j] = p; lfName[j] = n; lfState[j] = 1; return;
        }
    lfLostCount++;
}

static void lfGone(const void *p, const char *n) {
    int i = lfFind(p, n);
    // Never built: a destructor on a slot nothing constructed. [class.dtor]/11
    if (i < 0) { lfPhantomCount++; return; }
    if (lfState[i] == 2) { lfDoubleCount++; return; }
    lfState[i] = 2;
    lfGoneCount++;
}

static int lfLive() {
    int n = 0;
    for (int i = 0; i < lfCap; i++)
        if (lfState[i] == 1) n++;
    return n;
}

// One line. For a correct program built and gone are equal and every other
// number is zero, whatever the implementation chose to elide.
static void lfReport() {
    printf("LEDGER built=%d gone=%d live=%d double=%d phantom=%d over=%d lost=%d\n",
           lfBuiltCount, lfGoneCount, lfLive(),
           lfDoubleCount, lfPhantomCount, lfOverCount, lfLostCount);
}

// Reported at exit, not inline: main's own locals are destroyed after its
// last statement and before atexit runs, so a destructor that should not have
// happened is inside the window and an inline report would print before it.
static void lfWatch() { atexit(lfReport); }

#endif
