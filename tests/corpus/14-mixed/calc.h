/* calc.h - a C89 evaluator of integer expressions, called from C++ in main.cpp. */
#ifndef CALC_H
#define CALC_H
#ifdef __cplusplus
extern "C" {
#endif
/* evaluates "+ - * / ( )" over decimal integers; returns 0 and sets *error on a fault */
long calc_eval(const char *text, int *error);
const char *calc_version(void);
#ifdef __cplusplus
}
#endif
#endif
