// `S a[4];` - the default constructor once per element.
//
// **This was the one construction that silently did not happen.** The branch
// that builds a class local asks whether the type is a struct, and an array of
// S is an array - so this fell through to an ordinary uninitialised local: it
// compiled, it linked, it ran, and every element held whatever was on the
// stack. A member array was always fine, because the implicit constructor that
// builds one goes through the memberwise path, and `new T[n]` is refused by
// name; only the local declaration had nothing at all, which is why nothing
// caught it.
//
// Every array level is unwrapped at once, so `T g[2][3]` is six elements of T
// rather than two of `T[3]`: the loop steps by the element's own size and the
// intermediate shape has nothing to say about it.
extern "C" { int printf(const char *, ...); }

struct S {
    int x;
    S();
};

struct T {
    int y;
    T();
};

S::S() { x = 3; }
T::T() { y = 7; }

struct Holder { S a[2]; };      // the member form, which always worked

int main(void) {
    S one;
    S a[4];
    T g[2][3];
    Holder h;
    printf("%d | %d %d %d %d | %d %d | %d %d\n",
           one.x, a[0].x, a[1].x, a[2].x, a[3].x,
           g[0][0].y, g[1][2].y, h.a[0].x, h.a[1].x);
    return 0;
}
