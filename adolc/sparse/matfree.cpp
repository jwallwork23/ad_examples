#include <petscsnes.h>
#include <adolc/adolc.h>
#include <adolc/adolc_sparse.h>

#define tag 1

extern PetscErrorCode PrintMat(MPI_Comm comm,const char* name,PetscInt n,PetscInt m,PetscScalar **M);
extern PetscErrorCode PassiveEvaluate(PetscScalar *x,PetscScalar *c);
extern PetscErrorCode ActiveEvaluate(adouble *x,adouble *c);
extern PetscErrorCode JacobianTransposeVectorProduct(Mat J,Vec U,Vec Action);

int main(int argc,char **args)
{
  PetscErrorCode  ierr;
  MPI_Comm        comm = MPI_COMM_WORLD;
  PetscInt        n = 6,m = 3,i,j,ix[m];
  PetscScalar     x[n],c[m];
  adouble         xad[n],cad[m];
  Vec             C,Z;
  Mat             J;

  ierr = PetscInitialize(&argc,&args,(char*)0,NULL);if (ierr) return ierr;

  /* Give values for independent variables */
  for(i=0;i<n;i++)
    x[i] = log(1.0+i);

  /* Trace function c(x) */
  trace_on(tag);
    for(i=0;i<n;i++)
      xad[i] <<= x[i];

    ierr = ActiveEvaluate(xad,cad);CHKERRQ(ierr);

    for(i=0;i<m;i++)
      cad[i] >>= c[i];
  trace_off();

  /* Function evaluation as above */
  ierr = PetscPrintf(comm,"\n Function evaluation by RHS : ");CHKERRQ(ierr);
  for(j=0;j<m;j++) {
    ierr = PetscPrintf(comm," %e ",c[j]);CHKERRQ(ierr);
  }
  ierr = PetscPrintf(comm,"\n");CHKERRQ(ierr);

  /* Trace over ZOS to check function evaluation and enable reverse mode */
  zos_forward(tag,m,n,1,x,c);
  ierr = PetscPrintf(comm,"\n Function evaluation by ZOS : ");CHKERRQ(ierr);
  for(j=0;j<m;j++) {
    ierr = PetscPrintf(comm," %e ",c[j]);CHKERRQ(ierr);
    ix[j] = j;
  }
  ierr = PetscPrintf(comm,"\n");CHKERRQ(ierr);

  /* Insert dependent variable values into a Vec */
  ierr = VecCreate(comm,&C);CHKERRQ(ierr);
  ierr = VecSetSizes(C,PETSC_DECIDE,m);CHKERRQ(ierr);
  ierr = VecSetFromOptions(C);CHKERRQ(ierr);
  ierr = VecSetValues(C,m,ix,c,INSERT_VALUES);CHKERRQ(ierr);
  //ierr = VecView(C,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);

  /* Create matrix free matrix */
  ierr = MatCreateShell(comm,m,n,m,n,NULL,&J);CHKERRQ(ierr);
  ierr = MatShellSetOperation(J,MATOP_MULT_TRANSPOSE,(void(*)(void))JacobianTransposeVectorProduct);CHKERRQ(ierr);

  /* Evaluate Jacobian matrix free */
  ierr = VecCreate(comm,&Z);CHKERRQ(ierr);
  ierr = VecSetSizes(Z,PETSC_DECIDE,n);CHKERRQ(ierr);
  ierr = VecSetFromOptions(Z);CHKERRQ(ierr);
  //ierr = JacobianTransposeVectorProduct(C,m,n,Z);CHKERRQ(ierr);
  ierr = MatMultTranspose(J,C,Z);CHKERRQ(ierr);
  ierr = PetscPrintf(comm,"Jacobian transpose vector product:\n");CHKERRQ(ierr);
  ierr = VecView(Z,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);

  ierr = VecDestroy(&Z);CHKERRQ(ierr);
  ierr = VecDestroy(&C);CHKERRQ(ierr);
  ierr = MatDestroy(&J);CHKERRQ(ierr);
  ierr = PetscFinalize();

  return ierr;
}

PetscErrorCode JacobianTransposeVectorProduct(Mat J,Vec U,Vec Action)
{
  PetscErrorCode    ierr;
  PetscInt          i,m,n,q = 1;
  const PetscScalar *u;
  PetscScalar       **uarray,**action;

  PetscFunctionBegin;

  /* Read vector data */
  ierr = MatGetSize(J,&m,&n);CHKERRQ(ierr);
  ierr = PetscMalloc1(m,&u);CHKERRQ(ierr);
  uarray = myalloc2(q,m);
  ierr = VecGetArrayRead(U,&u);CHKERRQ(ierr);
  for (i=0; i<m; i++)
    uarray[0][i] = u[i];
  ierr = VecRestoreArrayRead(U,&u);CHKERRQ(ierr);

  /* Compute action */
  action = myalloc2(q,n);
  fov_reverse(tag,m,n,q,uarray,action);

  /* Set values in vector */
  for (i=0; i<n; i++) {
    ierr = VecSetValues(Action,1,&i,&action[0][i],INSERT_VALUES);CHKERRQ(ierr);
  }

  myfree2(action);
  myfree2(uarray);
  ierr = PetscFree(u);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode PrintMat(MPI_Comm comm,const char* name,PetscInt m,PetscInt n,PetscScalar **M)
{
  PetscErrorCode ierr;
  PetscInt       i,j;

  PetscFunctionBegin;
  ierr = PetscPrintf(comm,"%s \n",name);CHKERRQ(ierr);
  for(i=0; i<m ;i++) {
    ierr = PetscPrintf(comm,"\n %d: ",i);CHKERRQ(ierr);
    for(j=0; j<n ;j++)
      ierr = PetscPrintf(comm," %10.4f ", M[i][j]);CHKERRQ(ierr);
  }
  ierr = PetscPrintf(comm,"\n\n");CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode PassiveEvaluate(PetscScalar *x,PetscScalar *c)
{
  PetscFunctionBegin;
  c[0] = 2*x[0]+x[1]-2.0;
  c[0] += PetscCosScalar(x[3])*PetscSinScalar(x[4]);
  c[1] = x[2]*x[2]+x[3]*x[3]-2.0;
  c[2] = 3*x[4]*x[5] - 3.0+PetscSinScalar(x[4]*x[5]);
  PetscFunctionReturn(0);
}

PetscErrorCode ActiveEvaluate(adouble *x,adouble *c)
{
  PetscFunctionBegin;
  c[0] = 2*x[0]+x[1]-2.0;
  c[0] += PetscCosScalar(x[3])*PetscSinScalar(x[4]);
  c[1] = x[2]*x[2]+x[3]*x[3]-2.0;
  c[2] = 3*x[4]*x[5] - 3.0+PetscSinScalar(x[4]*x[5]);
  PetscFunctionReturn(0);
}

