// The three special members the compiler writes when the class does not: the
// default constructor, the copy constructor and the copy assignment.
//
// **A trivial one is not a function at all**, and that is measured rather than
// reasoned. cl emits no symbol for the default constructor, copy constructor
// or copy assignment of a class with no virtual function and no member that
// needs building, and clang emits none on either Itanium target either. So a
// class like Plain below is copied by moving its bytes - the struct assignment
// this compiler has emitted since it was a C compiler - and the implicit
// member is never declared. What makes one non-trivial is a virtual function,
// whose vptr somebody has to store, or a base or member with a special member
// of its own to run.
//
// The difference between the two non-trivial copies is the vptr, and it is
// visible here in `viaBase`: a copy *constructor* stores this class's own
// vtable pointer, because it is making a new object, and a copy *assignment*
// leaves the vptr alone, because it is writing into an object that is already
// of this class. Both measured in cl's own listing.
extern "C" { int printf(const char *, ...); }

class Counted {
public:
    Counted();
    Counted(const Counted &o);
    int n;
};
Counted::Counted() { n = 1; }
Counted::Counted(const Counted &o) { n = o.n + 100; }

class Shape {
public:
    virtual int id();
    int tag;
};
int Shape::id() { return 1; }

class Round : public Shape {
public:
    virtual int id();
    int r;
};
int Round::id() { return 2; }

// No constructor written anywhere in these two. Bag gets all three implicit
// members and every one of them is non-trivial: the Counted member has a copy
// constructor to run, and the array member is copied an element at a time.
class Bag {
public:
    Counted c;
    int k;
    int a[3];
};

class Plain {
public:
    int x;
    int y;
};

int main() {
    // Trivial: no constructor is declared, so none is called.
    Plain p;
    p.x = 1;
    p.y = 2;
    Plain q(p);
    Plain r = p;
    r.y = 20;
    printf("%d %d %d %d\n", q.x, q.y, r.x, r.y);

    // The implicit default constructor of a polymorphic class stores the vptr.
    Round s;
    s.tag = 7;
    s.r = 9;
    Shape *viaBase = &s;
    printf("%d %d %d\n", s.tag, s.r, viaBase->id());

    // The implicit copy constructor stores the *copy's* own vptr and moves the
    // members across, base's included.
    Round t(s);
    viaBase = &t;
    printf("%d %d %d\n", t.tag, t.r, viaBase->id());

    // The implicit copy assignment moves the members and leaves the vptr.
    Round u;
    u.tag = 0;
    u.r = 0;
    u = s;
    viaBase = &u;
    printf("%d %d %d\n", u.tag, u.r, viaBase->id());

    // A member with a copy constructor of its own gets it called; the array is
    // copied one element at a time.
    Bag b;
    b.k = 4;
    b.a[0] = 10;
    b.a[1] = 11;
    b.a[2] = 12;
    printf("%d %d\n", b.c.n, b.k);

    Bag d = b;
    printf("%d %d %d %d %d\n", d.c.n, d.k, d.a[0], d.a[1], d.a[2]);

    // And assignment reaches the same members without a copy constructor in
    // sight - Counted writes no operator=, so its own is implicit and trivial,
    // and Bag's copies it as bytes.
    Bag e;
    e = b;
    printf("%d %d %d\n", e.c.n, e.k, e.a[2]);

    // `a = b` is an expression and answers the object assigned to.
    Round v;
    printf("%d\n", (v = s).r);
    return 0;
}
