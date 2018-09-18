

static char help[] = "Time-dependent PDE in 2d. Simplified from ex7.c for illustrating how to use TS on a structured domain. \n";
/*
   u_t = uxx + uyy
   0 < x < 1, 0 < y < 1;
   At t=0: u(x,y) = exp(c*r*r*r), if r=PetscSqrtReal((x-.5)*(x-.5) + (y-.5)*(y-.5)) < .125
           u(x,y) = 0.0           if r >= .125

    mpiexec -n 2 ./ex13 -da_grid_x 40 -da_grid_y 40 -ts_max_steps 2 -snes_monitor -ksp_monitor
    mpiexec -n 1 ./ex13 -snes_fd_color -ts_monitor_draw_solution
    mpiexec -n 2 ./ex13 -ts_type sundials -ts_monitor 
*/

#include <petscdm.h>
#include <petscdmda.h>
#include <petscts.h>
#include <adolc/adolc.h>	// Include ADOL-C
#include <adolc/adolc_sparse.h> // Include ADOL-C sparse drivers

#define tag 1

/*
   User-defined data structures and routines
*/
typedef struct {
  PetscReal c;
  PetscBool zos,zos_view,no_an,sparse,sparse_row,sparse_view;
  PetscInt  Mx,My;
  adouble   **u_a,**f_a;
} AppCtx;

extern PetscErrorCode RHSFunction(TS,PetscReal,Vec,Vec,void*);
extern PetscErrorCode RHSJacobianADOLC(TS,PetscReal,Vec,Mat,Mat,void*);
extern PetscErrorCode RHSJacobianByHand(TS,PetscReal,Vec,Mat,Mat,void*);
extern PetscErrorCode RHSLocalActive(DM da,PetscScalar **f,PetscScalar **uarray,void *ptr);
extern PetscErrorCode RHSLocalPassive(DM da,PetscScalar **f,PetscScalar **uarray,void *ptr);
extern PetscErrorCode FormInitialSolution(DM,Vec,void*);

extern PetscErrorCode AdoubleGiveGhostPoints2d(DM da,adouble *cgs,adouble **a2d[]);
extern PetscErrorCode AdoubleInsertGhostValues2d(DM da,PetscScalar **u,adouble **u_a);


int main(int argc,char **argv)
{
  TS             ts;                    /* nonlinear solver */
  Vec            u,r;                   /* solution, residual vector */
  Mat            J;                     /* Jacobian matrix */
  PetscInt       steps,gxs,gys,gxm,gym; /* iterations for convergence, ghost points */
  PetscErrorCode ierr;
  DM             da;
  adouble        **u_a = NULL,**f_a = NULL,*u_c = NULL,*f_c = NULL;
  PetscReal      ftime,dt;
  AppCtx         user;                  /* user-defined work context */
  PetscBool      byhand = PETSC_FALSE;

  ierr = PetscInitialize(&argc,&argv,(char*)0,help);if (ierr) return ierr;
  user.no_an = PETSC_FALSE;user.zos = PETSC_FALSE;user.zos_view = PETSC_FALSE;user.sparse = PETSC_FALSE;user.sparse_row = PETSC_FALSE;user.sparse_view = PETSC_FALSE;
  ierr = PetscOptionsGetBool(NULL,NULL,"-adolc_test_zos",&user.zos,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetBool(NULL,NULL,"-adolc_test_zos_view",&user.zos_view,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetBool(NULL,NULL,"-jacobian_by_hand",&byhand,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetBool(NULL,NULL,"-no_annotation",&user.no_an,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetBool(NULL,NULL,"-sparse",&user.sparse,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetBool(NULL,NULL,"-sparse_row",&user.sparse_row,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetBool(NULL,NULL,"-sparse_view",&user.sparse_view,NULL);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Create distributed array (DMDA) to manage parallel grid and vectors
  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = DMDACreate2d(PETSC_COMM_WORLD, DM_BOUNDARY_GHOSTED, DM_BOUNDARY_GHOSTED,DMDA_STENCIL_STAR,8,8,PETSC_DECIDE,PETSC_DECIDE,1,1,NULL,NULL,&da);CHKERRQ(ierr);
  ierr = DMSetFromOptions(da);CHKERRQ(ierr);
  ierr = DMSetUp(da);CHKERRQ(ierr);
  ierr = DMDAGetInfo(da,PETSC_IGNORE,&user.Mx,&user.My,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE);CHKERRQ(ierr);


  /*  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Extract global vectors from DMDA;
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = DMCreateGlobalVector(da,&u);CHKERRQ(ierr);
  ierr = VecDuplicate(u,&r);CHKERRQ(ierr);

  /* Initialize user application context */
  user.c = -30.0;

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Allocate memory for (local) active arrays and store references in the
     application context. The active arrays are reused at each active
     section, so need only be created once.

     NOTE: Memory for ADOL-C active variables cannot be allocated using
           PetscMalloc, as this does not call the relevant class
           constructor. Instead, we use the C++ keyword `new`.

           It is also important to deconstruct and free memory appropriately.
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (!user.no_an) {

    ierr = DMDAGetGhostCorners(da,&gxs,&gys,NULL,&gxm,&gym,NULL);CHKERRQ(ierr);

    // Create contiguous 1-arrays of AFields
    u_c = new adouble[gxm*gym];
    f_c = new adouble[gxm*gym];

    // Corresponding 2-arrays of AFields
    u_a = new adouble*[gym];
    f_a = new adouble*[gym];

    // Align indices between array types and endow ghost points
    ierr = AdoubleGiveGhostPoints2d(da,u_c,&u_a);CHKERRQ(ierr);
    ierr = AdoubleGiveGhostPoints2d(da,f_c,&f_a);CHKERRQ(ierr);

    // Store active variables in context
    user.u_a = u_a;
    user.f_a = f_a;
  }

  if (user.zos) {
    PetscPrintf(MPI_COMM_WORLD,"    If ||F_zos(x) - F_rhs(x)||_2/||F_rhs(x)||_2 is O(1.e-8), ADOL-C function evaluation\n      is probably correct.\n");
  }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Create timestepping solver context
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = TSCreate(PETSC_COMM_WORLD,&ts);CHKERRQ(ierr);
  ierr = TSSetDM(ts,da);CHKERRQ(ierr);
  ierr = TSSetType(ts,TSBEULER);CHKERRQ(ierr);
  ierr = TSSetRHSFunction(ts,r,RHSFunction,&user);CHKERRQ(ierr);

  /* Set Jacobian */
  ierr = DMSetMatType(da,MATAIJ);CHKERRQ(ierr);
  ierr = DMCreateMatrix(da,&J);CHKERRQ(ierr);
  if (!byhand) {
    ierr = TSSetRHSJacobian(ts,J,J,RHSJacobianADOLC,NULL);CHKERRQ(ierr);
  } else {
    ierr = TSSetRHSJacobian(ts,J,J,RHSJacobianByHand,NULL);CHKERRQ(ierr);
  }

  ftime = 1.0;
  ierr = TSSetMaxTime(ts,ftime);CHKERRQ(ierr);
  ierr = TSSetExactFinalTime(ts,TS_EXACTFINALTIME_STEPOVER);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Set initial conditions
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = FormInitialSolution(da,u,&user);CHKERRQ(ierr);
  dt   = .01;
  ierr = TSSetTimeStep(ts,dt);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Set runtime options
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = TSSetFromOptions(ts);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Solve nonlinear system
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = TSSolve(ts,u);CHKERRQ(ierr);
  ierr = TSGetSolveTime(ts,&ftime);CHKERRQ(ierr);
  ierr = TSGetStepNumber(ts,&steps);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Free work space.
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = MatDestroy(&J);CHKERRQ(ierr);
  ierr = VecDestroy(&u);CHKERRQ(ierr);
  ierr = VecDestroy(&r);CHKERRQ(ierr);
  ierr = TSDestroy(&ts);CHKERRQ(ierr);
  ierr = DMDestroy(&da);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Call destructors for active fields, freeing associated memory in the
     process.
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (!user.no_an) {

    f_a += gys;
    u_a += gys;
    delete[] f_a;
    delete[] u_a;
    delete[] f_c;
    delete[] u_c;
  }


  ierr = PetscFinalize();
  return ierr;
}


PetscErrorCode RHSLocalActive(DM da,PetscScalar **f,PetscScalar **uarray,void *ptr)
{
  AppCtx         *user=(AppCtx*)ptr;
  PetscErrorCode ierr;
  PetscInt       i,j,xs,ys,xm,ym,gxs,gys,gxm,gym;
  PetscReal      hx,hy,sx,sy,two=2.0;
  adouble        **f_a = user->f_a,**u_a = user->u_a;
  adouble        u,uxx,uyy;

  PetscFunctionBegin;
  ierr = DMDAGetCorners(da,&xs,&ys,NULL,&xm,&ym,NULL);CHKERRQ(ierr);
  ierr = DMDAGetGhostCorners(da,&gxs,&gys,NULL,&gxm,&gym,NULL);CHKERRQ(ierr);
  hx = 1.0/(PetscReal)(user->Mx-1); sx = 1.0/(hx*hx);
  hy = 1.0/(PetscReal)(user->My-1); sy = 1.0/(hy*hy);

  trace_on(tag);  // ----------------------------------------------- Start of active section

  /*
    Mark independence

    NOTE: Ghost points are marked as independent at this stage, but their contributions to
          the Jacobian will be added to the corresponding rows on other processes, meaning
          the Jacobian remains square.
  */
  for (j=gys; j<gys+gym; j++) {
    for (i=gxs; i<gxs+gxm; i++) {
      u_a[j][i] <<= uarray[j][i];
    }
  }

  /* Compute function over the locally owned part of the grid */
  for (j=ys; j<ys+ym; j++) {
    for (i=xs; i<xs+xm; i++) {

      // Consider boundary cases
      if (i == 0 || j == 0 || i == user->Mx-1 || j == user->My-1) {
        f_a[j][i] = u_a[j][i];
      } else {
        u         = u_a[j][i];
        uxx       = (-two*u + u_a[j][i-1] + u_a[j][i+1])*sx;
        uyy       = (-two*u + u_a[j-1][i] + u_a[j+1][i])*sy;
        f_a[j][i] = uxx + uyy;
      }

      // Mark dependence
      f_a[j][i] >>= f[j][i];
    }
  }
  trace_off();  // ----------------------------------------------- End of active section

  PetscFunctionReturn(0);
}

PetscErrorCode RHSLocalPassive(DM da,PetscScalar **f,PetscScalar **uarray,void *ptr)
{
  AppCtx         *user=(AppCtx*)ptr;
  PetscErrorCode ierr;
  PetscInt       i,j,xs,ys,xm,ym;
  PetscReal      hx,hy,sx,sy,two=2.0;
  PetscScalar    u,uxx,uyy;

  PetscFunctionBegin;
  ierr = DMDAGetCorners(da,&xs,&ys,NULL,&xm,&ym,NULL);CHKERRQ(ierr);
  hx = 1.0/(PetscReal)(user->Mx-1); sx = 1.0/(hx*hx);
  hy = 1.0/(PetscReal)(user->My-1); sy = 1.0/(hy*hy);
  for (j=ys; j<ys+ym; j++) {
    for (i=xs; i<xs+xm; i++) {
      if (i == 0 || j == 0 || i == user->Mx-1 || j == user->My-1) {
        f[j][i] = uarray[j][i];
        continue;
      }
      u       = uarray[j][i];
      uxx     = (-two*u + uarray[j][i-1] + uarray[j][i+1])*sx;
      uyy     = (-two*u + uarray[j-1][i] + uarray[j+1][i])*sy;
      f[j][i] = uxx + uyy;
    }
  }
  PetscFunctionReturn(0);
}

/* ------------------------------------------------------------------- */
/*
   RHSFunction - Evaluates nonlinear function, F(u).

   Input Parameters:
.  ts - the TS context
.  U - input vector
.  ptr - optional user-defined context, as set by TSSetFunction()

   Output Parameter:
.  F - function vector
 */
PetscErrorCode RHSFunction(TS ts,PetscReal ftime,Vec U,Vec F,void *ptr)
{
  AppCtx         *user=(AppCtx*)ptr;
  DM             da;
  PetscErrorCode ierr;
  PetscInt       xs,ys,xm,ym;
  PetscScalar    **uarray,**f;
  Vec            localU;

  PetscFunctionBeginUser;
  ierr = TSGetDM(ts,&da);CHKERRQ(ierr);
  ierr = DMGetLocalVector(da,&localU);CHKERRQ(ierr);

  /*
     Scatter ghost points to local vector,using the 2-step process
        DMGlobalToLocalBegin(),DMGlobalToLocalEnd().
     By placing code between these two statements, computations can be
     done while messages are in transition.
  */
  ierr = DMGlobalToLocalBegin(da,U,INSERT_VALUES,localU);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(da,U,INSERT_VALUES,localU);CHKERRQ(ierr);

  /* Get pointers to vector data */
  ierr = DMDAVecGetArrayRead(da,localU,&uarray);CHKERRQ(ierr);
  ierr = DMDAVecGetArray(da,F,&f);CHKERRQ(ierr);

  /* Get local grid boundaries */
  ierr = DMDAGetCorners(da,&xs,&ys,NULL,&xm,&ym,NULL);CHKERRQ(ierr);

  /* Compute function over the locally owned part of the grid */
  if (!user->no_an) {
    ierr = RHSLocalActive(da,f,uarray,user);CHKERRQ(ierr);
  } else {
    ierr = RHSLocalPassive(da,f,uarray,user);CHKERRQ(ierr);
  }

  /* Restore vectors */
  ierr = DMDAVecRestoreArrayRead(da,localU,&uarray);CHKERRQ(ierr);
  ierr = DMDAVecRestoreArray(da,F,&f);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(da,&localU);CHKERRQ(ierr);
  ierr = PetscLogFlops(11.0*ym*xm);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* --------------------------------------------------------------------- */
/*
   RHSJacobian - User-provided routine to compute the Jacobian of
   the nonlinear right-hand-side function of the ODE.

   Input Parameters:
   ts - the TS context
   t - current time
   U - global input vector
   dummy - optional user-defined context, as set by TSetRHSJacobian()

   Output Parameters:
   J - Jacobian matrix
   Jpre - optionally different preconditioning matrix
   str - flag indicating matrix structure
*/
PetscErrorCode RHSJacobianByHand(TS ts,PetscReal t,Vec U,Mat J,Mat Jpre,void *ctx)
{
  PetscErrorCode ierr;
  DM             da;
  DMDALocalInfo  info;
  PetscInt       i,j;
  PetscReal      hx,hy,sx,sy;

  PetscFunctionBeginUser;
  ierr = TSGetDM(ts,&da);CHKERRQ(ierr);
  ierr = DMDAGetLocalInfo(da,&info);CHKERRQ(ierr);
  hx   = 1.0/(PetscReal)(info.mx-1); sx = 1.0/(hx*hx);
  hy   = 1.0/(PetscReal)(info.my-1); sy = 1.0/(hy*hy);
  for (j=info.ys; j<info.ys+info.ym; j++) {
    for (i=info.xs; i<info.xs+info.xm; i++) {
      PetscInt    nc = 0;
      MatStencil  row,col[5];
      PetscScalar val[5];
      row.i = i; row.j = j;
      if (i == 0 || j == 0 || i == info.mx-1 || j == info.my-1) {
        col[nc].i = i; col[nc].j = j; val[nc++] = 1.0;
      } else {
        col[nc].i = i-1; col[nc].j = j;   val[nc++] = sx;
        col[nc].i = i+1; col[nc].j = j;   val[nc++] = sx;
        col[nc].i = i;   col[nc].j = j-1; val[nc++] = sy;
        col[nc].i = i;   col[nc].j = j+1; val[nc++] = sy;
        col[nc].i = i;   col[nc].j = j;   val[nc++] = -2*sx - 2*sy;
      }
      ierr = MatSetValuesStencil(Jpre,1,&row,nc,col,val,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = MatAssemblyBegin(Jpre,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(Jpre,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  if (J != Jpre) {
    ierr = MatAssemblyBegin(J,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    ierr = MatAssemblyEnd(J,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/*
  Automatically generated ADOL-C version
*/
PetscErrorCode RHSJacobianADOLC(TS ts,PetscReal t,Vec U,Mat J,Mat Jpre,void *ctx)
{
  AppCtx         *appctx = (AppCtx*)ctx;
  PetscErrorCode ierr;
  DM             da;
  PetscInt       i,j,ii,jj,k = 0,l = 0,xs,ys,xm,ym,gxs,gys,gxm,gym,m,n,loc;
  PetscScalar    **u,*u_vec,**Jac = NULL,**frhs,*fz,norm=0.,diff=0.;
  Vec            localU;

  PetscFunctionBeginUser;
  ierr = TSGetDM(ts,&da);CHKERRQ(ierr);
  ierr = DMGetLocalVector(da,&localU);CHKERRQ(ierr);

  /*
     Scatter ghost points to local vector,using the 2-step process
        DMGlobalToLocalBegin(),DMGlobalToLocalEnd().
     By placing code between these two statements, computations can be
     done while messages are in transition.
  */
  ierr = DMGlobalToLocalBegin(da,U,INSERT_VALUES,localU);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(da,U,INSERT_VALUES,localU);CHKERRQ(ierr);

  /* Get pointers to vector data */
  ierr = DMDAVecGetArrayRead(da,localU,&u);CHKERRQ(ierr);

  /* Get local and ghosted grid boundaries */
  ierr = DMDAGetCorners(da,&xs,&ys,NULL,&xm,&ym,NULL);CHKERRQ(ierr);
  ierr = DMDAGetGhostCorners(da,&gxs,&gys,NULL,&gxm,&gym,NULL);CHKERRQ(ierr);

  /* Convert 2-array to a 1-array, so this can be read by ADOL-C */
  m = xm*ym;    // Number of dependent variables / globally owned points
  n = gxm*gym;  // Number of independent variables / locally owned points
  ierr = PetscMalloc1(n,&u_vec);CHKERRQ(ierr);
  for (j=gys; j<gys+gym; j++) {
    for (i=gxs; i<gxs+gxm; i++) {
      u_vec[k++] = u[j][i];
    }
  }

  /* Test zeroth order scalar evaluation in ADOL-C gives the same result as calling RHSLocalPassive */
  if (appctx->zos) {
    k = 0;
    ierr = PetscMalloc1(m,&fz);CHKERRQ(ierr);
    zos_forward(tag,m,n,0,u_vec,fz);
    frhs = new PetscScalar*[ym];
    for (j=ys; j<ys+ym; j++)
      frhs[j] = new PetscScalar[xm];
    RHSLocalPassive(da,frhs,u,appctx);

    for (j=ys; j<ys+ym; j++) {
      for (i=xs; i<xs+xm; i++) {
        if (appctx->zos_view) {
          if ((fabs(frhs[j][i]) > 1.e-16) && (fabs(fz[k]) > 1.e-16)) {
            PetscPrintf(MPI_COMM_WORLD,"F_rhs[%2d,%2d] = %+.4e, ",j,i,frhs[j][i]);
            PetscPrintf(MPI_COMM_WORLD,"F_zos[%2d,%2d] = %+.4e\n",j,i,fz[k]);
          }
        }
        diff += (frhs[j][i]-fz[k])*(frhs[j][i]-fz[k]);k++;
        norm += frhs[j][i]*frhs[j][i];
      }
    }
    ierr = PetscFree(fz);CHKERRQ(ierr);
    for (j=ys; j<ys+ym; j++)
      delete[] frhs[j];
    delete[] frhs;
    PetscPrintf(MPI_COMM_WORLD,"    ----- Testing Zero Order evaluation -----\n");
    PetscPrintf(MPI_COMM_WORLD,"    ||F_zos(x) - F_rhs(x)||_2/||F_rhs(x)||_2 = %.4e\n",sqrt(diff/norm));
  }

  /*
    Calculate Jacobian using ADOL-C
  */
  if (appctx->sparse) {

    /*
      Generate sparsity pattern  TODO: This only need be done once
    */
    unsigned int **JP = NULL;
    PetscInt     ctrl[3] = {0,0,0},p = 0;

    JP = (unsigned int **) malloc(m*sizeof(unsigned int*));
    jac_pat(tag,m,n,u_vec,JP,ctrl);

    for (i=0;i<m;i++) {
      if ((PetscInt) JP[i][0] > p)
        p = (PetscInt) JP[i][0];
    }

    if (appctx->sparse_view) {
      for (i=0;i<m;i++) {
        ierr = PetscPrintf(PETSC_COMM_WORLD," %d: ",i);CHKERRQ(ierr);
        for (j=1;j<= (PetscInt) JP[i][0];j++)
          ierr = PetscPrintf(PETSC_COMM_WORLD," %d ",JP[i][j]);CHKERRQ(ierr);
        ierr = PetscPrintf(PETSC_COMM_WORLD,"\n");CHKERRQ(ierr);
      }
      ierr = PetscPrintf(PETSC_COMM_WORLD,"\n");CHKERRQ(ierr);
    }
    for (i=0;i<m;i++)
      free(JP[i]);
    free(JP);

    /*
      Colour Jacobian
    */

    ISColoring     iscoloring;
    MatColoring    coloring;

    ierr = MatColoringCreate(Jpre,&coloring);CHKERRQ(ierr);
    ierr = MatColoringSetType(coloring,MATCOLORINGSL);CHKERRQ(ierr);      // 'Smallest last' default
    ierr = MatColoringSetFromOptions(coloring);CHKERRQ(ierr);
    ierr = MatColoringApply(coloring,&iscoloring);CHKERRQ(ierr);

    /*
      Generate seed matrix
    */

    IS             *isp,is;
    PetscScalar    **Seed = NULL;
    PetscInt       nis,size;
    const PetscInt *indices;

    Seed = myalloc2(n,p);
    ierr = ISColoringGetIS(iscoloring,&nis,&isp);CHKERRQ(ierr);
    for (i=0;i<p;i++) {
      is = *(isp+i);
      ierr = ISGetLocalSize(is,&size);CHKERRQ(ierr);
      ierr = ISGetIndices(is,&indices);CHKERRQ(ierr);
      for (j=0;j<size;j++) {
        Seed[indices[j]][i] = 1.;
      }
      ierr = ISRestoreIndices(is,&indices);CHKERRQ(ierr);
    }
    ierr = ISColoringRestoreIS(iscoloring,&isp);CHKERRQ(ierr);

    /*
      Form compressed Jacobian
    */
    PetscScalar **Jcomp;

    ierr = PetscMalloc1(m,&fz);CHKERRQ(ierr);
    zos_forward(tag,m,n,0,u_vec,fz);

    Jcomp = myalloc2(n,p);
    fov_forward(tag,m,n,p,u_vec,Seed,fz,Jcomp);
    ierr = PetscFree(fz);CHKERRQ(ierr);

    /*
      Free workspace
    */

    myfree2(Seed);

    ierr = MatColoringDestroy(&coloring);CHKERRQ(ierr);
    ierr = ISColoringDestroy(&iscoloring);CHKERRQ(ierr);

    // TODO: Use colouring in compressed format

    ierr = PetscPrintf(MPI_COMM_WORLD,"Exiting. Sparse driver not yet complete.\n");CHKERRQ(ierr);
    exit(0);

  } else {

    Jac = myalloc2(m,n);
    jacobian(tag,m,n,u_vec,Jac);
    ierr = PetscFree(u_vec);CHKERRQ(ierr);

    PetscMPIInt rank;
    MPI_Comm_rank(PETSC_COMM_WORLD,&rank);

    /* Insert entries one-by-one. TODO: better to insert row-by-row, similarly as with the stencil */
    ierr = MatZeroEntries(J);CHKERRQ(ierr);
    k = 0;

    for (jj=ys; jj<ys+ym; jj++) {
      for (ii=xs; ii<xs+xm; ii++) {
        for (j=gys; j<gys+gym; j++) {
          for (i=gxs; i<gxs+gxm; i++) {
            if (j < ys) {
              loc = i+j*appctx->My;		// TODO: Test this
            } else if (j >= ym) {
              loc = i+j*appctx->My;		// TODO: Test this
            } else if (i < xs) {
              loc = i+j*appctx->My;		// TODO: Test this
            } else if (i >= xm) {
              loc = i+j*appctx->My;		// TODO: Test this
            } else {
              loc = i+j*appctx->My;
            }
            if (fabs(Jac[k][l])!=0.) {
              ierr = PetscPrintf(PETSC_COMM_WORLD,"RANK %d: i=%2d j=%2d k=%3d l=%3d loc=%3d J=%+.4e\n",rank,i,j,k,l,loc,Jac[k][l]);CHKERRQ(ierr);
              ierr = MatSetValues(J,1,&k,1,&loc,&Jac[k][l],ADD_VALUES);CHKERRQ(ierr);
            }
            l++; // Index including ghost points
          }
        }
        l = 0;
        k++;
      }
    }
    myfree2(Jac);
  }

  /*
    Restore vectors
  */
  ierr = DMDAVecRestoreArrayRead(da,localU,&u);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(da,&localU);CHKERRQ(ierr);

  /*
    Assemble local matrix

    NOTE (from Vec ex2): Each processor can contribute any vector entries, regardless of which
         processor "owns" them; any nonlocal contributions will be transferred to the appropriate
         processor during the assembly process.
  */

  ierr = MatAssemblyBegin(Jpre,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(Jpre,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  if (J != Jpre) {
    ierr = MatAssemblyBegin(J,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    ierr = MatAssemblyEnd(J,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/* ------------------------------------------------------------------- */
PetscErrorCode FormInitialSolution(DM da,Vec U,void* ptr)
{
  AppCtx         *user=(AppCtx*)ptr;
  PetscReal      c=user->c;
  PetscErrorCode ierr;
  PetscInt       i,j,xs,ys,xm,ym,Mx,My;
  PetscScalar    **u;
  PetscReal      hx,hy,x,y,r;

  PetscFunctionBeginUser;
  ierr = DMDAGetInfo(da,PETSC_IGNORE,&Mx,&My,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE);CHKERRQ(ierr);

  hx = 1.0/(PetscReal)(Mx-1);
  hy = 1.0/(PetscReal)(My-1);

  /* Get pointers to vector data */
  ierr = DMDAVecGetArray(da,U,&u);CHKERRQ(ierr);

  /* Get local grid boundaries */
  ierr = DMDAGetCorners(da,&xs,&ys,NULL,&xm,&ym,NULL);CHKERRQ(ierr);

  /* Compute function over the locally owned part of the grid */
  for (j=ys; j<ys+ym; j++) {
    y = j*hy;
    for (i=xs; i<xs+xm; i++) {
      x = i*hx;
      r = PetscSqrtReal((x-.5)*(x-.5) + (y-.5)*(y-.5));
      if (r < .125) u[j][i] = PetscExpReal(c*r*r*r);
      else u[j][i] = 0.0;
    }
  }

  /* Restore vectors */
  ierr = DMDAVecRestoreArray(da,U,&u);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*
  Shift indices in adouble array to endow it with ghost points.
*/
PetscErrorCode AdoubleGiveGhostPoints2d(DM da,adouble *cgs,adouble **a2d[])
{
  PetscErrorCode ierr;
  PetscInt       gxs,gys,gxm,gym,j;

  PetscFunctionBegin;
  ierr = DMDAGetGhostCorners(da,&gxs,&gys,NULL,&gxm,&gym,NULL);CHKERRQ(ierr);
  for (j=0; j<gym; j++) {
    (*a2d)[j] = cgs + j*gxm - gxs;
  }
  *a2d -= gys;
  PetscFunctionReturn(0);
}

/*
  Insert ghost point values into adouble array.
*/
PetscErrorCode AdoubleInsertGhostValues2d(DM da,PetscScalar **u,adouble **u_a)
{
  PetscErrorCode   ierr;
  PetscInt         i,j,xs,ys,xm,ym,gxs,gys,gxm,gym,lower,upper;
  DMDAStencilType  st;
  DMBoundaryType   xbc,ybc;

  PetscFunctionBegin;
  ierr = DMDAGetCorners(da,&xs,&ys,NULL,&xm,&ym,NULL);CHKERRQ(ierr);
  ierr = DMDAGetGhostCorners(da,&gxs,&gys,NULL,&gxm,&gym,NULL);CHKERRQ(ierr);
  ierr = DMDAGetInfo(da,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,&xbc,&ybc,PETSC_IGNORE,&st);CHKERRQ(ierr);

  // Ghost points need to be present, even if unused, as with DM_BOUNDARY_GHOSTED.
  if ((xbc != DM_BOUNDARY_NONE) && (ybc != DM_BOUNDARY_NONE)) {
    lower = ys;upper = ys+ym;
    if (st == DMDA_STENCIL_BOX) {
      lower += gys;upper -= gys;
    }
    for (j=lower; j<upper; j++) {
      for (i=0; i<2; i++) {
        u_a[j][gxs+i*(gxm-1)] = u[j][gxs+i*(gxm-1)];
      }
    }
    lower = xs;upper = xs+xm;
    if (st == DMDA_STENCIL_BOX) {
      lower += gxs;upper -= gxs;
    }
    for (i=lower; i<upper; i++) {
      for (j=0; j<2; j++) {
        u_a[gys+j*(gym-1)][i] = u[gys+j*(gym-1)][i];
      }
    }
  } else {
    SETERRQ(PETSC_COMM_SELF,1,"Ghost points required on boundary.");
  }
  PetscFunctionReturn(0);
}

/*TEST

    test:
      args: -ts_max_steps 5 -ts_monitor 

    test:
      suffix: 2
      args: -ts_max_steps 5 -ts_monitor

    test:
      suffix: 3
      args: -ts_max_steps 5 -snes_fd_color -ts_monitor

TEST*/

