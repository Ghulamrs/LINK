// Dynamic initialisation under the ledger: every object with static storage
// duration that a constructor builds is built once, before main, and
// destroyed once, at exit - [basic.start.init]/2 and [basic.start.term].
//
// The report has to outlive the static objects, and lifetime.h says to call
// lfWatch() first thing in main - which here would be too late: a destructor
// registered before main runs *after* an atexit handler registered in main,
// the two lists being one list read backwards. So the watcher is the first
// object in the file, and its constructor registers the report before any
// other constructor here has run. That is [basic.start.term]/3 doing exactly
// what it says, and clang prints the same line.
//
// No shape here leaves elision any freedom - there is no `S x = S(n)` and no
// return by value - so built and gone are read as facts. Four kinds of object:
// at namespace scope, inside a namespace, internal linkage, and a static data
// member. The one with a constructor and no destructor is here because it is
// dynamic initialisation all the same, and was refused all the same.
#include "lifetime.h"

struct Watcher { Watcher(); };
Watcher::Watcher() { lfWatch(); }
static Watcher watcher;

struct S {
    int v;
    S(int a);
    ~S();
};
S::S(int a) : v(a) { lfBuilt(this, "S"); printf("S%d\n", v); }
S::~S() { lfGone(this, "S"); printf("~S%d\n", v); }

struct Plain { int v; Plain(int a); };
Plain::Plain(int a) : v(a * 10) {}

S first(1);
namespace N { S inner(2); namespace M { S deeper(3); } }
static S internal(4);
const S constant(5);
Plain plain(6);
struct Holder { static S member; static Plain flat; };
S Holder::member(7);
Plain Holder::flat(8);

int main() {
    printf("main %d %d %d %d %d %d %d %d\n", first.v, N::inner.v, N::M::deeper.v,
           internal.v, constant.v, plain.v, Holder::member.v, Holder::flat.v);
    first.v = 11;
    N::M::deeper.v = 33;
    Holder::member.v = 77;
    return 0;
}
