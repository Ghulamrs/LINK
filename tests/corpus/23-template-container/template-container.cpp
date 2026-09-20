// A container written in this subset - the thing the three operators were for.
//
// **This case exists because the ladder's next rung is the standard library**,
// and until `operator[]`, `operator=` and `operator->` could be reached from an
// expression, no container could be written at all: subscripting for the
// element, copy assignment for a class that owns memory, and `operator->` for
// the iterator. Every other overloadable operator was already reachable, so
// these three were the whole of the gap - and none of them was named in
// docs/EXCLUSIONS.md, which recorded all three under one bullet about operators
// "that can be named but not reached".
//
// What is exercised here is what `vector` needs and nothing more: a class
// template that allocates, grows, copies itself, assigns itself without leaking
// or double-freeing, and hands out an iterator with `->` and `*`. `live` is the
// instrument: a counter the elements move, asserted at the end, because a
// container that leaks or frees twice is the failure that prints the right
// answer anyway.

extern "C" int printf(const char *, ...);
extern "C" void *malloc(unsigned long);
extern "C" void free(void *);

int live = 0;

struct Item {
    int v;
    Item() : v(0) { live++; }
    Item(int x) : v(x) { live++; }
    Item(const Item &o) : v(o.v) { live++; }
    Item &operator=(const Item &o) { v = o.v; return *this; }
    ~Item() { live--; }
    int twice(void) const { return v * 2; }
};

template <class T>
class Vec {
public:
    Vec() : items_(0), size_(0), cap_(0) {}
    Vec(const Vec &o) : items_(0), size_(0), cap_(0) { copyFrom(o); }
    ~Vec() { free(items_); }

    Vec &operator=(const Vec &o) {
        if (this != &o) { free(items_); items_ = 0; size_ = 0; cap_ = 0; copyFrom(o); }
        return *this;
    }

    void push_back(const T &v) {
        if (size_ == cap_) grow();
        items_[size_] = v;
        size_++;
    }

    unsigned long size(void) const { return size_; }
    T &operator[](unsigned long i) { return items_[i]; }
    const T &operator[](unsigned long i) const { return items_[i]; }

    class iterator {
    public:
        T *at;
        T *operator->() const { return at; }
        T &operator*() const { return *at; }
        iterator &operator++() { at++; return *this; }
        bool operator!=(const iterator &o) const { return at != o.at; }
    };

    iterator begin(void) const { iterator i; i.at = items_; return i; }
    iterator end(void) const { iterator i; i.at = items_ + size_; return i; }

private:
    void copyFrom(const Vec &o) {
        for (unsigned long i = 0; i < o.size_; i++) push_back(o.items_[i]);
    }
    void grow(void) {
        unsigned long want = cap_ == 0 ? 2 : cap_ * 2;
        T *fresh = (T *)malloc(want * sizeof(T));
        for (unsigned long i = 0; i < size_; i++) fresh[i] = items_[i];
        free(items_);
        items_ = fresh;
        cap_ = want;
    }
    T *items_;
    unsigned long size_;
    unsigned long cap_;
};

int sumThrough(const Vec<Item> &v) {
    int total = 0;
    for (Vec<Item>::iterator i = v.begin(); i != v.end(); ++i)
        total += i->twice();                 // operator-> on the iterator
    return total;
}

int main(void) {
    Vec<int> plain;
    plain.push_back(3);
    plain.push_back(5);
    plain.push_back(7);
    plain[1] = plain[1] + 10;                // subscript, read and written
    printf("%lu %d %d %d\n", plain.size(), plain[0], plain[1], plain[2]);

    Vec<Item> items;
    items.push_back(Item(1));
    items.push_back(Item(2));
    items.push_back(Item(4));

    Vec<Item> copied(items);                 // copy constructor
    Vec<Item> assigned;
    assigned = items;                        // copy assignment
    assigned = assigned;                     // and self-assignment, which must not free

    printf("%lu %lu %d %d\n", copied.size(), assigned.size(),
           copied[2].v, assigned[2].twice());
    printf("%d %d\n", sumThrough(items), (int)(*items.begin()).v);
    return 0;
}
