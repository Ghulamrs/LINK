// [expr.prim.lambda]/21: a by-copy capture is a member of the closure,
// initialised from the entity it names - which for a class means its copy
// constructor and for an array means copying the array. cxx1 did neither.
//
// A class was copied bytewise: no copy constructor ran, and because the
// closure type was completed without the special members every other class
// gets, nothing ever destroyed the member either. The two cancelled in the
// output - one construction, one destruction, the right number printed - so
// only an object ledger could see it, which is why lifetime.h came first.
//
// An array was worse and needed no ledger: `decay` handed the member a
// *pointer* to the original where the member *is* the array, so the closure
// read whatever the frame held. `[arr]` and `[=]` both returned garbage from
// three lines of ordinary C++.
//
// The array copy is element by element, flattened to the innermost element so
// a two-dimensional array is six copies and not two rows, because an array is
// not assignable - the same reason the copy constructor copies an array member
// with a loop rather than one store. Every mutation below happens *after* the
// lambdas are made: a copy that still sees it is a pointer wearing a copy's
// type.
#include "lifetime.h"

struct Held {
    int v;
    Held(int n);
    Held(const Held &o);
    ~Held();
};
Held::Held(int n) : v(n) { lfBuilt(this, "Held"); }
Held::Held(const Held &o) : v(o.v) { lfBuilt(this, "Held"); }
Held::~Held() { lfGone(this, "Held"); }

int main() {
    lfWatch();

    int arr[3] = {1, 2, 3};
    int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
    int plain = 7;

    auto byName   = [arr]()   { return arr[0] + arr[2]; };
    auto byAll    = [=]()     { return arr[1] + grid[1][2] + plain; };
    auto multiDim = [grid]()  { return grid[0][0] + grid[1][1] + grid[1][2]; };

    arr[0] = 99; grid[1][2] = 99; plain = 99;
    printf("%d %d %d\n", byName(), byAll(), multiDim());

    // The class half: the copy constructor runs where the closure is made,
    // and the closure's own destructor takes the member with it.
    {
        Held h(5);
        auto holds = [h]() { return h.v; };
        printf("%d\n", holds());
    }

    // By reference, which was always right and is here to stay that way: the
    // closure holds an address, so it does see the mutation.
    int watched = 1;
    auto byRef = [&watched]() { return watched; };
    watched = 41;
    printf("%d\n", byRef());
    return 0;
}
