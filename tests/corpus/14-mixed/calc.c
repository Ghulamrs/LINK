/* calc.c - a recursive-descent evaluator in C89: the C half of a mixed program. */
#include "calc.h"
#include <ctype.h>
#include <string.h>

static const char *at;
static int failed;

static long expr(void);

static void skip(void) { while (*at == ' ' || *at == '\t') at++; }

static long factor(void)
{
    long v = 0;
    skip();
    if (*at == '(') {
        at++;
        v = expr();
        skip();
        if (*at == ')') at++; else failed = 1;
        return v;
    }
    if (*at == '-') { at++; return -factor(); }
    if (!isdigit((unsigned char)*at)) { failed = 1; return 0; }
    while (isdigit((unsigned char)*at)) v = v * 10 + (*at++ - '0');
    return v;
}

static long term(void)
{
    long v = factor();
    for (;;) {
        skip();
        if (*at == '*') { at++; v *= factor(); }
        else if (*at == '/') {
            long d;
            at++;
            d = factor();
            if (d == 0) { failed = 1; return 0; }
            v /= d;
        }
        else return v;
    }
}

static long expr(void)
{
    long v = term();
    for (;;) {
        skip();
        if (*at == '+') { at++; v += term(); }
        else if (*at == '-') { at++; v -= term(); }
        else return v;
    }
}

long calc_eval(const char *text, int *error)
{
    long v;
    at = text;
    failed = 0;
    v = expr();
    skip();
    if (*at != '\0') failed = 1;
    *error = failed;
    return failed ? 0 : v;
}

const char *calc_version(void) { return "calc 1.0 (C89)"; }
