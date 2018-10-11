#include <petscdm.h>
#include <petscdmda.h>
#include <adolc/adolc.h>
#include "contexts.cpp"

extern PetscErrorCode JacobianVectorProduct(Mat A_shell,Vec X,Vec Y);
extern PetscErrorCode JacobianTransposeVectorProduct(Mat A_shell,Vec Y,Vec X);

/*
  ADOL-C implementation for Jacobian vector product, using the forward mode of AD.
  Intended to overload MatMult in matrix-free methods where implicit timestepping
  has been used.

  For an implicit Jacobian we may use the rule that
     G = M*xdot + f(x)    ==>     dG/dx = a*M + df/dx,
  where a = d(xdot)/dx is a constant. Evaluated at x0 and acting upon a vector x1:
     (dG/dx)(x0) * x1 = (a*M + df/dx)(x0) * x1.

  Input parameters:
  A_shell - Jacobian matrix of MatShell type
  X       - vector to be multiplied by A_shell

  Output parameters:
  Y       - product of A_shell and X
*/
PetscErrorCode JacobianVectorProduct(Mat A_shell,Vec X,Vec Y)
{
  MatCtx            *mctx;
  PetscErrorCode    ierr;
  PetscInt          m,n,i,j,k = 0,d;
  const PetscScalar *x0;
  PetscScalar       *action,*x1;
  Vec               localX0,localX1;
  DM                da;
  DMDALocalInfo     info;

  PetscFunctionBegin;

  /* Get matrix-free context info */
  ierr = MatShellGetContext(A_shell,(void**)&mctx);CHKERRQ(ierr);
  m = mctx->m;
  n = mctx->n;

  /* Get local input vectors and extract data, x0 and x1*/
  ierr = TSGetDM(mctx->ts,&da);CHKERRQ(ierr);
  ierr = DMDAGetLocalInfo(da,&info);CHKERRQ(ierr);
  ierr = DMGetLocalVector(da,&localX0);CHKERRQ(ierr);
  ierr = DMGetLocalVector(da,&localX1);CHKERRQ(ierr);
  ierr = DMGlobalToLocalBegin(da,mctx->X,INSERT_VALUES,localX0);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(da,mctx->X,INSERT_VALUES,localX0);CHKERRQ(ierr);
  ierr = DMGlobalToLocalBegin(da,X,INSERT_VALUES,localX1);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(da,X,INSERT_VALUES,localX1);CHKERRQ(ierr);
  ierr = VecGetArrayRead(localX0,&x0);CHKERRQ(ierr);
  ierr = VecGetArray(localX1,&x1);CHKERRQ(ierr);

  /* dF/dx part */
  ierr = PetscMalloc1(m,&action);CHKERRQ(ierr);
  fos_forward(1,m,n,0,x0,x1,NULL,action);     // TODO: Could replace NULL to implement ZOS test
  for (j=info.gys; j<info.gys+info.gym; j++) {
    for (i=info.gxs; i<info.gxs+info.gxm; i++) {
      for (d=0; d<2; d++) {
        if ((i >= info.xs) && (i < info.xs+info.xm) && (j >= info.ys) && (j < info.ys+info.ym)) {
          ierr = VecSetValuesLocal(Y,1,&k,&action[k],INSERT_VALUES);CHKERRQ(ierr);
        }
        k++;
      }
    }
  }
  k = 0;
  ierr = VecAssemblyBegin(Y);CHKERRQ(ierr); /* Note: Need to assemble between separate calls */
  ierr = VecAssemblyEnd(Y);CHKERRQ(ierr);   /*       to INSERT_VALUES and ADD_VALUES         */

  /* a * dF/d(xdot) part */
  fos_forward(2,m,n,0,x0,x1,NULL,action);
  for (j=info.gys; j<info.gys+info.gym; j++) {
    for (i=info.gxs; i<info.gxs+info.gxm; i++) {
      for (d=0; d<2; d++) {
        if ((i >= info.xs) && (i < info.xs+info.xm) && (j >= info.ys) && (j < info.ys+info.ym)) {
          action[k] *= mctx->shift;
          ierr = VecSetValuesLocal(Y,1,&k,&action[k],ADD_VALUES);CHKERRQ(ierr);
        }
        k++;
      }
    }
  }
  ierr = VecAssemblyBegin(Y);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(Y);CHKERRQ(ierr);
  ierr = PetscFree(action);CHKERRQ(ierr);

  /* Restore local vector */
  ierr = VecRestoreArray(localX1,&x1);CHKERRQ(ierr);
  ierr = VecRestoreArrayRead(localX0,&x0);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(da,&localX1);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(da,&localX0);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* Intended to overload MatMultTranspose in matrix-free methods */
PetscErrorCode JacobianTransposeVectorProduct(Mat A_shell,Vec Y,Vec X)
{
  MatCtx            *mctx;
  PetscErrorCode    ierr;
  PetscInt          m,n,i,j,k = 0,d;
  const PetscScalar *y0;
  PetscScalar       *action,*y1;
  Vec               localY0,localY1;
  DM                da;
  DMDALocalInfo     info;

  PetscFunctionBegin;

  /* Get matrix-free context info */
  ierr = MatShellGetContext(A_shell,(void**)&mctx);CHKERRQ(ierr);
  m = mctx->m;
  n = mctx->n;

  /* Get local input vectors and extract data, x0 and x1*/
  ierr = TSGetDM(mctx->ts,&da);CHKERRQ(ierr);
  ierr = DMDAGetLocalInfo(da,&info);CHKERRQ(ierr);
  ierr = DMGetLocalVector(da,&localY0);CHKERRQ(ierr);
  ierr = DMGetLocalVector(da,&localY1);CHKERRQ(ierr);
  ierr = DMGlobalToLocalBegin(da,mctx->X,INSERT_VALUES,localY0);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(da,mctx->X,INSERT_VALUES,localY0);CHKERRQ(ierr);
  // TODO: The above should be Y's. Where is MatMultTranspose used in the call sequence?
  //ierr = DMGlobalToLocalBegin(da,mctx->Y,INSERT_VALUES,localY0);CHKERRQ(ierr);
  //ierr = DMGlobalToLocalEnd(da,mctx->Y,INSERT_VALUES,localY0);CHKERRQ(ierr);
  ierr = DMGlobalToLocalBegin(da,Y,INSERT_VALUES,localY1);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(da,Y,INSERT_VALUES,localY1);CHKERRQ(ierr);
  ierr = VecGetArrayRead(da,localY0,&y0);CHKERRQ(ierr);
  ierr = VecGetArray(da,localY1,&y1);CHKERRQ(ierr);

  /* dF/dx part */
  ierr = PetscMalloc1(n,&action);CHKERRQ(ierr);
  zos_forward(1,m,n,1,y0,NULL); // TODO: This should be optional
  fos_reverse(1,m,n,y1,action);
  for (j=info.gys; j<info.gys+info.gym; j++) {
    for (i=info.gxs; i<info.gxs+info.gxm; i++) {
      for (d=0; d<2; d++) {
        if ((i >= info.xs) && (i < info.xs+info.xm) && (j >= info.ys) && (j < info.ys+info.ym)) {
          ierr = VecSetValuesLocal(Y,1,&k,&action[k],INSERT_VALUES);CHKERRQ(ierr);
        }
        k++;
      }
    }
  }
  k = 0;
  ierr = VecAssemblyBegin(X);CHKERRQ(ierr); /* Note: Need to assemble between separate calls */
  ierr = VecAssemblyEnd(X);CHKERRQ(ierr);   /*       to INSERT_VALUES and ADD_VALUES         */

  /* a * dF/d(xdot) part */
  zos_forward(2,m,n,1,y0,NULL); // TODO: This should be optional
  fos_reverse(2,m,n,y1,action);
  for (j=info.gys; j<info.gys+info.gym; j++) {
    for (i=info.gxs; i<info.gxs+info.gxm; i++) {
      for (d=0; d<2; d++) {
        if ((i >= info.xs) && (i < info.xs+info.xm) && (j >= info.ys) && (j < info.ys+info.ym)) {
          action[k] *= mctx->shift;
          ierr = VecSetValuesLocal(Y,1,&k,&action[k],ADD_VALUES);CHKERRQ(ierr);
        }
        k++;
      }
    }
  }
  ierr = VecAssemblyBegin(X);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(X);CHKERRQ(ierr);
  ierr = PetscFree(action);CHKERRQ(ierr);

  /* Restore local vector */
  ierr = VecRestoreArray(da,localY1,&y1);CHKERRQ(ierr);
  ierr = VecRestoreArrayRead(da,localY0,&y0);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(da,&localY1);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(da,&localY0);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}
