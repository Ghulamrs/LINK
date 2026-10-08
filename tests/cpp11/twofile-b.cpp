// The second half of the two-file COMDAT program - see twofile-a.cpp.
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

int fromB(int x) {
    Counter<int>::hits++;
    Square q(x);
    const Shape &s = q;
    int r = twice(s.area());
    try {
        throw Oops(r);
    } catch (const Oops &o) {
        printf("b: caught %d\n", o.code);
    }
    return r;
}
