#include <wmath.h>
#include <stdlib.h>

double dabs(double d) {
    return d > 0 ? d : -d;
}

int i32min(int a, int b) {
    return a < b ? a : b;
}

int i32max(int a, int b) {
    return a < b ? b : a;
}

long i64min(long a, long b) {
    return a < b ? a : b;
}

long i64max(long a, long b) {
    return a < b ? b : a;
}

double frand() {
    return rand() / (double)RAND_MAX;
}
