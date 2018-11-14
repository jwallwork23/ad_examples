#include <stdbool.h>
#include <string.h>
#include <stdio.h>

$input int m, n, p;
$assume(m<SIZELIMIT && n<SIZELIMIT && p < SIZELIMIT && m>0 && n>0 && p>0);
$input bool transa, transb;
$input double alpha, beta;
$input double A[m][p], B[p][n], Cin[m][n];
$output double Cout[m][n];
$input double Ad[m][p], Bd[p][n], Cdin[m][n];
$output double Cdout[m][n];

#include "../derivatives/byhand.c"

void main() {
    double C[m][n], Cd[m][n];
    memcpy(&C[0][0], &Cin[0][0], m*n*sizeof(double));
    memcpy(&Cd[0][0], &Cdin[0][0], m*n*sizeof(double));
    mxm_dot(m, p, n, A, Ad, B, Bd, C, Cd);
    memcpy(&Cout[0][0], &C[0][0], m*n*sizeof(double));
    memcpy(&Cdout[0][0], &Cd[0][0], m*n*sizeof(double));
}
