// The two-file COMDAT program: twofile-a.cpp and twofile-b.cpp both define, through the
// shared part below, an inline function, a class with an inline virtual (its vftable and
// RTTI), a class template's static data member and a thrown class - every one a COMDAT
// cpp11's MASM spelling writes and the linker must fold to one copy.
extern "C" int printf(const char *, ...);

inline int twice(int x) { return x * 2; }

struct Shape {
    virtual ~Shape() {}
    virtual int area() const { return 1; }
};
struct Square : Shape {
    int s;
    Square(int v) : s(v) {}
    int area() const { return s * s; }
};

template <class T> struct Counter {
    static int hits;
};
template <class T> int Counter<T>::hits = 0;

struct Oops {
    int code;
    Oops(int c) : code(c) {}
};

int fromB(int x);

int main() {
    Counter<int>::hits++;
    Square q(3);
    const Shape &s = q;
    printf("a: twice %d area %d\n", twice(4), s.area());
    int b = fromB(5);
    printf("a: fromB %d\n", b);
    printf("a: hits %d\n", Counter<int>::hits);
    try {
        throw Oops(7);
    } catch (const Oops &o) {
        printf("a: caught %d\n", o.code);
    }
    return 0;
}
