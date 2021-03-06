#include <petscsnes.h>
#include <adolc/adolc.h>
#include <adolc/adolc_sparse.h>
#include "example_utils.cpp"
#include "../../petsc/utils/sparse.cpp"

#define tag 1

int main(int argc,char **args)
{
  PetscErrorCode  ierr;
  MPI_Comm        comm = MPI_COMM_WORLD;

  ierr = PetscInitialize(&argc,&args,(char*)0,NULL);if (ierr) return ierr;

  PetscInt n = 6,m = 3,i,j;
  PetscScalar x[n],c[m];
  adouble xad[n],cad[m];

/****************************************************************************/
/*******                function evaluation                   ***************/
/****************************************************************************/

  for(i=0;i<n;i++)
    x[i] = log(1.0+i);

  /* Tracing of function c(x) */
  trace_on(tag);
    for(i=0;i<n;i++)
      xad[i] <<= x[i];

    ierr = ActiveEvaluate(xad,cad);CHKERRQ(ierr);

    for(i=0;i<m;i++)
      cad[i] >>= c[i];
  trace_off();

  ierr = PetscPrintf(comm,"\n c = ");CHKERRQ(ierr);
  for(j=0;j<m;j++)
      ierr = PetscPrintf(comm," %e ",c[j]);CHKERRQ(ierr);
  ierr = PetscPrintf(comm,"\n\n");CHKERRQ(ierr);

/****************************************************************************/
/********           For comparisons: Full Jacobian                   ********/
/****************************************************************************/

  PetscScalar **Jdense;
  Jdense = myalloc2(m,n);

  jacobian(tag,m,n,x,Jdense);

  ierr = PrintMat(comm," Jacobian:",m,n,Jdense);CHKERRQ(ierr);

/****************************************************************************/
/*******       sparse Jacobians, separate drivers             ***************/
/****************************************************************************/

/*--------------------------------------------------------------------------*/
/*                                                sparsity pattern Jacobian */
/*--------------------------------------------------------------------------*/

  unsigned int  **JP = NULL;                /* compressed block row storage */
  PetscInt      ctrl[3] = {0,0,0};

  JP = (unsigned int **) malloc(m*sizeof(unsigned int*));
  jac_pat(tag, m, n, x, JP, ctrl);

  ierr = PrintSparsity(comm,m,JP);CHKERRQ(ierr);

/*--------------------------------------------------------------------------*/
/*                                                     preallocate nonzeros */
/*--------------------------------------------------------------------------*/

  Mat             J;
  PetscInt        k,p = 0,nnz[m];
  PetscScalar     one = 1.;

  // Get number of nonzeros per row
  for (i=0; i<m; i++)
    nnz[i] = (PetscInt) JP[i][0];

  // Create Jacobian object, assembling with preallocated nonzeros as ones
  ierr = MatCreateSeqAIJ(comm,m,n,0,nnz,&J);CHKERRQ(ierr);
  ierr = MatSetFromOptions(J);CHKERRQ(ierr);
  ierr = MatSetUp(J);CHKERRQ(ierr);
  for (i=0; i<m; i++) {
    for (j=1; j<=nnz[i]; j++) {
      k = JP[i][j];
      ierr = MatSetValues(J,1,&i,1,&k,&one,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = MatAssemblyBegin(J,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(J,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

/*--------------------------------------------------------------------------*/
/*                                      obtain a colouring for the Jacobian */
/*--------------------------------------------------------------------------*/

  ISColoring      iscoloring;
  MatColoring     coloring;

  /*
    Colour Jacobian using 'smallest last' method

    NOTE: Other methods may be selected from the command line using
            -mat_coloring_type <sl,lf,id>
          ('natural' only works for square Jacobians, and 'greedy' and 'jp'
          are for parallel programs.)

          FIXME: Why is there a seg fault in id case?
  */
  ierr = MatColoringCreate(J,&coloring);CHKERRQ(ierr);
  ierr = MatColoringSetType(coloring,MATCOLORINGSL);CHKERRQ(ierr);
  ierr = MatColoringSetFromOptions(coloring);CHKERRQ(ierr);
  ierr = MatColoringApply(coloring,&iscoloring);CHKERRQ(ierr);

/*--------------------------------------------------------------------------*/
/*                                                              seed matrix */
/*--------------------------------------------------------------------------*/

  PetscScalar     **Seed = NULL;
  PetscInt        size;
  IS              *is;
  const PetscInt  *indices;

  ierr = ISColoringGetIS(iscoloring,&p,&is);CHKERRQ(ierr);
  Seed = myalloc2(n,p);
  for (i=0;i<p;i++) {					// Loop over colours
    ierr = ISGetLocalSize(is[i],&size);CHKERRQ(ierr);
    ierr = ISGetIndices(is[i],&indices);CHKERRQ(ierr);
    for (j=0;j<size;j++)				// Loop over associated entries
      Seed[indices[j]][i] = 1.;
    ierr = ISRestoreIndices(is[i],&indices);CHKERRQ(ierr);
  }
  ierr = ISColoringRestoreIS(iscoloring,&is);CHKERRQ(ierr);
  ierr = PrintMat(comm," Seed matrix:",n,p,Seed);CHKERRQ(ierr);

/*--------------------------------------------------------------------------*/
/*                                                      compressed Jacobian */
/*--------------------------------------------------------------------------*/

  PetscScalar **Jcomp;

  Jcomp = myalloc2(m,p);

  fov_forward(tag,m,n,p,x,Seed,c,Jcomp);
  ierr = PrintMat(PETSC_COMM_WORLD," Compressed Jacobian:",m,p,Jcomp);CHKERRQ(ierr);

/*--------------------------------------------------------------------------*/
/*                                                         recover Jacobian */
/*--------------------------------------------------------------------------*/

  PetscInt    colour;
  PetscScalar **Jdecomp,**Rec;

  Jdecomp = myalloc2(m,n);
  Rec = myalloc2(m,p);

  for (i=0;i<m;i++) {
    for (colour=0;colour<p;colour++) {
      Rec[i][colour] = -1.;
      for (k=1;k<=(PetscInt) JP[i][0];k++) {
        j = (PetscInt) JP[i][k];
        if (Seed[j][colour] == 1.) {
          Rec[i][colour] = j;
          break;
        }
      }
    }
  }
  for (i=0;i<m;i++) {
    for (colour=0;colour<p;colour++) {
      j = (PetscInt) Rec[i][colour];
      if (j != -1)
        Jdecomp[i][j] = Jcomp[i][colour];
    }
  }

  ierr = PrintMat(comm," Recovered Jacobian:",m,n,Jdecomp);CHKERRQ(ierr);


/****************************************************************************/
/*******       free workspace and finalise                    ***************/
/****************************************************************************/

  myfree2(Rec);
  myfree2(Jdecomp);
  myfree2(Jcomp);
  myfree2(Seed);
  myfree2(Jdense);
  ierr = MatColoringDestroy(&coloring);CHKERRQ(ierr);
  ierr = ISColoringDestroy(&iscoloring);CHKERRQ(ierr);
  ierr = MatDestroy(&J);CHKERRQ(ierr);
  for (i=0;i<m;i++)
    free(JP[i]);
  free(JP);
  ierr = PetscFinalize();

  return ierr;
}
