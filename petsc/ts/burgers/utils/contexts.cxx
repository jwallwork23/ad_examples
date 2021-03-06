#include <petscts.h>
#include "../../../utils/drivers.cxx"


/* (Passive) field for two PDEs */
#ifndef FIELD
#define FIELD
typedef struct {
  PetscScalar u,v;
} Field;
#endif

/* Active field for two PDEs */
#ifndef AFIELD
#define AFIELD
typedef struct {
  adouble u,v;
} AField;
#endif

/* Application context */
#ifndef APPCTX
#define APPCTX
typedef struct {
  PetscReal D,kappa;
  PetscBool no_an,aijpc;
  AField    **u_a,**f_a,**udot_a;
  AdolcCtx  *adctx;
  PetscInt  m,n;                // Dependent/indpendent variables (#local nodes, inc. ghost points)
} AppCtx;
#endif
