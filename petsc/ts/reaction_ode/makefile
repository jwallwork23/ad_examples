ALL: clean ex1
CFLAGS		=
FFLAGS		=
CXXFLAGS	= -std=c++11 -I${ADOLC_BUILDDIR}/include
CPPFLAGS	=
FPPFLAGS	=
CLEANFILES	= ex1 binaryoutput *.tap *.info *.txt *.o

LIB 		= ${PETSC_TS_LIB} -L${USER_LIB} -lboost_system
LIB             += -L${ADOLC_BUILDDIR}/lib64 -ladolc -Wl,-rpath,${ADOLC_BUILDDIR}/lib64

include ${PETSC_DIR}/lib/petsc/conf/variables
include ${PETSC_DIR}/lib/petsc/conf/rules

ex1: ex1.o
	-${CLINKER} -o $@ $^ $(LIB)
	${RM} $^

include ${PETSC_DIR}/lib/petsc/conf/test
