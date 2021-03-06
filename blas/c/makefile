all: clean derivatives/mxm_d.c derivatives/mxm_b.c test_tensor

# Compiling
CFLAGS = -O3 -Wall -I. -I${TAPENADE_HOME}/ADFirstAidKit
CFLAGS += -I${LAPACK_HOME}/CBLAS -I${LAPACK_HOME}/SRC -I${LAPACK_HOME}/LAPACKE/include

# Linking TODO: not if we are using CIVL
LIBRARIES = ${LAPACK_HOME}/liblapacke.a ${LAPACK_HOME}/liblapack.a ${LAPACK_HOME}/libcblas.a ${LAPACK_HOME}/librefblas.a

DDIR = derivatives
TFLAGS = -O $(DDIR)
clean: cleandiff cleanobj
cleandiff:
	rm -rf $(DDIR)/*naive_* $(DDIR)/*.msg $(DDIR)/*~ $(DDIR)/Global* $(DDIR)/RHS_*
cleanobj:
	rm -rf test_AB test_A test_tensor *.o *~

### DIFFERENTIATION OF MATRIX / TENSOR SUBROUTINES USING TAPENADE

derivatives/mxm_d.c: mxm.c
	tapenade -d -head "naive_mxm[AB](A B)\(C)                    \
                           naive_mxm[A](A)\(C)                       \
                           naive_mxm[B](B)\(C)                       \
                           naive_mpm[AB](A B)\(C)                    \
                           naive_mpm[A](A)\(C)                       \
                           naive_mpm[B](B)\(C)                       \
                           naive_dgemm[AB](A B)\(C)                  \
                           naive_dgemm[A](A)\(C)                     \
                           naive_dgemm[B](B)\(C)                     \
                           extra_naive_dgemm[scalar](alpha beta)\(C) \
                           naive_dgemm[scalar](alpha beta)\(C)       \
                           naive_mtmv(U)\(V)                         \
                           " mxm.c $(TFLAGS)

derivatives/mxm_b.c: mxm.c
	tapenade -b -head "naive_mxm[AB](A B)\(C)                    \
                           naive_mxm[A](A)\(C)                       \
                           naive_mxm[B](B)\(C)                       \
                           naive_mpm[AB](A B)\(C)                    \
                           naive_mpm[A](A)\(C)                       \
                           naive_mpm[B](B)\(C)                       \
                           naive_dgemm[AB](A B)\(C)                  \
                           naive_dgemm[A](A)\(C)                     \
                           naive_dgemm[B](B)\(C)                     \
                           extra_naive_dgemm[scalar](alpha beta)\(C) \
                           naive_dgemm[scalar](alpha beta)\(C)       \
                           naive_mtmv(U)\(V)                         \
                           " mxm.c $(TFLAGS)

### VERIFICATION

verify_mxm: derivatives/mxm_d.c derivatives/mxm_b.c
	civl compare -impl -DSIZELIMIT=5 verification_drivers/driver_mxm_dot.c -spec -DSIZELIMIT=5 verification_drivers/driver_mxm_dAB.c

verify_mpm: derivatives/mxm_d.c derivatives/mxm_b.c
	civl compare -impl -DSIZELIMIT=5 verification_drivers/driver_mpm_dot.c -spec -DSIZELIMIT=5 verification_drivers/driver_mpm_dAB.c

verify_dgemm: derivatives/mxm_d.c derivatives/mxm_b.c
	civl compare -impl -DSIZELIMIT=5 verification_drivers/driver_dgemm_dot.c -spec -DSIZELIMIT=5 verification_drivers/driver_dgemm_dAB.c
	civl compare -impl -DSIZELIMIT=5 verification_drivers/driver_dgemm_bar.c -spec -DSIZELIMIT=5 verification_drivers/driver_dgemm_bAB.c
	civl compare -impl -DSIZELIMIT=5 verification_drivers/driver_dgemm_scalar_dot.c -spec -DSIZELIMIT=5 verification_drivers/driver_extranaive_dgemm_scalar_dscalar.c
	#civl compare -impl -DSIZELIMIT=5 verification_drivers/driver_dgemm_scalar_dot.c -spec -DSIZELIMIT=5 verification_drivers/driver_dgemm_scalar_dscalar.c

### TAPENADE UTILS

adBuffer.o: ${TAPENADE_HOME}/ADFirstAidKit/adBuffer.c
	gcc $(CFLAGS) -c -o $@ $^

adStack.o: ${TAPENADE_HOME}/ADFirstAidKit/adStack.c
	gcc $(CFLAGS) -c -o $@ $^

### SIMPLE TESTS USING PSEUDO-RANDOM NUMBERS

test_A.o: test_A.c
	gcc $(CFLAGS) -c -o $@ $<

test_A: test_A.o adBuffer.o adStack.o $(LIBRARIES)
	if [ ! -d "results" ]; then mkdir results; fi
	gfortran -o $@ $^
	rm *.o
	./$@ >> results/experiment1.log

test_AB.o: test_AB.c
	gcc $(CFLAGS) -c -o $@ $<

test_AB: test_AB.o adBuffer.o adStack.o $(LIBRARIES)
	if [ ! -d "results" ]; then mkdir results; fi
	gfortran -o $@ $^
	rm *.o
	./$@ >> results/experiment2.log

test_tensor.o: test_tensor.c
	gcc $(CFLAGS) -c -o $@ $<

test_tensor: test_tensor.o adBuffer.o adStack.o $(LIBRARIES)
	if [ ! -d "results" ]; then mkdir results; fi
	gfortran -o $@ $^
	rm *.o
	./$@ >> results/experiment3.log
