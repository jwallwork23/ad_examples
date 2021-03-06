#include <iostream>
#define ADOLC_TAPELESS
#include <adolc/adtl.h>
using namespace adtl;
#include "example_utils.cpp"

extern PetscErrorCode JacobianVectorProduct(Mat J,Vec x_ro,Vec y);

typedef struct {
  Vec         X,Y;
} AdolcCtx;

int main(int argc,char **args)
{
  AdolcCtx        ctx;
  PetscErrorCode  ierr;
  MPI_Comm        comm = MPI_COMM_WORLD;
  PetscInt        n = 6,m = 3,i,ni[n];
  PetscScalar     *xdat;
  Vec             W;
  Mat             J;

  ierr = PetscInitialize(&argc,&args,(char*)0,NULL);if (ierr) return ierr;

  /* Give values for independent variables */
  ierr = PetscMalloc1(n,&xdat);CHKERRQ(ierr);
  for(i=0;i<n;i++) {
    xdat[i] = log(1.0+i);
    ni[i] = i;
  }

  /* Insert independent variable values into a Vec */
  ierr = VecCreate(comm,&ctx.X);CHKERRQ(ierr);
  ierr = VecSetSizes(ctx.X,PETSC_DECIDE,n);CHKERRQ(ierr);
  ierr = VecSetFromOptions(ctx.X);CHKERRQ(ierr);
  ierr = VecSetValues(ctx.X,n,ni,xdat,INSERT_VALUES);CHKERRQ(ierr);
  ierr = PetscPrintf(comm,"Input vector:\n");CHKERRQ(ierr);
  ierr = VecView(ctx.X,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);

  /* Vec for dependent variables */
  ierr = VecCreate(comm,&ctx.Y);CHKERRQ(ierr);
  ierr = VecSetSizes(ctx.Y,PETSC_DECIDE,m);CHKERRQ(ierr);
  ierr = VecSetFromOptions(ctx.Y);CHKERRQ(ierr);

  /* Create matrix free matrix */
  ierr = MatCreateShell(comm,m,n,m,n,NULL,&J);CHKERRQ(ierr);
  ierr = MatShellSetContext(J,&ctx);CHKERRQ(ierr);
  ierr = MatShellSetOperation(J,MATOP_MULT,(void(*)(void))JacobianVectorProduct);CHKERRQ(ierr);

  /*
    Evaluate Jacobian vector product matrix free:
                  W = J * X
  */
  ierr = VecCreate(comm,&W);CHKERRQ(ierr);
  ierr = VecSetSizes(W,PETSC_DECIDE,m);CHKERRQ(ierr);
  ierr = VecSetFromOptions(W);CHKERRQ(ierr);
  ierr = MatMult(J,ctx.X,W);CHKERRQ(ierr);

  /* Print results */
  ierr = PetscPrintf(comm,"\nFunction evaluation:\n");CHKERRQ(ierr);
  ierr = VecView(ctx.Y,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  ierr = PetscPrintf(comm,"\nJacobian vector product:\n");CHKERRQ(ierr);
  ierr = VecView(W,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);

  ierr = VecDestroy(&W);CHKERRQ(ierr);
  ierr = MatDestroy(&J);CHKERRQ(ierr);
  ierr = VecDestroy(&ctx.Y);CHKERRQ(ierr);
  ierr = VecDestroy(&ctx.X);CHKERRQ(ierr);
  ierr = PetscFree(xdat);CHKERRQ(ierr);

  ierr = PetscFinalize();

  return ierr;
}

/*
  ADOL-C implementation for Jacobian vector product, using the traceless forward mode of AD.
  Intended to overload MatMult in matrix-free methods

  Input parameters:
  J - Jacobian matrix of MatShell type
  x - vector to be multiplied by J

  Output parameters:
  y - product of J and x
*/
PetscErrorCode JacobianVectorProduct(Mat J,Vec x_ro,Vec y)
{
  AdolcCtx          *ctx;
  PetscErrorCode    ierr;
  PetscInt          m,n,i;
  PetscScalar       *x0dat,*x1dat,tmp;
  adouble           *xad,*yad;
  Vec               x0,x1;

  PetscFunctionBegin;

  /* Read data and allocate memory */
  ierr = MatGetSize(J,&m,&n);CHKERRQ(ierr);
  xad = new adouble[n];
  yad = new adouble[m];
  ierr = MatShellGetContext(J,&ctx);CHKERRQ(ierr);
  ierr = VecDuplicate(x_ro,&x1);CHKERRQ(ierr);
  ierr = VecCopy(x_ro,x1);CHKERRQ(ierr);
  ierr = VecGetArray(x1,&x1dat);CHKERRQ(ierr);
  ierr = VecDuplicate(ctx->X,&x0);CHKERRQ(ierr);
  ierr = VecCopy(ctx->X,x0);CHKERRQ(ierr);
  ierr = VecGetArray(x1,&x1dat);CHKERRQ(ierr);
  ierr = VecGetArray(x0,&x0dat);CHKERRQ(ierr);

  /* Call function c(x) and apply Jacobian vector product*/
  for(i=0; i<n; i++) {
    xad[i].setValue(x0dat[i]);
    xad[i].setADValue(&x1dat[i]);
  }
  ierr = ActiveEvaluate(xad,yad);CHKERRQ(ierr);
  for(i=0; i<m; i++) {
    tmp = yad[i].getValue();
    ierr = VecSetValues(ctx->Y,1,&i,&tmp,INSERT_VALUES);CHKERRQ(ierr);
    ierr = VecSetValues(y,1,&i,(PetscScalar*) yad[i].getADValue(),INSERT_VALUES);CHKERRQ(ierr);
  }

  /* Restore arrays and free memory */
  ierr = VecRestoreArray(x0,&x0dat);CHKERRQ(ierr);
  ierr = VecRestoreArray(x1,&x1dat);CHKERRQ(ierr);
  ierr = VecDestroy(&x0);CHKERRQ(ierr);
  ierr = VecDestroy(&x1);CHKERRQ(ierr);
  delete[] yad;
  delete [] xad;
  PetscFunctionReturn(0);
}

