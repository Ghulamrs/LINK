// [stmt.jump]/2: a jump out of a scope destroys the objects the scope built,
// innermost first, on the way out. The neighbours of goto-out-of-scope, each
// run and diffed against clang: a goto out of two scopes at once, out of a
// loop body, and backwards out of a block; break out of a loop, out of a
// switch, and out of an inner loop only; continue; several objects in one
// scope; an object whose only destructor is a member's; and a for-init
// object that a break must leave alone.
extern "C" int printf(const char *, ...);

struct Probe {
    Probe(const char *n);
    ~Probe();
    const char *name;
};
Probe::Probe(const char *n) { name = n; printf("+%s ", n); }
Probe::~Probe() { printf("-%s ", name); }

// A class with no constructor and no destructor of its own: the implicit
// ones build and destroy `inner`, and it is the implicit destructor a jump
// out of the scope has to call.
struct Member { Member(); ~Member(); };
Member::Member() { printf("+m "); }
Member::~Member() { printf("-m "); }
struct Holder { Member inner; };

void twoScopes(int n) {
    {
        Probe a("a");
        {
            Probe b("b");
            Probe c("c");
            if (n) goto out;
            printf("fell ");
        }
        printf("mid ");
    }
out:
    printf("| ");
}

void outOfLoop(int n) {
    for (int i = 0; i < 3; i++) {
        Probe p("p");
        if (i == n) goto out;
        printf("%d ", i);
    }
out:
    printf("| ");
}

void backwards(int n) {
    int tries = 0;
again:
    tries++;
    {
        Probe q("q");
        if (tries < n) goto again;
    }
    printf("%d | ", tries);
}

void breaks(int n) {
    for (Probe once("once"); ; ) {      // built once; a break leaves it alone
        Probe body("body");
        Holder h;
        if (n) break;
        printf("no ");
        break;
    }
    printf("| ");
}

void continues() {
    for (int i = 0; i < 3; i++) {
        Probe p("p");
        if (i == 1) continue;
        printf("%d ", i);
    }
    printf("| ");
}

void innerOnly() {
    for (int i = 0; i < 2; i++) {
        Probe outer("o");
        for (int j = 0; j < 3; j++) {
            Probe inner("i");
            if (j == 1) break;
        }
        printf("%d ", i);
    }
    printf("| ");
}

void chooser(int n) {
    switch (n) {
    case 0: {
        Probe s("s");
        Probe t("t");
        if (n == 0) break;
        printf("no ");
    }
    default:
        printf("d ");
    }
    printf("| ");
}

void whileLoop() {
    int i = 0;
    while (i < 5) {
        Probe w("w");
        i++;
        if (i == 2) continue;
        if (i == 4) break;
        printf("%d ", i);
    }
    printf("| ");
}

int main() {
    twoScopes(1); printf("\n");
    twoScopes(0); printf("\n");
    outOfLoop(1); printf("\n");
    backwards(3); printf("\n");
    breaks(1); printf("\n");
    breaks(0); printf("\n");
    continues(); printf("\n");
    innerOnly(); printf("\n");
    chooser(0); printf("\n");
    chooser(5); printf("\n");
    whileLoop(); printf("\n");
    return 0;
}
